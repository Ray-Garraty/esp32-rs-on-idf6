#include "interface/rest_api.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "application/response.hpp"
#include "domain/types.hpp"
#include "nlohmann/json.hpp"

namespace ecotiter::interface {

// ---------------------------------------------------------------------------
// Core logic helpers
// ---------------------------------------------------------------------------

std::expected<size_t, int> handlePingCore(
    domain::memory::ResponseBuffer& buf) {
    int n = std::snprintf(buf.data(), buf.size(), R"({"status":"ok"})");
    if (n < 0) return std::unexpected(500);
    return static_cast<size_t>(n);
}

std::expected<size_t, int> handleStatusCore(
    domain::memory::ResponseBuffer& buf) {
    auto state = domain::gBuretteState.load(std::memory_order_acquire);
    bool volumeIsNull = (state == domain::BuretteState::Homing);
    size_t offset = 0;
    application::serializeStatusJson(
        buf, offset,
        state,
        domain::gTempCX100.load(std::memory_order_acquire),
        domain::gValvePosition.load(std::memory_order_acquire),
        static_cast<float>(domain::gLastMv.load(std::memory_order_acquire)),
        domain::gDirection.load(std::memory_order_acquire),
        domain::gSpeed.load(std::memory_order_acquire),
        domain::gAccel.load(std::memory_order_acquire),
        domain::gVolumeMl.load(std::memory_order_acquire),
        volumeIsNull);
    if (offset == 0) return std::unexpected(500);
    return offset;
}

std::expected<size_t, int> handleCommandCore(
    std::string_view body,
    domain::memory::ResponseBuffer& buf) {

    auto cmd = application::parseCommand(body);
    if (!cmd) {
        int n = std::snprintf(buf.data(), buf.size(),
            R"({"status":"error","message":"invalid_params"})");
        if (n < 0) return std::unexpected(500);
        return std::unexpected(400);
    }

    auto rsp = application::dispatch(*cmd);
    if (!rsp) {
        int n = std::snprintf(buf.data(), buf.size(),
            R"({"status":"error","message":"start_failed"})");
        if (n < 0) return std::unexpected(500);
        return std::unexpected(500);
    }

    auto serialized = application::serializeToBuffer(*rsp, buf);
    if (!serialized) {
        return std::unexpected(500);
    }
    return *serialized;
}

std::expected<size_t, int> handleValveGetCore(
    domain::memory::ResponseBuffer& buf) {
    auto pos = domain::gValvePosition.load(std::memory_order_acquire);
    const char* s = (pos == domain::ValvePosition::Input) ? "input" : "output";
    int n = std::snprintf(buf.data(), buf.size(),
        R"({"valve":"%s"})", s);
    if (n < 0) return std::unexpected(500);
    return static_cast<size_t>(n);
}

std::expected<size_t, int> handleValvePostCore(
    std::string_view body,
    domain::memory::ResponseBuffer& buf) {
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded()) {
        int n = std::snprintf(buf.data(), buf.size(),
            R"({"error":"invalid_json"})");
        if (n < 0) return std::unexpected(500);
        return std::unexpected(400);
    }

    auto it = j.find("position");
    if (it == j.end() || !it->is_string()) {
        int n = std::snprintf(buf.data(), buf.size(),
            R"({"error":"missing_position"})");
        if (n < 0) return std::unexpected(500);
        return std::unexpected(400);
    }

    std::string posStr = it->get<std::string>();
    domain::ValvePosition pos;
    if (posStr == "input") {
        pos = domain::ValvePosition::Input;
    } else if (posStr == "output") {
        pos = domain::ValvePosition::Output;
    } else {
        int n = std::snprintf(buf.data(), buf.size(),
            R"({"error":"invalid_position"})");
        if (n < 0) return std::unexpected(500);
        return std::unexpected(400);
    }

    domain::gValvePosition.store(pos, std::memory_order_release);

