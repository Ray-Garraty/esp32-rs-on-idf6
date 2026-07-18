#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

#include "driver/gpio.h"

namespace ecotiter::infrastructure::drivers
{

inline std::atomic<int32_t> gTempCX100{std::numeric_limits<int32_t>::min()};

class OneWireBus
{
public:
    explicit OneWireBus(gpio_num_t pin);
    ~OneWireBus() = default;

    OneWireBus(const OneWireBus&) = delete;
    OneWireBus& operator=(const OneWireBus&) = delete;

    [[nodiscard]] bool reset();
    void writeByte(uint8_t byte);
    uint8_t readByte();
    void skipRom();
    void convertT();
    std::array<uint8_t, 9> readScratchpad();

private:
    void writeBit1();
    void writeBit0();
    bool readBit();

    gpio_num_t pin_;
};

std::optional<float> readSensor(OneWireBus& bus);

} // namespace ecotiter::infrastructure::drivers
