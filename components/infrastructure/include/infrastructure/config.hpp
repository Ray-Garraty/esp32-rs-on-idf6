#pragma once

#include <cstdint>
#include "driver/gpio.h"

namespace ecotiter::config {

// GPIO Pinout — from AGENTS.md x3.1
// Onboard WS2812 RGB LED (ESP32-S3-DevKitC-1, GPIO 48)
inline constexpr gpio_num_t PIN_LED_RGB    = GPIO_NUM_48;
inline constexpr uint32_t   LED_RMT_RES_HZ = 10'000'000;  // 0.1 us per tick for WS2812
// Number of RGB LEDs on the strip (1 = onboard LED)
inline constexpr size_t     LED_RGB_COUNT  = 1;
inline constexpr gpio_num_t PIN_ADC_PH     = GPIO_NUM_4;   // ADC1_CH3
inline constexpr gpio_num_t PIN_VALVE      = GPIO_NUM_14;
inline constexpr gpio_num_t PIN_STEP       = GPIO_NUM_21;  // RMT channel
inline constexpr gpio_num_t PIN_DIR        = GPIO_NUM_5;   // GPIO26 is PSRAM CS1 (LL-027)
inline constexpr gpio_num_t PIN_EN         = GPIO_NUM_27;  // Active LOW
inline constexpr gpio_num_t PIN_DS18B20    = GPIO_NUM_6;   // GPIO33 is PSRAM D4 (LL-027)
inline constexpr gpio_num_t PIN_LIMIT_FULL = GPIO_NUM_7;   // GPIO34 is PSRAM D5 (LL-027)
inline constexpr gpio_num_t PIN_LIMIT_EMPTY = GPIO_NUM_15;  // GPIO35 is PSRAM D6 (LL-027)

// RMT config
inline constexpr uint32_t RMT_RESOLUTION_HZ = 1'000'000;  // 1 tick = 1 us
inline constexpr uint32_t RMT_CHUNK_SYMBOLS = 128;
inline constexpr uint32_t RMT_MAX_SYMBOLS   = 192;  // S3 shared RAM = 384 words

// Stepper kinematics
inline constexpr uint32_t STEP_FREQ_MIN_HZ = 30;
inline constexpr uint32_t STEP_FREQ_MAX_HZ = 3000;


// Network
inline constexpr const char* AP_SSID       = "EcoTiter-AP";
inline constexpr uint32_t STA_RECONNECT_MS = 10'000;

// Memory map (ESP32-S3) — for is_sane_sp() in panic handler
inline constexpr uintptr_t DRAM_START        = 0x3FC8'8000;
inline constexpr uintptr_t DRAM_END          = 0x3FF0'0000;
inline constexpr uintptr_t IRAM_START        = 0x4037'0000;
inline constexpr uintptr_t IRAM_END          = 0x403E'0000;
inline constexpr uintptr_t FLASH_CACHE_START = 0x4200'0000;
inline constexpr uintptr_t FLASH_CACHE_END   = 0x43FF'FFFF;
inline constexpr uintptr_t UART0_BASE        = 0x6000'0000;

// ADC config
inline constexpr uint32_t ADC_SAMPLES           = 64;
inline constexpr uint16_t ADC_DEFAULT_A_X1000    = 1000;
inline constexpr int16_t  ADC_DEFAULT_B          = 0;

// OneWire config
inline constexpr uint32_t TEMP_CONVERSION_WAIT_MS = 800;

// NVS namespaces
inline constexpr const char* NVS_NS_STALLGUARD    = "stallguard";
inline constexpr const char* NVS_NS_WIFI          = "wifi";
inline constexpr const char* NVS_NS_BURETTE_CAL   = "burette_cal";
inline constexpr const char* NVS_KEY_SG_THRESHOLD  = "threshold";
inline constexpr const char* NVS_KEY_WIFI_SSID    = "ssid";
inline constexpr const char* NVS_KEY_WIFI_PASS    = "password";
inline constexpr const char* NVS_KEY_CAL_SPM      = "steps_per_ml";
inline constexpr const char* NVS_KEY_CAL_NOM      = "nominal_vol";
inline constexpr const char* NVS_KEY_CAL_COEFF    = "speed_coeff";
inline constexpr const char* NVS_KEY_CAL_MIN_FREQ = "min_freq";
inline constexpr const char* NVS_KEY_CAL_MAX_FREQ = "max_freq";
inline constexpr const char* NVS_KEY_CAL_DATE     = "cal_date";

// ADC calibration NVS
inline constexpr const char* NVS_NS_ADC_CAL       = "adc_cal";
inline constexpr const char* NVS_KEY_ADC_A_X1000  = "a_x1000";
inline constexpr const char* NVS_KEY_ADC_B        = "b";

} // namespace ecotiter::config