    int n = std::snprintf(buf.data(), buf.size(),
        R"({"valve":"%s"})", posStr.c_str());
    if (n < 0) return std::unexpected(500);
    return static_cast<size_t>(n);
}

} // namespace ecotiter::interface

// ---------------------------------------------------------------------------
// ESP-IDF HTTP handler wrappers — on-target only
// ---------------------------------------------------------------------------
#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "infrastructure/motor_task.hpp"

static constexpr auto TAG = "rest_api";

esp_err_t ecotiter::interface::ping_handler(httpd_req_t* req) {
    domain::memory::ResponseBuffer buf{};
    auto result = handlePingCore(buf);
    if (!result) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal error");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf.data(), static_cast<ssize_t>(*result));
    return ESP_OK;
}

esp_err_t ecotiter::interface::status_handler(httpd_req_t* req) {
    domain::memory::ResponseBuffer buf{};
    auto result = handleStatusCore(buf);
    if (!result) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal error");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf.data(), static_cast<ssize_t>(*result));
    return ESP_OK;
}

esp_err_t ecotiter::interface::command_handler(httpd_req_t* req) {
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

    auto sv = std::string_view(body.data(), bodyLen);
    auto cmd = application::parseCommand(sv);
    if (!cmd) {
        const char* detail = "Protocol error";
        domain::memory::ResponseBuffer buf{};
        std::snprintf(buf.data(), buf.size(),
            R"({"status":"error","message":"%s"})", detail);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, buf.data(), HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    auto rsp = application::dispatch(*cmd);
    if (!rsp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "dispatch failed");
        return ESP_FAIL;
    }

    // For synchronous commands (Single, Error), return immediately
    if (rsp->kind != application::ResponseKind::AckThen) {
        domain::memory::ResponseBuffer rspBuf{};
        auto serialized = application::serializeToBuffer(*rsp, rspBuf);
        if (!serialized) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialize failed");
            return ESP_FAIL;
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, rspBuf.data(), static_cast<ssize_t>(*serialized));
        return ESP_OK;
    }

    // AckThen: wait for result via queue
    static constexpr TickType_t CMD_TIMEOUT_TICKS = pdMS_TO_TICKS(60000);
    TickType_t startTick = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (now - startTick >= CMD_TIMEOUT_TICKS) {
            domain::memory::ResponseBuffer buf{};
            std::snprintf(buf.data(), buf.size(),
                R"({"status":"error","message":"watchdog_timeout"})");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_status(req, "408 Request Timeout");
            httpd_resp_send(req, buf.data(), HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        infrastructure::SmResult result;
        if (infrastructure::gSmResultQueue &&
            xQueueReceive(infrastructure::gSmResultQueue, &result, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            domain::memory::ResponseBuffer buf{};
            size_t off = application::formatSmResult(buf, rsp->id, result);

            httpd_resp_set_type(req, "application/json");
            if (off > 0 && off < buf.size()) {
                httpd_resp_send(req, buf.data(), static_cast<ssize_t>(off));
            } else {
                httpd_resp_send(req, R"({"status":"error"})", HTTPD_RESP_USE_STRLEN);
            }
            return ESP_OK;
        }
    }
}

esp_err_t ecotiter::interface::valve_get_handler(httpd_req_t* req) {
    domain::memory::ResponseBuffer buf{};
    auto result = handleValveGetCore(buf);
    if (!result) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "internal error");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf.data(), static_cast<ssize_t>(*result));
    return ESP_OK;
}

esp_err_t ecotiter::interface::valve_post_handler(httpd_req_t* req) {
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

    domain::memory::ResponseBuffer rspBuf{};
    auto result = handleValvePostCore(
        std::string_view(body.data(), bodyLen), rspBuf);
    if (!result) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, rspBuf.data(),
            static_cast<ssize_t>(std::strlen(rspBuf.data())));
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, rspBuf.data(), static_cast<ssize_t>(*result));
    return ESP_OK;
}

#endif // ESP_PLATFORM
