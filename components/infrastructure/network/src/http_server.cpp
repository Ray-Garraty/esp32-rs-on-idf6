#include "infrastructure/network/http_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"

#include "interface/rest_api.hpp"
#include "interface/webui.hpp"
#include "application/scheduler.hpp"
#include "domain/types.hpp"
#include "domain/memory.hpp"
#include "domain/json_utils.hpp"
#include "infrastructure/memory/psram_buffer.hpp"
#include "domain/log_buffer.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/heap_snapshot.hpp"
#include "infrastructure/network/wifi.hpp"
#include "infrastructure/storage/nvs.hpp"
#include "infrastructure/config.hpp"

#pragma GCC diagnostic push
// HTTPD_DEFAULT_CONFIG() uses designated initializers that leave some fields
// at default — this is safe because we override the ones we need (stack_size,
// lru_purge_enable, max_open_sockets, max_uri_handlers) after the macro.
// CONTRACT: this suppression must be removed if ESP-IDF ever provides a
// fully-specified default macro.
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

std::expected<void, domain::AppError> HttpServer::init() { // NOLINT(readability-function-cognitive-complexity) // reason: httpd config + URI handler registration
    if (handle_) return {};

    diag::FfiGuard guard(80);

    // Suppress httpd noise — captive portal generates many "invalid requests"
    // (as recommended in the official ESP-IDF captive portal example)
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

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
    config.lru_purge_enable = true;
    // max_open_sockets=13 as in official ESP-IDF captive portal example.
    // HTTPD_MAX_SOCKETS = max(CONFIG_LWIP_MAX_SOCKETS, 16) → 13+3=16 ≤ 16 ✓
    config.max_open_sockets = 13;
    config.max_uri_handlers = 24;

    if (!diag::HeapSnapshot::assertCanAllocate(config.stack_size + 4096)) {
        ESP_LOGW(TAG, "Low DRAM before httpd_start");
    }
    esp_err_t err = httpd_start(&handle_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "httpd config: stack_size=%u, max_uri_handlers=%u, max_open_sockets=%d, lru_purge=%d",
                 config.stack_size, config.max_uri_handlers, config.max_open_sockets, config.lru_purge_enable);
        return std::unexpected(domain::AppError::Resource);
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return {};
}

namespace {

esp_err_t serve_wifi_page(httpd_req_t* req) {
    auto sv = interface::webui::getFile("/wifi");
    if (sv.empty()) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, sv.data(), static_cast<ssize_t>(sv.size()));
    return ESP_OK;
}

esp_err_t captive_wifi_page_handler(httpd_req_t* req) {
    diag::FfiGuard guard(81);
    ESP_LOGI(TAG, "Serving /wifi: %zu bytes",
             interface::webui::getFile("/wifi").size());
    return serve_wifi_page(req);
}

// NOLINTBEGIN(cppcoreguidelines-owning-memory) // reason: ESP-IDF httpd C API returns raw pointers
esp_err_t captive_wifi_connect_handler(httpd_req_t* req) { // NOLINT(readability-function-cognitive-complexity) // reason: captive portal WiFi connect + JSON parse
    diag::FfiGuard guard(81);

    auto* server = static_cast<HttpServer*>(req->user_ctx);
    if (!server || !server->wifiManager()) {
        ESP_LOGE(TAG, "No WifiManager available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no wifi mgr");
        return ESP_FAIL;
    }

    domain::memory::CommandBuffer body{};
    size_t bodyLen = std::min(
        static_cast<size_t>(req->content_len),
        body.size() - 1);  // reserve 1 byte for null terminator
    int ret = httpd_req_recv(req, body.data(), bodyLen);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
        return ESP_FAIL;
    }
    bodyLen = static_cast<size_t>(ret);

    // Null-terminate and parse as simple JSON
    body[bodyLen] = '\0';
    auto* bodyStr = reinterpret_cast<const char*>(body.data());
    ESP_LOGI(TAG, "WiFi connect request: content_len=%d, received=%zu bytes, body=%s",
             req->content_len, bodyLen, bodyStr);

    // Parse SSID and password using domain-layer JSON utility
    auto* ssid = domain::findJsonField(bodyStr, "\"ssid\"");
    auto* password = domain::findJsonField(bodyStr, "\"password\"");

    if (!ssid || !password) {
        free(ssid);
        free(password);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, R"({"success":false,"message":"Missing ssid or password"})",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Attempting WiFi connect to SSID: %s", ssid);

    // Try to connect
    auto* wifi = server->wifiManager();
    bool connected = wifi->connectSTA(ssid, password, 15000);

    if (connected) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
            R"({"success":true,"message":"Connected. Restarting..."})",
            HTTPD_RESP_USE_STRLEN);

        ESP_LOGI(TAG, "WiFi connected, restarting in STA mode");
        // Small delay so the response is sent before restart
        vTaskDelay(pdMS_TO_TICKS(500));
        free(ssid);
        free(password);
        esp_restart();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
        R"({"success":false,"message":"Connection failed. Check SSID and password."})",
        HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "WiFi connect failed for: %s", ssid);
    free(ssid);
    free(password);
    return ESP_OK;
}
// NOLINTEND(cppcoreguidelines-owning-memory) // reason: END httpd C API suppressions

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

