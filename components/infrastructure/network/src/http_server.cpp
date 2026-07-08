#include "infrastructure/network/http_server.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"

#include "interface/rest_api.hpp"
#include "interface/webui.hpp"
#include "domain/types.hpp"
#include "domain/memory.hpp"
#include "diag/ffi_guard.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static constexpr auto TAG = "http_srv";

namespace ecotiter::infrastructure::network {

HttpServer::HttpServer() = default;

HttpServer::~HttpServer() {
    if (handle_) {
        diag::FfiGuard guard(80);
        httpd_stop(handle_);
        handle_ = nullptr;
    }
}

std::expected<void, domain::AppError> HttpServer::init() {
    if (handle_) return {};

    diag::FfiGuard guard(80);

    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "DRAM before HTTP server: free=%zu bytes, largest_block=%zu bytes",
             freeHeap, largestBlock);
    if (freeHeap < 16384) {
        ESP_LOGW(TAG, "Low DRAM for HTTP server: free=%zu bytes, largest_block=%zu bytes",
                 freeHeap, largestBlock);
        return std::unexpected(domain::AppError::Resource);
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = STACK_SIZE;
    config.max_uri_handlers = 30;
    config.lru_purge_enable = true;
    // LWIP_MAX_SOCKETS=5, HTTP server uses 3 internally → 2 for clients
    config.max_open_sockets = 4;

    esp_err_t err = httpd_start(&handle_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start: %s", esp_err_to_name(err));
        return std::unexpected(domain::AppError::Hardware);
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return {};
}

namespace {

esp_err_t captive_redirect_handler(httpd_req_t* req) {
    diag::FfiGuard guard(81);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/wifi");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t captive_wifi_page_handler(httpd_req_t* req) {
    diag::FfiGuard guard(81);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    auto sv = interface::webui::getFile("/wifi");
    httpd_resp_send(req, sv.data(), static_cast<ssize_t>(sv.size()));
    return ESP_OK;
}

esp_err_t captive_wifi_connect_handler(httpd_req_t* req) {
    diag::FfiGuard guard(81);

    domain::memory::CommandBuffer body{};
    size_t bodyLen = std::min(
        static_cast<size_t>(req->content_len),
        body.size());
    int ret = httpd_req_recv(req, body.data(), bodyLen);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
        return ESP_FAIL;
    }
    bodyLen = static_cast<size_t>(ret);

    domain::memory::ResponseBuffer rsp{};
    int n = std::snprintf(rsp.data(), rsp.size(),
        R"({"success":true,"message":"Credentials received"})");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, rsp.data(), static_cast<ssize_t>(n));
    ESP_LOGI(TAG, "WiFi connect request: %.*s",
             static_cast<int>(bodyLen), body.data());
    return ESP_OK;
}

esp_err_t captive_wifi_status_handler(httpd_req_t* req) {
    diag::FfiGuard guard(81);
    domain::memory::ResponseBuffer rsp{};
    int n = std::snprintf(rsp.data(), rsp.size(),
        R"({"ap":"EcoTiter-AP","sta":false,"ip":"192.168.4.1"})");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, rsp.data(), static_cast<ssize_t>(n));
    return ESP_OK;
}

esp_err_t api_ping_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    return interface::ping_handler(req);
}

esp_err_t api_status_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    return interface::status_handler(req);
}

esp_err_t api_command_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    return interface::command_handler(req);
}

esp_err_t api_valve_get_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    return interface::valve_get_handler(req);
}

esp_err_t api_valve_post_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    return interface::valve_post_handler(req);
}

esp_err_t api_logs_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    domain::memory::ResponseBuffer rsp{};
    int n = std::snprintf(rsp.data(), rsp.size(),
        R"({"entries":[],"count":0})");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, rsp.data(), static_cast<ssize_t>(n));
    return ESP_OK;
}

esp_err_t api_logs_download_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "No logs available", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ws_handler(httpd_req_t* req) {
    diag::FfiGuard guard(83);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket connect");
        int fd = httpd_req_to_sockfd(req);
        if (fd >= 0 && req->user_ctx) {
            auto* server = static_cast<HttpServer*>(req->user_ctx);
            server->addSession(fd);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame{};
    uint8_t buf[256]{};
    frame.payload = buf;
    frame.len = sizeof(buf);
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.len > 0) {
        buf[frame.len] = '\0';
        ESP_LOGI(TAG, "WS RX: %s", reinterpret_cast<char*>(buf));
    }

    return ESP_OK;
}

