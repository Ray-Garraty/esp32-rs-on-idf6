#include "diag/rtc_watchdog.hpp"

#include <cstdio>
#include "esp_log.h"
#include "soc/rtc.h"

static constexpr auto TAG = "rtc_wdt";

// RWDT timeout in seconds. Must be longer than IWDT timeout (500ms)
// but shorter than TWDT timeout (10s). 6s provides a good safety margin.
static constexpr unsigned RWDT_TIMEOUT_S = 6;

// RWDT tick rate = RTC slow clock frequency. On ESP32-S3 default:
//   - Internal RC: ~150 kHz  (RTC_SLOW_CLK_FREQ_150K)
//   - 8MD256:      ~31.25 kHz (RTC_SLOW_CLK_FREQ_8MD256)
// We query the actual frequency at init.
static uint32_t rtc_slow_clk_hz() noexcept
{
    return rtc_clk_slow_freq_get_hz();
}

namespace ecotiter::diag
{

RtcWatchdog* gRtcWdt = nullptr;

RtcWatchdog::RtcWatchdog() noexcept
{
    // Step 1: Initialize HAL context. rwdt_hal_init disables RWDT + all
    // stages + sets defaults. Must be called first to get a clean state.
    wdt_hal_init(&hal_, WDT_RWDT, 0, false);
    // After init, RWDT is write-protected again.

    // Step 2: Unlock registers
    wdt_hal_write_protect_disable(&hal_);

    // Step 3: Disable flashboot mode (bootloader may have set it)
    wdt_hal_set_flashboot_en(&hal_, false);

    // Step 4: Configure stage 0 — OFF (we use stage 1 to avoid the
    // stage-0 implicit multiplier; stage 1 has a clean 1:1 tick mapping).
    wdt_hal_config_stage(&hal_, WDT_STAGE0, 0, WDT_STAGE_ACTION_OFF);

    // Step 5: Configure stage 1 — RESET_SYSTEM at RWDT_TIMEOUT_S.
    // Stage 1 ticks = 1:1 with RTC slow clock (no multiplier).
    auto clk_hz = rtc_slow_clk_hz();
    if (clk_hz == 0)
    {
        ESP_LOGE(TAG, "RTC slow clock freq = 0! Cannot configure RWDT.");
        enabled_ = false;
        wdt_hal_write_protect_enable(&hal_);
        return;
    }
    uint32_t timeout_ticks = RWDT_TIMEOUT_S * clk_hz;
    wdt_hal_config_stage(&hal_, WDT_STAGE1, timeout_ticks, WDT_STAGE_ACTION_RESET_SYSTEM);

    // Step 6: Enable RWDT (also feeds it)
    wdt_hal_enable(&hal_);

    // Step 7: Lock registers
    wdt_hal_write_protect_enable(&hal_);

    enabled_ = true;
    gRtcWdt = this;

    ESP_LOGI(TAG, "RWDT enabled: %u s timeout (%u Hz RTC slow clk, %u ticks)", RWDT_TIMEOUT_S,
             clk_hz, timeout_ticks);

    char dbg[80];
    std::snprintf(dbg, sizeof(dbg), "DBG: RWDT enabled, %us timeout", RWDT_TIMEOUT_S);
    puts(dbg);
    fflush(stdout);
}

RtcWatchdog::~RtcWatchdog() noexcept
{
    if (enabled_)
    {
        wdt_hal_deinit(&hal_);
        enabled_ = false;
        gRtcWdt = nullptr;
        ESP_LOGI(TAG, "RWDT disabled");
    }
}

void RtcWatchdog::feed() noexcept
{
    if (!enabled_)
    {
        return;
    }
    // RWDT registers are write-protected. Must unlock → feed → relock.
    // This is ~3 register writes (a few CPU cycles).
    wdt_hal_write_protect_disable(&hal_);
    wdt_hal_feed(&hal_);
    wdt_hal_write_protect_enable(&hal_);
}

} // namespace ecotiter::diag
