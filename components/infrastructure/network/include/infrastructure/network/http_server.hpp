#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <expected>
#include <optional>

#include "esp_http_server.h"

#include "domain/errors.hpp"

namespace ecotiter::infrastructure::network {

static constexpr size_t WS_MAX_SESSIONS = 4;
static constexpr int WS_FD_INVALID = -1;

struct WsSession {
    int fd{WS_FD_INVALID};
};

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    [[nodiscard]] std::expected<void, domain::AppError> init();
    httpd_handle_t handle() const noexcept { return handle_; }

    void registerRoutes();

    // WebSocket broadcast — call from any thread
    void broadcastWsEvent(const char* jsonData, size_t len);

    // Access session tracking
    [[nodiscard]] WsSession* findSession(int fd);
    void addSession(int fd);
    void removeSession(int fd);

    static constexpr size_t STACK_SIZE = 12288;

private:
    httpd_handle_t handle_{nullptr};

    std::array<WsSession, WS_MAX_SESSIONS> sessions_{};
};

} // namespace ecotiter::infrastructure::network