esp_err_t webui_file_handler(httpd_req_t* req) {
    diag::FfiGuard guard(84);

    std::string_view path = req->uri;
    auto content = interface::webui::getFile(path);
    if (content.empty()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }

    auto pos = path.rfind('.');
    const char* mime = "text/html; charset=utf-8";
    if (pos != std::string_view::npos) {
        auto ext = path.substr(pos + 1);
        if (ext == "css") mime = "text/css; charset=utf-8";
        else if (ext == "js") mime = "application/javascript; charset=utf-8";
    }

    httpd_resp_set_type(req, mime);
    httpd_resp_send(req, content.data(), static_cast<ssize_t>(content.size()));
    return ESP_OK;
}

} // anonymous namespace

void HttpServer::registerRoutes() {
    if (!handle_) return;

    diag::FfiGuard guard(85);

    auto reg = [this](const httpd_uri_t& uri) {
        httpd_register_uri_handler(handle_, &uri);
    };

    // Captive portal
    reg({ .uri = "/wifi", .method = HTTP_GET, .handler = captive_wifi_page_handler });
    reg({ .uri = "/wifi/connect", .method = HTTP_POST, .handler = captive_wifi_connect_handler });
    reg({ .uri = "/wifi/status", .method = HTTP_GET, .handler = captive_wifi_status_handler });

    // Probe redirects
    reg({ .uri = "/generate_204", .method = HTTP_GET, .handler = captive_redirect_handler });
    reg({ .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_redirect_handler });
    reg({ .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_redirect_handler });
    reg({ .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_redirect_handler });
    reg({ .uri = "/redirect", .method = HTTP_GET, .handler = captive_redirect_handler });

    // REST API
    reg({ .uri = "/api/ping", .method = HTTP_GET, .handler = api_ping_handler });
    reg({ .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler });
    reg({ .uri = "/api/command", .method = HTTP_POST, .handler = api_command_handler });
    reg({ .uri = "/api/valve", .method = HTTP_GET, .handler = api_valve_get_handler });
    reg({ .uri = "/api/valve", .method = HTTP_POST, .handler = api_valve_post_handler });
    reg({ .uri = "/api/logs", .method = HTTP_GET, .handler = api_logs_handler });
    reg({ .uri = "/api/logs/download", .method = HTTP_GET, .handler = api_logs_download_handler });

    // WebSocket — pass `this` as user_ctx so ws_handler can track sessions
    reg({
        .uri = "/ws/stream",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = this,
        .is_websocket = true,
    });

    // WebUI static files
    reg({ .uri = "/", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/style.css", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/state.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/ws.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/ui-update.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/logs.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/stepper.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/calibration.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/init.js", .method = HTTP_GET, .handler = webui_file_handler });

    ESP_LOGI(TAG, "All routes registered");
}

void HttpServer::broadcastWsEvent(const char* jsonData, size_t len) {
    if (!handle_) return;

    httpd_ws_frame_t frame{};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(jsonData));
    frame.len = len;

    for (auto& session : sessions_) {
        if (session.fd < 0) continue;

        auto fdInfo = httpd_ws_get_fd_info(handle_, session.fd);
        if (fdInfo != HTTPD_WS_CLIENT_WEBSOCKET) {
            session.fd = WS_FD_INVALID;
            continue;
        }

        esp_err_t err = httpd_ws_send_frame_async(
            handle_, session.fd, &frame);
        if (err != ESP_OK) {
            session.fd = WS_FD_INVALID;
        }
    }
}

WsSession* HttpServer::findSession(int fd) {
    for (auto& s : sessions_) {
        if (s.fd == fd) return &s;
    }
    return nullptr;
}

void HttpServer::addSession(int fd) {
    for (auto& s : sessions_) {
        if (s.fd < 0) {
            s.fd = fd;
            return;
        }
    }
}

void HttpServer::removeSession(int fd) {
    for (auto& s : sessions_) {
        if (s.fd == fd) {
            s.fd = WS_FD_INVALID;
            return;
        }
    }
}

} // namespace ecotiter::infrastructure::network

#pragma GCC diagnostic pop