static void ipToStr(uint32_t ip, char* out, size_t outSize) {
    uint8_t* b = reinterpret_cast<uint8_t*>(&ip);
    std::snprintf(out, outSize, "%u.%u.%u.%u",
        static_cast<unsigned>(b[0]), static_cast<unsigned>(b[1]),
        static_cast<unsigned>(b[2]), static_cast<unsigned>(b[3]));
}

esp_err_t api_status_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);

    auto state = domain::gBuretteState.load(std::memory_order_acquire);
    int32_t tempCX100 = domain::gTempCX100.load(std::memory_order_acquire);
    uint16_t mv = domain::gLastMv.load(std::memory_order_acquire);
    auto valvePos = domain::gValvePosition.load(std::memory_order_acquire);
    float volumeMl = domain::gVolumeMl.load(std::memory_order_acquire);
    float speed = domain::gSpeedMlMin.load(std::memory_order_acquire);
    uint32_t tick = application::gTick.load(std::memory_order_acquire);

    const char* stateStr = (state == domain::BuretteState::Idle) ? "idle"
                       : (state == domain::BuretteState::Error) ? "error"
                       : "working";

    const char* valveStr = (valvePos == domain::ValvePosition::Input) ? "input" : "output";

    bool tempConnected = (tempCX100 > -99999);
    float tempC = tempConnected
        ? static_cast<float>(tempCX100) / 100.0f
        : 0.0f;

    char ipBuf[16] = "192.168.4.1";
    auto* server = static_cast<HttpServer*>(req->user_ctx);
    if (server && server->wifiManager()) {
        auto staIp = server->wifiManager()->getSTAIP();
        if (staIp) {
            ipToStr(*staIp, ipBuf, sizeof(ipBuf));
        }
    }

    ecotiter::memory::PsramBuffer<domain::memory::MAX_RSP_SIZE> buf{};
    int n = std::snprintf(reinterpret_cast<char*>(buf.data()), buf.size(),
        R"({"ts":%lu,"meta":{"ip":"%s"},)"
        R"("sensors":{"temperature":{"is_connected":%s,"celsius_val":%.1f},"electrode":{"mv":%u}},)"
        R"("valve":{"position":"%s"},)"
        R"("burette":{"status":"%s","volume_ml":%.2f,"speed_ml_min":%.2f})"
        R"(})",
        static_cast<unsigned long>(tick),
        ipBuf,
        tempConnected ? "true" : "false",
        static_cast<double>(tempC),
        static_cast<unsigned>(mv),
        valveStr,
        stateStr,
        static_cast<double>(volumeMl),
        static_cast<double>(speed));

    httpd_resp_set_type(req, "application/json");
    if (n > 0) {
        httpd_resp_send(req, reinterpret_cast<const char*>(buf.data()), static_cast<ssize_t>(n));
    } else {
        httpd_resp_send(req, "{}", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
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

    // Parse query params
    int limit = config::LOG_FETCH_DEFAULT_LIMIT;
    char levelFilter[16]{};
    char qbuf[128];
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(qbuf, "limit", val, sizeof(val)) == ESP_OK) {
            int parsed = std::atoi(val);
            if (parsed > 0 && parsed <= config::LOG_FETCH_MAX_LIMIT) limit = parsed;
        }
        if (httpd_query_key_value(qbuf, "level", val, sizeof(val)) == ESP_OK) {
            std::strncpy(levelFilter, val, sizeof(levelFilter) - 1);
        }
    }

    // Build JSON response
    ecotiter::memory::PsramBuffer<domain::memory::MAX_RSP_SIZE> rsp{};
    size_t pos = 0;
    auto append = [&](const char* s) {
        size_t len = std::strlen(s);
        if (pos + len < rsp.size()) {
            std::memcpy(reinterpret_cast<char*>(rsp.data()) + pos, s, len);
            pos += len;
        }
    };
    auto appendCh = [&](char c) {
        if (pos + 1 < rsp.size()) { reinterpret_cast<char*>(rsp.data())[pos++] = c; }
    };

    append(R"({"entries":)");
    {
        domain::LogEntry entries[50];
        size_t count = domain::LogBuffer::instance().fetch(
            entries, static_cast<size_t>(limit),
            levelFilter[0] ? levelFilter : nullptr);

        appendCh('[');
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) appendCh(',');
            appendCh('{');
            append(R"("level":")");
            append(entries[i].level);
            append(R"(","msg":")");
            // message — escape JSON special chars
            for (const char* p = entries[i].message; *p; ++p) {
                if (*p == '\\') { append(R"(\\)"); }
                else if (*p == '"') { append(R"(\")"); }
                else if (*p == '\n') { append(R"(\n)"); }
                else if (*p == '\r') { append(R"(\r)"); }
                else if (*p == '\t') { append(R"(\t)"); }
                else { appendCh(*p); }
            }
            appendCh('"');
            appendCh('}');
        }
        appendCh(']');

        append(R"(,"count":)");
        {
            char num[16];
            int n = std::snprintf(num, sizeof(num), "%zu", count);
            if (n > 0) append(num);
        }
    }
    appendCh('}');

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, reinterpret_cast<const char*>(rsp.data()), static_cast<ssize_t>(pos));
    return ESP_OK;
}

