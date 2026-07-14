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

RmtEncoder::RmtEncoder() {
    rmt_copy_encoder_config_t encConfig = {};
    esp_err_t err = rmt_new_copy_encoder(&encConfig, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create copy encoder: %s", esp_err_to_name(err));
        handle_ = nullptr;
    }
}

RmtEncoder::~RmtEncoder() {
    if (handle_) {
        rmt_del_encoder(handle_);
    }
}

RmtEncoder::RmtEncoder(RmtEncoder&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
}

RmtEncoder& RmtEncoder::operator=(RmtEncoder&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            rmt_del_encoder(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

StepperMotor::StepperMotor(gpio_num_t stepPin, gpio_num_t enPin)
    : channel_(stepPin)
    , enPin_(enPin) {

    puts("DBG: StepperMotor ctor"); fflush(stdout);

    if (!encoder_.valid()) {
        ESP_LOGE(TAG, "Failed to create copy encoder");
    }
}

StepperMotor::~StepperMotor() {
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

        uint32_t symbols[config::RMT_CHUNK_SYMBOLS];
        for (size_t i = 0; i < chunkSize; ++i) {
            uint32_t interval = intervals[offset + i];
            uint32_t pulseUs = 5;
            if (interval <= pulseUs * 2) {
                pulseUs = interval / 2;
            }
            rmt_symbol_word_t sym{};
            sym.duration0 = static_cast<uint16_t>(pulseUs);
            sym.level0 = 1;
            sym.duration1 = static_cast<uint16_t>(interval - pulseUs);
            sym.level1 = 0;
            symbols[i] = sym.val;
        }

        esp_err_t err = rmt_transmit(
            channel_.get(),
            encoder_.get(),
            symbols,
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
