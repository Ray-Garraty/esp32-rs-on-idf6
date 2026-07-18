#pragma once

#include <array>
#include <cstdint>
#include <expected>

#include "esp_http_server.h"

#include "domain/errors.hpp"

namespace ecotiter::infrastructure::network
{

class WifiManager; // forward decl

static constexpr size_t WS_MAX_SESSIONS = 4;
static constexpr int WS_FD_INVALID = -1;

struct WsSession
{
    int fd{WS_FD_INVALID};
};

class HttpServer
{
public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    [[nodiscard]] std::expected<void, domain::AppError> init();
    httpd_handle_t handle() const noexcept { return handle_; }

    void setWifiManager(WifiManager* mgr) noexcept { wifiManager_ = mgr; }
    WifiManager* wifiManager() const noexcept { return wifiManager_; }

    void registerRoutes();

    // WebSocket broadcast — call from any thread
    void broadcastWsEvent(const char* jsonData, size_t len);

    // WebSocket session tracking
    [[nodiscard]] WsSession* findSession(int fd);
    void addSession(int fd);
    void removeSession(int fd);

    // Increased from 12288 to 16384 after investigation confirmed
    // POST /api/valve handler chain uses ~10588 bytes of stack,
    // leaving only 1700 bytes before the deepest call (handleSetPosition),
    // causing stack overflow into FreeRTOS scheduler data (LL-050).
    static constexpr size_t STACK_SIZE = 16384;

private:
    httpd_handle_t handle_{nullptr};
    WifiManager* wifiManager_{nullptr};

    std::array<WsSession, WS_MAX_SESSIONS> sessions_{};
};

} // namespace ecotiter::infrastructure::network
