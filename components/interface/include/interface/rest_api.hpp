#pragma once

#include <expected>
#include <string_view>

#include "domain/errors.hpp"
#include "domain/memory.hpp"

namespace ecotiter::interface
{

// ---------------------------------------------------------------------------
// Core logic — no ESP-IDF dependency, host-testable
// ---------------------------------------------------------------------------

[[nodiscard]] std::expected<size_t, int> handlePingCore(domain::memory::ResponseBuffer& buf);

[[nodiscard]] std::expected<size_t, int> handleStatusCore(domain::memory::ResponseBuffer& buf);

[[nodiscard]] std::expected<size_t, int> handleCommandCore(std::string_view body,
                                                           domain::memory::ResponseBuffer& buf);

} // namespace ecotiter::interface

// ---------------------------------------------------------------------------
// ESP-IDF HTTP handlers — only compiled on-target (ESP_PLATFORM defined by IDF)
// ---------------------------------------------------------------------------
#ifdef ESP_PLATFORM
#include "esp_http_server.h"

namespace ecotiter::interface
{

esp_err_t ping_handler(httpd_req_t* req);
esp_err_t status_handler(httpd_req_t* req);
esp_err_t command_handler(httpd_req_t* req);
esp_err_t valve_get_handler(httpd_req_t* req);
esp_err_t valve_post_handler(httpd_req_t* req);

} // namespace ecotiter::interface
#endif // ESP_PLATFORM