esp_err_t api_nvs_status_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);

    bool buretteCalExists = false;
    bool adcCalExists = false;

    {
        auto nvs = storage::NvsHandle(config::NVS_NS_BURETTE_CAL, false);
        if (nvs.isValid()) {
            auto r = nvs.getF32(config::NVS_KEY_CAL_SPM);
            buretteCalExists = (r && r->has_value());
        }
    }
    {
        auto nvs = storage::NvsHandle(config::NVS_NS_ADC_CAL, false);
        if (nvs.isValid()) {
            uint16_t aX1000{};
            int16_t b{};
            storage::adcCalibrationRead(aX1000, b);
            adcCalExists = (aX1000 != 0 || b != 0);
        }
    }

    domain::memory::ResponseBuffer rsp{};
    int n = std::snprintf(rsp.data(), rsp.size(),
        R"({"burette_cal":%s,"adc_cal":%s})",
        buretteCalExists ? "true" : "false",
        adcCalExists ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, rsp.data(), static_cast<ssize_t>(n));
    return ESP_OK;
}

esp_err_t api_logs_download_handler(httpd_req_t* req) {
    diag::FfiGuard guard(82);

    domain::LogEntry entries[20];
    size_t count = domain::LogBuffer::instance().fetch(entries, 20);

    ecotiter::memory::PsramBuffer<domain::memory::MAX_RSP_SIZE> rsp{};
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t remaining = rsp.size() - pos;
        if (remaining < 4) break;
        int n = std::snprintf(reinterpret_cast<char*>(rsp.data()) + pos, remaining,
            "[%s] %s\n", entries[i].level, entries[i].message);
        if (n > 0) {
            size_t written = std::min(static_cast<size_t>(n), remaining - 1);
            pos += written;
        }
        if (pos >= rsp.size()) break;
    }
    if (pos == 0) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "No logs available", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, reinterpret_cast<const char*>(rsp.data()), static_cast<ssize_t>(pos));
    return ESP_OK;
}

