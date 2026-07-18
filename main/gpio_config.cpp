#include "gpio_config.hpp"

#include "driver/gpio.h"
#include "esp_err.h"

#include "infrastructure/config.hpp"

void configureGpioPins()
{
    using namespace ecotiter;
    // DIR pin (GPIO5) — safe
    gpio_set_direction(config::PIN_DIR, GPIO_MODE_OUTPUT);
    gpio_set_level(config::PIN_DIR, 0);

    // EN pin (GPIO13) — safe (moved from GPIO27: LL-027 PSRAM D3)
    gpio_set_direction(config::PIN_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(config::PIN_EN, 0); // Active LOW: enable driver

    // FULL endstop (GPIO7) — input with pull-down, pos-edge interrupt
    gpio_config_t fullConf = {};
    fullConf.pin_bit_mask = (1ULL << config::PIN_LIMIT_FULL);
    fullConf.mode = GPIO_MODE_INPUT;
    fullConf.pull_up_en = GPIO_PULLUP_DISABLE;
    fullConf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    fullConf.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&fullConf));

    // EMPTY endstop (GPIO15) — input, floating, pos-edge interrupt
    gpio_config_t emptyConf = {};
    emptyConf.pin_bit_mask = (1ULL << config::PIN_LIMIT_EMPTY);
    emptyConf.mode = GPIO_MODE_INPUT;
    emptyConf.pull_up_en = GPIO_PULLUP_DISABLE;
    emptyConf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    emptyConf.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&emptyConf));

    // DS18B20 (GPIO6) — open-drain with pull-up for OneWire bitbang
    gpio_config_t dsConf = {};
    dsConf.pin_bit_mask = (1ULL << config::PIN_DS18B20);
    dsConf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    dsConf.pull_up_en = GPIO_PULLUP_ENABLE;
    dsConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    dsConf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&dsConf));

    // Install ISR service once (for all limit switch pins)
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
}