#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"

namespace ecotiter::infrastructure::drivers
{

// TMC2209 single-wire UART communication (half-duplex via PDN_UART).
// TX and RX pins must be externally connected through a 1 kΩ resistor.
class TmcUart
{
public:
    TmcUart() = default;
    ~TmcUart();

    TmcUart(const TmcUart&) = delete;
    TmcUart& operator=(const TmcUart&) = delete;

    [[nodiscard]] bool init(gpio_num_t txPin, gpio_num_t rxPin, uint32_t baud);
    void deinit();

    [[nodiscard]] bool writeRegister(uint8_t reg, uint32_t value) const;
    [[nodiscard]] bool readRegister(uint8_t reg, uint32_t& value) const;
    [[nodiscard]] bool testConnection() const;

    // Pure CRC utility — public so helper functions in readRegister can use it
    static uint8_t computeCrc(const uint8_t* data, size_t len);

private:

    gpio_num_t txPin_{GPIO_NUM_NC};
    gpio_num_t rxPin_{GPIO_NUM_NC};
    int uartNum_{-1};
    bool initialized_{false};
};

// TMC2209 register addresses
inline constexpr uint8_t TMC_REG_GCONF = 0x00;
inline constexpr uint8_t TMC_REG_IOIN = 0x08;
inline constexpr uint8_t TMC_REG_TCOOLTHRS = 0x14;
inline constexpr uint8_t TMC_REG_SGTHRS = 0x40;
inline constexpr uint8_t TMC_REG_SG_RESULT = 0x41;
inline constexpr uint8_t TMC_REG_COOLCONF = 0x42;
inline constexpr uint8_t TMC_REG_CHOPCONF = 0x6C;
inline constexpr uint8_t TMC_REG_DRV_STATUS = 0x6F;
inline constexpr uint8_t TMC_REG_PWMCONF = 0x70;

} // namespace ecotiter::infrastructure::drivers
