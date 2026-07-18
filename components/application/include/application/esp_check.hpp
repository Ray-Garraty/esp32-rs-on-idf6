#pragma once

#include <cstdio>

#include "esp_err.h"
#include "esp_log.h"

#include "domain/errors.hpp"

#ifndef ESP_RETURN_UNEXPECTED
/// Helper macro: evaluate an esp_err_t expression, log if not ESP_OK,
/// and return std::unexpected(domain::AppError::Resource).
/// Always logs the error before returning — silent error swallowing
/// is forbidden by coding_style.md §12.
#define ESP_RETURN_UNEXPECTED(expr, tag)                                                   \
    do {                                                                                   \
        esp_err_t _esp_err_ = (expr);                                                      \
        if (_esp_err_ != ESP_OK) {                                                         \
            ESP_LOGE((tag), "%s:%d: %s", __FILE__, __LINE__, esp_err_to_name(_esp_err_));  \
            return std::unexpected(domain::AppError::Resource);                            \
        }                                                                                  \
    } while (0)
#endif
