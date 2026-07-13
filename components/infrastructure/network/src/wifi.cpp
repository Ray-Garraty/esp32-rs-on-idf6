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
#include "lwip/inet.h"
#include "mdns.h"

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

        // Create AP and STA netif BEFORE esp_wifi_init — required by ESP-IDF
        // (official captive portal example creates netif before wifi init).
        // esp_wifi_init() picks up already-created default interfaces.
        apNetif_ = esp_netif_create_default_wifi_ap();
        if (apNetif_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            return std::unexpected(domain::AppError::Hardware);
        }
        staNetif_ = esp_netif_create_default_wifi_sta();
        if (staNetif_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create STA netif");
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

    if (apNetif_ == nullptr) {
        ESP_LOGE(TAG, "AP netif not created (init() must be called first)");
        return;
    }

    esp_netif_ip_info_t ipInfo{};
    ipInfo.ip.addr = AP_IP;
    ipInfo.netmask.addr = 0x00FFFFFF; // 255.255.255.0
    ipInfo.gw.addr = AP_IP;
    esp_netif_dhcps_stop(apNetif_);
    esp_netif_set_ip_info(apNetif_, &ipInfo);

    // DHCP option 6 (DNS Server): enable DNS server advertisement via DHCP
    // ESP_NETIF_DOMAIN_NAME_SERVER expects a uint8_t: 1 = enable, 0 = disable
    uint8_t dnsEnabled = 1;
    esp_netif_dhcps_option(apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &dnsEnabled, sizeof(dnsEnabled));

    // Set DNS info on the netif so lwip resolves via 192.168.4.1
    esp_netif_dns_info_t dnsInfo{};
    dnsInfo.ip.u_addr.ip4.addr = AP_IP;
    dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(apNetif_, ESP_NETIF_DNS_MAIN, &dnsInfo);

    // DHCP option 114 (Captive Portal URI): required by iOS/Android detection
    char captivePortalUri[] = "http://192.168.4.1/wifi";
    esp_netif_dhcps_option(apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                           captivePortalUri,
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
    apConfig.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    std::strncpy(reinterpret_cast<char*>(apConfig.ap.password),
                 config::AP_PASSWORD, sizeof(apConfig.ap.password) - 1);
    apConfig.ap.password[sizeof(apConfig.ap.password) - 1] = '\0';

    // Ensure APSTA mode (belt-and-suspenders — init() normally sets it,
    // but tryStartSTA() may have changed it to WIFI_MODE_STA).
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set APSTA mode: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &apConfig);
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
    // [INVESTIGATION] Exp F: DNS server disabled
    // startDnsServer();
    ESP_LOGI(TAG, "AP started: %s (192.168.4.1)", apSsid_);
}

bool WifiManager::connectSTA(const char* ssid, const char* password,
                              uint32_t timeoutMs) {
    if (!initialized_) return false;

    diag::FfiGuard guard(72);

    if (staEventGroup_ == nullptr) {
        staEventGroup_ = xEventGroupCreate();
        if (staEventGroup_ == nullptr) return false;
    }
    xEventGroupClearBits(staEventGroup_, STA_CONNECTED_BIT | STA_DISCONNECTED_BIT);

    // If already connected to the same SSID, skip reconnection
    if (staConnected_.load(std::memory_order_acquire) && 
        std::strcmp(staSsid_, ssid) == 0) {
        ESP_LOGI(TAG, "connectSTA: already connected to %s", ssid);
        return true;
    }

    ESP_LOGI(TAG, "connectSTA: ssid='%s' (len=%zu), password_len=%zu",
             ssid, strlen(ssid), strlen(password));

    wifi_config_t staConfig{};
    std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid),
                 ssid, sizeof(staConfig.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(staConfig.sta.password),
                 password, sizeof(staConfig.sta.password) - 1);
    staConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &staConfig);
    ESP_LOGI(TAG, "connectSTA: set_config result=%s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set STA config: %s", esp_err_to_name(err));
        return false;
    }

    staConnecting_ = true;
    err = esp_wifi_connect();
    ESP_LOGI(TAG, "connectSTA: connect result=%s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi connect: %s", esp_err_to_name(err));
        staConnecting_ = false;
        return false;
    }

    ESP_LOGI(TAG, "Connecting to STA: %s (timeout=%lu ms)", ssid,
             (unsigned long)timeoutMs);

    EventBits_t bits = xEventGroupWaitBits(
        staEventGroup_,
        STA_CONNECTED_BIT | STA_DISCONNECTED_BIT,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeoutMs));
    ESP_LOGI(TAG, "connectSTA: event_bits=0x%x (CONNECTED=%d, DISCONNECTED=%d, TIMEOUT=%d)",
             bits,
             (bits & STA_CONNECTED_BIT) != 0,
             (bits & STA_DISCONNECTED_BIT) != 0,
             bits == 0);

    if (bits & STA_CONNECTED_BIT) {
        std::strncpy(staSsid_, ssid, sizeof(staSsid_) - 1);
        staSsid_[sizeof(staSsid_) - 1] = '\0';
        saveCredentials(ssid, password);
        ESP_LOGI(TAG, "STA connected via connectSTA: %s", ssid);
        return true;
    }

    ESP_LOGW(TAG, "STA connection timeout/failure for: %s", ssid);
    staConnecting_ = false;
    esp_wifi_disconnect();
    return false;
}

