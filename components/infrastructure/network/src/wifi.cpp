#include "infrastructure/network/wifi.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
// mdns not in IDF v6 built-in components; handled via idf_component.yml
// #include "mdns.h"

#include "infrastructure/config.hpp"
#include "infrastructure/storage/nvs.hpp"
#include "domain/dns.hpp"
#include "diag/ffi_guard.hpp"

static constexpr auto TAG = "wifi";

namespace ecotiter::infrastructure::network {

WifiManager::WifiManager() {
    std::snprintf(apSsid_, sizeof(apSsid_), "%s", AP_SSID);
}

WifiManager::~WifiManager() {
    stop();
}

std::expected<void, domain::AppError> WifiManager::init() {
    if (initialized_) return {};

    {
        diag::FfiGuard guard(70);

        ESP_LOGI(TAG, "Initializing WiFi");
        esp_netif_init();

        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "event loop create: %s", esp_err_to_name(err));
            return std::unexpected(domain::AppError::Hardware);
        }

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi init: %s", esp_err_to_name(err));
            return std::unexpected(domain::AppError::Hardware);
        }

        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi set storage: %s", esp_err_to_name(err));
            return std::unexpected(domain::AppError::Hardware);
        }

        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi set mode: %s", esp_err_to_name(err));
            return std::unexpected(domain::AppError::Hardware);
        }

        err = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &eventHandler, this, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register wifi handler: %s", esp_err_to_name(err));
            return std::unexpected(domain::AppError::Hardware);
        }

        err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &eventHandler, this, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register IP handler: %s", esp_err_to_name(err));
            return std::unexpected(domain::AppError::Hardware);
        }
    }

    initialized_ = true;
    ESP_LOGI(TAG, "WiFi initialized");
    return {};
}

void WifiManager::startAP() {
    if (!initialized_ || apActive_) return;

    diag::FfiGuard guard(71);

    apNetif_ = esp_netif_create_default_wifi_ap();
    if (apNetif_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return;
    }

    esp_netif_ip_info_t ipInfo{};
    ipInfo.ip.addr = AP_IP;
    ipInfo.netmask.addr = 0x00FFFFFF; // 255.255.255.0
    ipInfo.gw.addr = AP_IP;
    esp_netif_dhcps_stop(apNetif_);
    esp_netif_set_ip_info(apNetif_, &ipInfo);

    // DHCP option 6 (DNS Server): advertise ESP32 as DNS server (192.168.4.1)
    // ESP-IDF v6 removed LWIP_DHCPS_ADD_DNS, so must set explicitly
    uint32_t dnsIp = AP_IP;
    esp_netif_dhcps_option(apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &dnsIp, sizeof(dnsIp));

    // Set DNS info on the netif so lwip resolves via 192.168.4.1
    esp_netif_dns_info_t dnsInfo{};
    dnsInfo.ip.u_addr.ip4.addr = AP_IP;
    dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(apNetif_, ESP_NETIF_DNS_MAIN, &dnsInfo);

    // DHCP option 114 (Captive Portal URI): required by iOS/Android detection
    static constexpr char captivePortalUri[] = "http://192.168.4.1/wifi";
    esp_netif_dhcps_option(apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                           const_cast<char*>(captivePortalUri),
                           strlen(captivePortalUri) + 1);

    esp_netif_dhcps_start(apNetif_);

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    int n = std::snprintf(ssid, sizeof(ssid), "EcoTiter-%02X%02X",
                          static_cast<unsigned>(mac[4]),
                          static_cast<unsigned>(mac[3]));
    if (n > 0) {
        std::strncpy(apSsid_, ssid, sizeof(apSsid_) - 1);
    }

    wifi_config_t apConfig{};
    std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), apSsid_,
                 sizeof(apConfig.ap.ssid) - 1);
    apConfig.ap.ssid_len = static_cast<uint8_t>(std::strlen(apSsid_));
    apConfig.ap.channel = 1;
    apConfig.ap.max_connection = 4;
    apConfig.ap.authmode = WIFI_AUTH_OPEN;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &apConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set AP config: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi start: %s", esp_err_to_name(err));
        return;
    }

    apActive_ = true;
    startDnsServer();
    ESP_LOGI(TAG, "AP started: %s (192.168.4.1)", apSsid_);
}

