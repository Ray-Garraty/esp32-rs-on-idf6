#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <span>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"

#include "domain/errors.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"

namespace ecotiter::infrastructure::drivers {

// RAII wrapper for RMT TX channel
class RmtChannel {
public:
    explicit RmtChannel(gpio_num_t stepPin);
    ~RmtChannel();

    RmtChannel(const RmtChannel&) = delete;
    RmtChannel& operator=(const RmtChannel&) = delete;
    RmtChannel(RmtChannel&&) noexcept;
    RmtChannel& operator=(RmtChannel&&) noexcept;

    [[nodiscard]] rmt_channel_handle_t get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }

private:
    rmt_channel_handle_t handle_ = nullptr;
};

// RAII wrapper for RMT copy encoder
class RmtEncoder {
public:
    explicit RmtEncoder();
    ~RmtEncoder();

    RmtEncoder(const RmtEncoder&) = delete;
    RmtEncoder& operator=(const RmtEncoder&) = delete;
    RmtEncoder(RmtEncoder&&) noexcept;
    RmtEncoder& operator=(RmtEncoder&&) noexcept;

    [[nodiscard]] rmt_encoder_handle_t get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }

private:
    rmt_encoder_handle_t handle_ = nullptr;
};

// Concrete stepper motor driver (TMC2209 via RMT)
class StepperMotor {
public:
    StepperMotor(gpio_num_t stepPin, gpio_num_t enPin);
    ~StepperMotor();

    StepperMotor(const StepperMotor&) = delete;
    StepperMotor& operator=(const StepperMotor&) = delete;

    // GR-2: stop_flag is MANDATORY
    [[nodiscard]] domain::Result<void, domain::StepperError> moveStepsIntervals(
        std::span<const uint32_t> intervals,
        std::atomic<bool>* stopFlag = nullptr);

    [[nodiscard]] domain::Result<void, domain::StepperError> emergencyStop();
    [[nodiscard]] domain::Result<void, domain::StepperError> enable();
    [[nodiscard]] domain::Result<void, domain::StepperError> disable();

    [[nodiscard]] domain::Steps position() const noexcept {
        return domain::Steps{position_.load(std::memory_order_acquire)};
    }

    void setCurrentPosition(domain::Steps pos) noexcept {
        position_.store(pos.value, std::memory_order_release);
    }

private:
    RmtChannel channel_;
    RmtEncoder encoder_;
    gpio_num_t enPin_;
    std::atomic<int32_t> position_{0};
};

} // namespace ecotiter::infrastructure::drivers