bool WifiManager::tryStartSTA() {
    if (!initialized_ || staConnecting_) return false;

    // Read number of saved networks and log them
    auto countResult = storage::wifiReadCount();
    uint8_t count = countResult ? *countResult : 0;
    if (count == 0) {
        ESP_LOGI(TAG, "NVS: no saved WiFi networks");
        return false;
    }
    ESP_LOGI(TAG, "NVS: %u saved WiFi network(s)", count);
    for (uint8_t s = 0; s < count && s < config::WIFI_MAX_NETWORKS; s++) {
        char keyBuf[16];
        char ssidBuf[64]{};
        std::snprintf(keyBuf, sizeof(keyBuf), "ssid_%u", s);
        auto r = storage::wifiReadStr(keyBuf, ssidBuf);
        if (r && r->has_value()) {
            ESP_LOGI(TAG, "NVS slot %u: SSID=%s", s, ssidBuf);
        }
    }

    diag::FfiGuard guard(72);

    // Ensure event group exists
    if (staEventGroup_ == nullptr) {
        staEventGroup_ = xEventGroupCreate();
        if (staEventGroup_ == nullptr) return false;
    }

    // Start WiFi in STA mode if not already running.
    // init() sets WIFI_MODE_APSTA but does NOT call esp_wifi_start().
    // Handle both WIFI_MODE_NULL (uninitialized) and WIFI_MODE_APSTA
    // (init called but not started) by switching to STA and starting.
    bool startedHere = false;
    wifi_mode_t currentMode;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_mode(&currentMode));
    if (currentMode == WIFI_MODE_NULL || currentMode == WIFI_MODE_APSTA) {
        // Either init() hasn't been called (NULL) or it set APSTA without
        // starting (init() line 75). Switch to STA-only and start.
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
        esp_err_t err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
            return false;
        }
        startedHere = true;
    }
    // If currentMode == WIFI_MODE_STA or WIFI_MODE_AP, WiFi is already
    // running — proceed with connection attempt.

    if (staNetif_ == nullptr) {
        ESP_LOGE(TAG, "STA netif not created (init() must be called first)");
        return false;
    }

    // Try each saved network in order
    for (uint8_t slot = 0; slot < count; slot++) {
        char keyBuf[16];
        char ssidBuf[64]{};
        char passBuf[64]{};

        std::snprintf(keyBuf, sizeof(keyBuf), "ssid_%u", slot);
        auto ssidResult = storage::wifiReadStr(keyBuf, ssidBuf);
        if (!ssidResult || !ssidResult->has_value()) continue;

        std::snprintf(keyBuf, sizeof(keyBuf), "password_%u", slot);
        auto passResult = storage::wifiReadStr(keyBuf, passBuf);
        if (!passResult || !passResult->has_value()) continue;

        ESP_LOGI(TAG, "Trying STA slot %u: %s", slot, ssidBuf);

        xEventGroupClearBits(staEventGroup_, STA_CONNECTED_BIT | STA_DISCONNECTED_BIT);

        wifi_config_t staConfig{};
        std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid),
                     ssidBuf, sizeof(staConfig.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char*>(staConfig.sta.password),
                     passBuf, sizeof(staConfig.sta.password) - 1);
        staConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &staConfig);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "STA slot %u set_config failed: %s", slot, esp_err_to_name(err));
            continue;
        }

        staConnecting_ = true;
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            staConnecting_ = false;
            ESP_LOGE(TAG, "STA slot %u connect failed: %s", slot, esp_err_to_name(err));
            continue;
        }

        // Blocking wait per slot (6s per network, covers auth + DHCP)
        EventBits_t bits = xEventGroupWaitBits(
            staEventGroup_,
            STA_CONNECTED_BIT | STA_DISCONNECTED_BIT,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(6000));

        if (bits & STA_CONNECTED_BIT) {
            auto sv = ssidResult->value();
            size_t len = std::min(sv.size(), sizeof(staSsid_) - 1);
            std::memcpy(staSsid_, sv.data(), len);
            staSsid_[len] = '\0';
            ESP_LOGI(TAG, "Connected to STA: %s", staSsid_);
            return true;
        }

        staConnecting_ = false;
        esp_wifi_disconnect();
        ESP_LOGW(TAG, "STA slot %u failed (%s)", slot, ssidBuf);
    }

    // If we started WiFi but all slots failed, stop and restore APSTA
    // mode so that startAP() (called by netTaskEntry on failure) can
    // start cleanly.
    if (startedHere) {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "wifi stop after STA failure: %s", esp_err_to_name(stop_err));
        }
        // Restore APSTA mode expected by startAP() fallback
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_APSTA));
    }
    ESP_LOGW(TAG, "All %u saved networks failed", count);
    return false;
}

