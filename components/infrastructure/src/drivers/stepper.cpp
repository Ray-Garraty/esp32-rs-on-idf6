#include "infrastructure/drivers/stepper.hpp"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static constexpr auto TAG = "stepper";

namespace ecotiter::infrastructure::drivers {

RmtChannel::RmtChannel(gpio_num_t stepPin) {
    puts("DBG: RmtChannel ctor"); fflush(stdout);
    rmt_tx_channel_config_t txConfig = {
        .gpio_num = stepPin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = config::RMT_RESOLUTION_HZ,
        .mem_block_symbols = config::RMT_MAX_SYMBOLS,
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {
            .invert_out = false,
            .with_dma = false,
            .allow_pd = false,
            .init_level = 0,
        }
    };

    esp_err_t err = rmt_new_tx_channel(&txConfig, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT channel: %s", esp_err_to_name(err));
        handle_ = nullptr;
        return;
    }

    err = rmt_enable(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        rmt_del_channel(handle_);
        handle_ = nullptr;
    }
}

RmtChannel::~RmtChannel() {
    if (handle_) {
        rmt_disable(handle_);
        rmt_del_channel(handle_);
    }
}

RmtChannel::RmtChannel(RmtChannel&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
}

RmtChannel& RmtChannel::operator=(RmtChannel&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            rmt_disable(handle_);
            rmt_del_channel(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

StepperMotor::StepperMotor(gpio_num_t stepPin, gpio_num_t dirPin, gpio_num_t enPin)
    : channel_(stepPin)
    , dirPin_(dirPin)
    , enPin_(enPin) {

    puts("DBG: StepperMotor ctor"); fflush(stdout);

    puts("DBG: StepperMotor ctor A"); fflush(stdout);
    gpio_set_level(enPin_, 0); // Active LOW: enable driver
    puts("DBG: StepperMotor ctor B"); fflush(stdout);
    gpio_set_direction(dirPin_, GPIO_MODE_OUTPUT);
    puts("DBG: StepperMotor ctor C"); fflush(stdout);
    // LL-027: EN pin (GPIO27) is on PSRAM data bus — gpio_set_direction() would
    // hang (IOMUX conflict with MSPI). gpio_set_level() is safe because it only
    // writes the output register without touching the IOMUX.
    // gpio_set_direction(enPin_, GPIO_MODE_OUTPUT);  // HANGS — PSRAM bus pin
    puts("DBG: StepperMotor ctor D1 (EN gpio_set_direction skipped — PSRAM pin)"); fflush(stdout);
    puts("DBG: StepperMotor ctor D2"); fflush(stdout);

    rmt_copy_encoder_config_t encConfig = {};
    puts("DBG: StepperMotor ctor E"); fflush(stdout);
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&encConfig, &encoder_));
    puts("DBG: StepperMotor ctor F"); fflush(stdout);
}

StepperMotor::~StepperMotor() {
    if (encoder_) {
        rmt_del_encoder(encoder_);
    }
}

domain::Result<void, domain::StepperError> StepperMotor::moveStepsIntervals(
    std::span<const uint32_t> intervals,
    std::atomic<bool>* stopFlag) {

    rmt_transmit_config_t txConfig = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .queue_nonblocking = false,
        }
    };

    size_t offset = 0;
    while (offset < intervals.size()) {
        if (stopFlag != nullptr &&
            stopFlag->load(std::memory_order_acquire)) {
            std::ignore = emergencyStop();
            return std::unexpected(domain::StepperError::LimitSwitchTriggered);
        }

        size_t chunkSize = intervals.size() - offset;
        if (chunkSize > config::RMT_CHUNK_SYMBOLS) {
            chunkSize = config::RMT_CHUNK_SYMBOLS;
        }

        esp_err_t err = rmt_transmit(
            channel_.get(),
            encoder_,
            &intervals[offset],
            chunkSize * sizeof(uint32_t),
            &txConfig);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(err));
            return std::unexpected(domain::StepperError::Rmt);
        }

        err = rmt_tx_wait_all_done(channel_.get(), portMAX_DELAY);
        if (err != ESP_OK) {
            return std::unexpected(domain::StepperError::Rmt);
        }

        position_.fetch_add(static_cast<int32_t>(chunkSize),
                            std::memory_order_acq_rel);
        offset += chunkSize;
    }

    return {};
}

domain::Result<void, domain::StepperError> StepperMotor::emergencyStop() {
    gpio_set_level(enPin_, 1); // Active LOW: disable driver
    rmt_tx_wait_all_done(channel_.get(), portMAX_DELAY);
    return {};
}

domain::Result<void, domain::StepperError> StepperMotor::enable() {
    gpio_set_level(enPin_, 0); // Active LOW: enable
    return {};
}

domain::Result<void, domain::StepperError> StepperMotor::disable() {
    gpio_set_level(enPin_, 1); // Active LOW: disable
    return {};
}

} // namespace ecotiter::infrastructure::drivers
