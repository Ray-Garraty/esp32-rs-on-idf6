#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"

#include "domain/types.hpp"

namespace ecotiter::infrastructure::drivers {

struct RmtTxChannel {
    rmt_channel_handle_t handle{nullptr};
    explicit RmtTxChannel(gpio_num_t pin, uint32_t resolution_hz);
    ~RmtTxChannel();
    RmtTxChannel(const RmtTxChannel&) = delete;
    RmtTxChannel& operator=(const RmtTxChannel&) = delete;
    RmtTxChannel(RmtTxChannel&&) = delete;
    RmtTxChannel& operator=(RmtTxChannel&&) = delete;
};

struct RmtCopyEncoder {
    rmt_encoder_handle_t handle{nullptr};
    RmtCopyEncoder();
    ~RmtCopyEncoder();
    RmtCopyEncoder(const RmtCopyEncoder&) = delete;
    RmtCopyEncoder& operator=(const RmtCopyEncoder&) = delete;
    RmtCopyEncoder(RmtCopyEncoder&&) = delete;
    RmtCopyEncoder& operator=(RmtCopyEncoder&&) = delete;
};

class RgbLed {
public:
    explicit RgbLed(gpio_num_t pin);
    ~RgbLed();

    RgbLed(const RgbLed&) = delete;
    RgbLed& operator=(const RgbLed&) = delete;
    RgbLed(RgbLed&&) = delete;
    RgbLed& operator=(RgbLed&&) = delete;

    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void refresh();

    void setTransportMode(domain::TransportMode mode, bool error = false);

private:
    gpio_num_t pin_;
    uint8_t r_{0};
    uint8_t g_{0};
    uint8_t b_{0};
    RmtTxChannel channel_;
    RmtCopyEncoder encoder_;
    bool initialized_{false};
};

namespace color {
    inline constexpr uint8_t RED_R = 255, RED_G = 0, RED_B = 0;
    inline constexpr uint8_t OFF_R = 0, OFF_G = 0, OFF_B = 0;
    inline constexpr uint8_t BLUE_R = 0, BLUE_G = 0, BLUE_B = 255;
    inline constexpr uint8_t GREEN_R = 0, GREEN_G = 255, GREEN_B = 0;
}

} // namespace ecotiter::infrastructure::drivers