void WifiManager::saveCredentials(const char* ssid, const char* password) {
    char keyBuf[16];
    uint8_t count = 0;
    auto countResult = storage::wifiReadCount();
    if (countResult) count = *countResult;

    // Check if SSID already exists — update in place
    for (uint8_t i = 0; i < count; i++) {
        std::snprintf(keyBuf, sizeof(keyBuf), "ssid_%u", i);
        char existing[64]{};
        auto r = storage::wifiReadStr(keyBuf, existing);
        if (r && r->has_value() && r->value() == ssid) {
            std::snprintf(keyBuf, sizeof(keyBuf), "password_%u", i);
            std::ignore = storage::wifiWriteStr(keyBuf, password);
            ESP_LOGI(TAG, "Updated password for existing network: %s", ssid);
            return;
        }
    }

    if (count < config::WIFI_MAX_NETWORKS) {
        // Append new slot
        std::snprintf(keyBuf, sizeof(keyBuf), "ssid_%u", count);
        std::ignore = storage::wifiWriteStr(keyBuf, ssid);
        std::snprintf(keyBuf, sizeof(keyBuf), "password_%u", count);
        std::ignore = storage::wifiWriteStr(keyBuf, password);
        std::ignore = storage::wifiWriteCount(count + 1);
        ESP_LOGI(TAG, "Saved new network %s (slot %u, total %u)", ssid, count, count + 1);
    } else {
        // FIFO eviction: shift slot 1..MAX-1 to 0..MAX-2
        for (uint8_t i = 1; i < config::WIFI_MAX_NETWORKS; i++) {
            char srcKey[16], dstKey[16];
            std::snprintf(srcKey, sizeof(srcKey), "ssid_%u", i);
            std::snprintf(dstKey, sizeof(dstKey), "ssid_%u", i - 1);
            char buf[64]{};
            auto r = storage::wifiReadStr(srcKey, buf);
            if (r && r->has_value()) {
                std::ignore = storage::wifiWriteStr(dstKey, buf);
            }
            std::snprintf(srcKey, sizeof(srcKey), "password_%u", i);
            std::snprintf(dstKey, sizeof(dstKey), "password_%u", i - 1);
            buf[0] = '\0';
            r = storage::wifiReadStr(srcKey, buf);
            if (r && r->has_value()) {
                std::ignore = storage::wifiWriteStr(dstKey, buf);
            }
        }
        // Write new network to last slot
        uint8_t last = config::WIFI_MAX_NETWORKS - 1;
        std::snprintf(keyBuf, sizeof(keyBuf), "ssid_%u", last);
        std::ignore = storage::wifiWriteStr(keyBuf, ssid);
        std::snprintf(keyBuf, sizeof(keyBuf), "password_%u", last);
        std::ignore = storage::wifiWriteStr(keyBuf, password);
        ESP_LOGI(TAG, "Saved new network %s (FIFO eviction, slot %u)", ssid, last);
    }
}