bool WifiManager::tryStartSTA() {
    if (!initialized_ || staConnecting_) return false;

    char ssidBuf[64]{};
    char passBuf[64]{};

    {
        auto ssidResult = storage::wifiReadStr(config::NVS_KEY_WIFI_SSID, ssidBuf);
        if (!ssidResult || !ssidResult->has_value()) {
            ESP_LOGI(TAG, "No saved WiFi credentials");
            return false;
        }
        auto passResult = storage::wifiReadStr(config::NVS_KEY_WIFI_PASS, passBuf);
        if (!passResult || !passResult->has_value()) {
            ESP_LOGI(TAG, "No saved WiFi password");
            return false;
        }

        auto ssidSv = ssidResult->value();
        size_t ssidLen = std::min(ssidSv.size(), sizeof(staSsid_) - 1);
        std::memcpy(staSsid_, ssidSv.data(), ssidLen);
        staSsid_[ssidLen] = '\0';
    }

    diag::FfiGuard guard(72);

    staNetif_ = esp_netif_create_default_wifi_sta();
    if (staNetif_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return false;
    }

    wifi_config_t staConfig{};
    std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid),
                 ssidBuf, sizeof(staConfig.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(staConfig.sta.password),
                 passBuf, sizeof(staConfig.sta.password) - 1);
    staConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &staConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set STA config: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi connect: %s", esp_err_to_name(err));
        return false;
    }

    staConnecting_ = true;
    ESP_LOGI(TAG, "Connecting to STA: %s", staSsid_);
    return true;
}

void WifiManager::stop() {
    if (!initialized_) return;

    diag::FfiGuard guard(73);

    stopDnsServer();

    if (staConnecting_ || staConnected_.load()) {
        esp_wifi_disconnect();
    }
    staConnecting_ = false;
    staConnected_.store(false);

    if (apActive_) {
        apActive_ = false;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (apNetif_) {
        esp_netif_destroy(apNetif_);
        apNetif_ = nullptr;
    }
    if (staNetif_) {
        esp_netif_destroy(staNetif_);
        staNetif_ = nullptr;
    }

    initialized_ = false;
    ESP_LOGI(TAG, "WiFi stopped");
}

bool WifiManager::isConnected() const noexcept {
    return staConnected_.load(std::memory_order_acquire);
}

bool WifiManager::isApActive() const noexcept {
    return apActive_;
}

std::optional<uint32_t> WifiManager::getAPIP() const noexcept {
    if (!apNetif_) return {};
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(apNetif_, &ip) == ESP_OK) {
        return ip.ip.addr;
    }
    return {};
}

std::optional<uint32_t> WifiManager::getSTAIP() const noexcept {
    if (!staNetif_) return {};
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(staNetif_, &ip) == ESP_OK) {
        return ip.ip.addr;
    }
    return {};
}

void WifiManager::process() {
    if (!initialized_ || dnsSocket_ < 0) return;

    domain::memory::DnsBuf query{};
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);

    {
        diag::FfiGuard guard(74);
        int len = lwip_recvfrom(dnsSocket_, query.data(), query.size(), 0,
                                reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (len <= 0) return;

        domain::memory::DnsBuf response{};
        auto result = domain::tryBuildDnsResponse(
            query.data(), static_cast<size_t>(len),
            response, AP_IP);
        if (!result) return;

        size_t written = *result;
        lwip_sendto(dnsSocket_, response.data(), written, 0,
                    reinterpret_cast<sockaddr*>(&from), sizeof(from));
    }
}

void WifiManager::startDnsServer() {
    if (dnsSocket_ >= 0) return;

    diag::FfiGuard guard(75);

    dnsSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dnsSocket_ < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        return;
    }

    int reuse = 1;
    setsockopt(dnsSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 1000ms timeout — prevents lwip_recvfrom() blocking indefinitely
    // lwip_recvfrom() returns -1 (EAGAIN) on timeout, which process() handles gracefully
    int timeout = 1000;
    setsockopt(dnsSocket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    esp_err_t err = bind(dnsSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (err != 0) {
        ESP_LOGE(TAG, "DNS bind failed: %d", err);
        close(dnsSocket_);
        dnsSocket_ = -1;
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
}

void WifiManager::stopDnsServer() {
    if (dnsSocket_ >= 0) {
        close(dnsSocket_);
        dnsSocket_ = -1;
        ESP_LOGI(TAG, "DNS server stopped");
    }
}

void WifiManager::startMdns() {
    // mDNS requires esp_mdns from IDF Component Registry.
    // Enable by: idf.py add-dependency "esp_mdns^1.0"
    // Then uncomment mdns_init() call below.
    ESP_LOGI(TAG, "mDNS skipped — add esp_mdns component dependency to enable");
}

void WifiManager::eventHandler(void* arg, esp_event_base_t base,
                                int32_t id, void* data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (self) self->handleEvent(base, id, data);
}

void WifiManager::handleEvent(esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            if (staConnecting_) {
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGW(TAG, "STA disconnected");
            staConnected_.store(false);
            if (staConnecting_) {
                staConnecting_ = false;
            }
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Station connected to AP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Station disconnected from AP");
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            auto* event = static_cast<ip_event_got_ip_t*>(data);
            staConnected_.store(true);
            staConnecting_ = false;

            char ipStr[16];
            esp_ip4addr_ntoa(&event->ip_info.ip, ipStr, sizeof(ipStr));
            ESP_LOGI(TAG, "STA got IP: %s", ipStr);

            startMdns();
        }
    }
}

} // namespace ecotiter::infrastructure::network
