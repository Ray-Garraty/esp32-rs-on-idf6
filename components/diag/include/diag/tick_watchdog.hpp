#pragma once

#include <cstdint>
#include "diag/black_box.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static constexpr auto TICK_TAG = "tick_watchdog";

namespace ecotiter::diag {

class TickWatchdog {
public:
    inline TickWatchdog() noexcept
        : startUs_(static_cast<uint64_t>(esp_timer_get_time())) {}

    inline ~TickWatchdog() noexcept {
        auto elapsed = static_cast<uint64_t>(esp_timer_get_time()) - startUs_;
        if (elapsed > 10000) {
            ESP_LOGW(TICK_TAG, "main loop took %llu us (>10ms threshold)",
                     static_cast<unsigned long long>(elapsed));
        }
        // Periodically record to BlackBox (every 100th tick)
        static uint32_t s_counter = 0;
        if (++s_counter % 100 == 0) {
            BlackBox::Event ev = {};
            ev.type = BlackBox::EventType::TickDuration;
            ev.payloadId = static_cast<uint16_t>(elapsed / 1000);
            ev.payloadValue = static_cast<uint32_t>(elapsed);
            BlackBox::instance().record(ev);
        }
    }

    TickWatchdog(const TickWatchdog&) = delete;
    TickWatchdog& operator=(const TickWatchdog&) = delete;

private:
    uint64_t startUs_;
};

} // namespace ecotiter::diag