bool WifiManager::waitForSTA(uint32_t timeoutMs) {
    if (!initialized_ || !staConnecting_) return staConnected_.load(std::memory_order_acquire);

    xEventGroupClearBits(staEventGroup_, STA_CONNECTED_BIT | STA_DISCONNECTED_BIT);

    EventBits_t bits = xEventGroupWaitBits(
        staEventGroup_,
        STA_CONNECTED_BIT | STA_DISCONNECTED_BIT,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeoutMs));

    if (bits & STA_CONNECTED_BIT) {
        ESP_LOGI(TAG, "waitForSTA: connected (bit=0x%x)", bits);
        return true;
    }

    ESP_LOGW(TAG, "waitForSTA: timeout/failure after %lu ms", (unsigned long)timeoutMs);
    staConnecting_ = false;
    esp_wifi_disconnect();
    return false;
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

        // Diagnostic: log DNS query source and domain name
        char domainBuf[256]{};
        size_t domainLen = domain::extractDomainName(
            query.data(), static_cast<size_t>(len),
            domainBuf, sizeof(domainBuf));
        char srcIp[16]{};
        inet_ntop(AF_INET, &from.sin_addr, srcIp, sizeof(srcIp));
        if (domainLen > 0) {
            ESP_LOGI(TAG, "DNS query from %s:%d: %d bytes (domain=%s)",
                     srcIp, ntohs(from.sin_port), len, domainBuf);
        } else {
            ESP_LOGI(TAG, "DNS query from %s:%d: %d bytes",
                     srcIp, ntohs(from.sin_port), len);
        }

        domain::memory::DnsBuf response{};
        auto result = domain::tryBuildDnsResponse(
            query.data(), static_cast<size_t>(len),
            response, AP_IP);
        if (!result) return;

        size_t written = *result;
        lwip_sendto(dnsSocket_, response.data(), written, 0,
                    reinterpret_cast<sockaddr*>(&from), sizeof(from));
        ESP_LOGI(TAG, "DNS response to %s:%d: %zu bytes",
                 srcIp, ntohs(from.sin_port), written);
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
    if (mdnsInitDone_) return;
    diag::FfiGuard guard(76);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }

    err = mdns_hostname_set("ecotiter");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS set_hostname failed: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }

    err = mdns_service_add("EcoTiter Burette Controller", "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS add_service failed: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }

    mdnsInitDone_ = true;
    ESP_LOGI(TAG, "mDNS started: ecotiter.local (_http._tcp, port 80)");
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
            if (staEventGroup_) {
                xEventGroupSetBits(staEventGroup_, STA_CONNECTED_BIT);
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGW(TAG, "STA disconnected");
            staConnected_.store(false);
            if (staConnecting_) {
                staConnecting_ = false;
            }
            if (staEventGroup_) {
                xEventGroupSetBits(staEventGroup_, STA_DISCONNECTED_BIT);
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

            // STA connected — stop AP to save power and avoid confusion
            if (apActive_) {
                stopDnsServer();
                esp_wifi_set_mode(WIFI_MODE_STA);
                apActive_ = false;
                ESP_LOGI(TAG, "AP stopped (STA connected)");
            }
        }
    }
}

} // namespace ecotiter::infrastructure::network
