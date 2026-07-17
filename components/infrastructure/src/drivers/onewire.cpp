#include "infrastructure/drivers/onewire.hpp"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr auto TAG = "onewire";

namespace ecotiter::infrastructure::drivers {

OneWireBus::OneWireBus(gpio_num_t pin)
    : pin_(pin) {
    gpio_set_level(pin_, 1);
}

// CONTRACT: OneWire bitbang timing via esp_rom_delay_us. All delays are
// derived from the DS18B20 datasheet and are tolerant of 1-10 us variance.
// No blocking calls other than the busy-wait delay_us spinloops.

bool OneWireBus::reset() {
    gpio_set_level(pin_, 0);
    esp_rom_delay_us(480);
    gpio_set_level(pin_, 1);
    esp_rom_delay_us(75);
    bool present = gpio_get_level(pin_) == 0;
    esp_rom_delay_us(405);
    return present;
}

void OneWireBus::writeByte(uint8_t byte) {
    for (int i = 0; i < 8; ++i) {
        if ((byte >> i) & 1) {
            writeBit1();
        } else {
            writeBit0();
        }
    }
}

void OneWireBus::writeBit1() {
    gpio_set_level(pin_, 0);
    esp_rom_delay_us(6);
    gpio_set_level(pin_, 1);
    esp_rom_delay_us(64);
}

void OneWireBus::writeBit0() {
    gpio_set_level(pin_, 0);
    esp_rom_delay_us(60);
    gpio_set_level(pin_, 1);
    esp_rom_delay_us(10);
}

uint8_t OneWireBus::readByte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; ++i) {
        if (readBit()) {
            byte |= static_cast<uint8_t>(1 << i);
        }
    }
    return byte;
}

bool OneWireBus::readBit() {
    gpio_set_level(pin_, 0);
    esp_rom_delay_us(3);
    gpio_set_level(pin_, 1);
    esp_rom_delay_us(10);
    bool bit = gpio_get_level(pin_) != 0;
    esp_rom_delay_us(53);
    return bit;
}

void OneWireBus::skipRom() {
    writeByte(0xCC);
}

void OneWireBus::convertT() {
    writeByte(0x44);
}

std::array<uint8_t, 9> OneWireBus::readScratchpad() {
    writeByte(0xBE);
    std::array<uint8_t, 9> buf{};
    for (auto& b : buf) {
        b = readByte();
    }
    return buf;
}

std::optional<float> readSensor(OneWireBus& bus) { // NOLINT(readability-function-cognitive-complexity) // reason: DS18B20 protocol: reset -> convert -> read scratchpad
    if (!bus.reset()) {
        ESP_LOGD(TAG, "DS18B20 not detected (no presence pulse)");
        return std::nullopt;
    }

    bus.skipRom();
    bus.convertT();

    vTaskDelay(pdMS_TO_TICKS(800));

    if (!bus.reset()) {
        ESP_LOGW(TAG, "DS18B20 lost during conversion");
        return std::nullopt;
    }

    bus.skipRom();
    auto buf = bus.readScratchpad();

    uint16_t raw = static_cast<uint16_t>(buf[1]) << 8 | buf[0];
    int16_t tempRaw = static_cast<int16_t>(raw);
    float temp = static_cast<float>(tempRaw) / 16.0f;

    if (temp < -55.0f || temp > 125.0f) {
        ESP_LOGW(TAG, "DS18B20 out of range: %.2f C", static_cast<double>(temp));
        return std::nullopt;
    }

    return temp;
}

} // namespace ecotiter::infrastructure::drivers
