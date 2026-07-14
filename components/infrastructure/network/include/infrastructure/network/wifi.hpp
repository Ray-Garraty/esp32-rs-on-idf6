#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>

#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "domain/errors.hpp"
#include "domain/memory.hpp"

namespace ecotiter::infrastructure::network {

class WifiManager {
public:
    WifiManager();
    ~WifiManager();

    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;
    WifiManager(WifiManager&&) = delete;
    WifiManager& operator=(WifiManager&&) = delete;

    [[nodiscard]] std::expected<void, domain::AppError> init();
    void startAP();
    [[nodiscard]] bool tryStartSTA();
    [[nodiscard]] bool waitForSTA(uint32_t timeoutMs = 10000);
    void saveCredentials(const char* ssid, const char* password);

    // Connect to STA with explicit credentials (blocking, up to timeoutMs).
    // Returns true if connected, false on timeout/failure.
    // Saves credentials to NVS on success.
    [[nodiscard]] bool connectSTA(const char* ssid, const char* password,
                                  uint32_t timeoutMs = 15000);

    void stop();

    [[nodiscard]] bool isConnected() const noexcept;
    [[nodiscard]] bool isApActive() const noexcept;
    [[nodiscard]] std::optional<uint32_t> getAPIP() const noexcept;
    [[nodiscard]] std::optional<uint32_t> getSTAIP() const noexcept;

    // Non-blocking: poll DNS UDP socket for captive portal
    void process() const;

private:
    static void eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);
    void handleEvent(esp_event_base_t base, int32_t id, void* data);
    void stopDnsServer();
    void startMdns();

    static constexpr uint32_t AP_IP = 0x0104A8C0; // 192.168.4.1 in network order
    static constexpr const char* AP_SSID = "EcoTiter-AP";

    static constexpr EventBits_t STA_CONNECTED_BIT  = 1 << 0;
    static constexpr EventBits_t STA_DISCONNECTED_BIT = 1 << 1;

    bool initialized_{false};
    bool apActive_{false};
    std::atomic<bool> staConnected_{false};
    bool staConnecting_{false};

    esp_netif_t* apNetif_{nullptr};
    esp_netif_t* staNetif_{nullptr};
    int dnsSocket_{-1};

    EventGroupHandle_t staEventGroup_{nullptr};

    bool mdnsInitDone_{false};
    char apSsid_[32]{};
    char staSsid_[32]{};
};

} // namespace ecotiter::infrastructure::network