esp_err_t ws_handler(httpd_req_t* req) { // NOLINT(readability-function-cognitive-complexity) // reason: WebSocket frame parser + command dispatch
    diag::FfiGuard guard(83);

    // ESP-IDF handles the HTTP_GET WebSocket upgrade internally
    // (httpd_uri.c:362-363 — "do not call the uri->handler"),
    // so ws_handler HTTP_GET branch may never be reached.
    // Track the session unconditionally on the first data frame instead.
    //
    // For HTTP_GET (if it ever reaches here): add session and return OK.
    // For data frames: add session if not yet tracked, then process.

    int fd = httpd_req_to_sockfd(req);

    if (fd >= 0 && req->user_ctx) {
        auto* server = static_cast<HttpServer*>(req->user_ctx);
        if (!server->findSession(fd)) {
            server->addSession(fd);
            ESP_LOGI(TAG, "WS session added for fd=%d", fd);
        }
    }

    if (req->method == HTTP_GET) {
        return ESP_OK;
    }

    httpd_ws_frame_t frame{};
    uint8_t buf[1024]{};
    frame.payload = buf;
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, sizeof(buf));
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_SIZE) {
            ESP_LOGW(TAG, "ws_handler: frame too large (%u bytes, max %zu)",
                     frame.len, sizeof(buf));
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "ws_handler: recv failed (fd=%d, err=%d)", fd, err);
        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        if (fd >= 0 && req->user_ctx) {
            auto* server = static_cast<HttpServer*>(req->user_ctx);
            server->removeSession(fd);
        }
        return ESP_OK;
    }

    if (frame.len > 0) {
        size_t safeLen = std::min<size_t>(frame.len, sizeof(buf) - 1);
        buf[safeLen] = '\0';
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

esp_err_t root_handler(httpd_req_t* req) { // NOLINT(readability-function-cognitive-complexity) // reason: static file serving with fallback chain
    diag::FfiGuard guard(81);

    ESP_LOGI(TAG, "Root handler called for /");
    auto* server = static_cast<HttpServer*>(req->user_ctx);
    bool connected = server && server->wifiManager() && server->wifiManager()->isConnected();
    ESP_LOGI(TAG, "Root handler: server=%p, wifiManager=%p, isConnected=%d",
             (void*)server, server ? (void*)server->wifiManager() : nullptr, connected);
    if (server && server->wifiManager() && !connected) {
        // AP-only mode: redirect to captive portal page
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/wifi");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Root: redirecting to /wifi (AP-only mode)");
        return ESP_OK;
    }

    // STA connected: serve the normal dashboard
    ESP_LOGI(TAG, "Root handler: serving dashboard");
    return webui_file_handler(req);
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
    reg({ .uri = "/wifi/connect", .method = HTTP_POST,
          .handler = captive_wifi_connect_handler,
          .user_ctx = this });
    reg({ .uri = "/wifi/status", .method = HTTP_GET, .handler = captive_wifi_status_handler });

    // REST API
    reg({ .uri = "/api/ping", .method = HTTP_GET, .handler = api_ping_handler });
    reg({ .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler });
    reg({ .uri = "/api/command", .method = HTTP_POST, .handler = api_command_handler });
    reg({ .uri = "/api/valve", .method = HTTP_GET, .handler = api_valve_get_handler });
    reg({ .uri = "/api/valve", .method = HTTP_POST, .handler = api_valve_post_handler });
    reg({ .uri = "/api/logs", .method = HTTP_GET, .handler = api_logs_handler });
    reg({ .uri = "/api/logs/download", .method = HTTP_GET, .handler = api_logs_download_handler });

    // NVS status
    reg({ .uri = "/api/nvs/status", .method = HTTP_GET, .handler = api_nvs_status_handler });

    // WebSocket — pass `this` as user_ctx so ws_handler can track sessions
    reg({
        .uri = "/ws/stream",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = this,
        .is_websocket = true,
    });

    // WebUI static files
    reg({ .uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = this });
    reg({ .uri = "/style.css", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/state.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/ws.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/ui-update.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/logs.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/stepper.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/calibration.js", .method = HTTP_GET, .handler = webui_file_handler });
    reg({ .uri = "/js/init.js", .method = HTTP_GET, .handler = webui_file_handler });

    // Catch-all 404 handler redirects unknown URLs to captive portal page.
    // Redirect to /wifi (self-contained HTML) rather than / (dashboard with CDN deps)
    // to avoid infinite redirect loops from secondary resource requests.
    // iOS requires content in the response to detect a captive portal
    // (simply redirecting is not sufficient — official ESP-IDF note).
    httpd_register_err_handler(handle_, HTTPD_404_NOT_FOUND,
        [](httpd_req_t* req, httpd_err_code_t err) -> esp_err_t {
            httpd_resp_set_status(req, "303 See Other");
            httpd_resp_set_hdr(req, "Location", "/wifi");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
            httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);
            ESP_LOGI(TAG, "Redirecting unknown URI to /wifi");
            return ESP_OK;
        });

    ESP_LOGI(TAG, "All routes registered");
}

void HttpServer::broadcastWsEvent(const char* jsonData, size_t len) {
    if (!handle_ || !jsonData || len == 0) return;

    bool hasValid = false;
    for (auto& s : sessions_) { if (s.fd >= 0) { hasValid = true; break; } }
    if (!hasValid) { return; }

    httpd_ws_frame_t frame{};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(jsonData));
    frame.len = len;

    int sent = 0;
    int skipped = 0;
    (void)sent;
    (void)skipped;
    for (auto& session : sessions_) {
        if (session.fd < 0) {
            ++skipped;
            continue;
        }

        auto fdInfo = httpd_ws_get_fd_info(handle_, session.fd);
        if (fdInfo == HTTPD_WS_CLIENT_INVALID) {
            session.fd = WS_FD_INVALID;
            ++skipped;
            continue;
        }
        if (fdInfo != HTTPD_WS_CLIENT_WEBSOCKET) {
            ++skipped;
            continue;
        }

        esp_err_t err = httpd_ws_send_frame_async(
            handle_, session.fd, &frame);
        if (err != ESP_OK) {
            auto checkInfo = httpd_ws_get_fd_info(handle_, session.fd);
            if (checkInfo == HTTPD_WS_CLIENT_INVALID) {
                session.fd = WS_FD_INVALID;
            }
        } else {
            ++sent;
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
