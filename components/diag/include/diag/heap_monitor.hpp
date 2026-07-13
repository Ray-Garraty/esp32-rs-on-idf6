#pragma once

#include <cstdio>
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace ecotiter::diag {

inline void print_heap_stats() {
    auto psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    auto psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    auto dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    auto dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    ESP_LOGI("heap", "PSRAM free=%lu largest=%lu | DRAM free=%lu largest=%lu",
             (unsigned long)psram_free, (unsigned long)psram_largest,
             (unsigned long)dram_free, (unsigned long)dram_largest);

    // Warning thresholds per memory_spec.md §7.2
    if (psram_free < 4 * 1024 * 1024) {
        ESP_LOGW("heap", "CRITICAL: PSRAM free < 4 MB (%lu)", (unsigned long)psram_free);
    }
    if (psram_largest < 256 * 1024) {
        ESP_LOGW("heap", "CRITICAL: PSRAM largest block < 256 KB (%lu)", (unsigned long)psram_largest);
    }
    if (dram_free < 20 * 1024) {
        ESP_LOGW("heap", "CRITICAL: DRAM free < 20 KB (%lu)", (unsigned long)dram_free);
    }
    if (dram_largest < 4 * 1024) {
        ESP_LOGW("heap", "CRITICAL: DRAM largest block < 4 KB (%lu)", (unsigned long)dram_largest);
    }
}

} // namespace ecotiter::diag
