#include "infrastructure/drivers/rgb_led.hpp"

#include "infrastructure/config.hpp"
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static constexpr auto TAG = "rgb_led";

namespace ecotiter::infrastructure::drivers
{

RmtTxChannel::RmtTxChannel(gpio_num_t pin, uint32_t resolution_hz)
{
    rmt_tx_channel_config_t tx_chan_cfg = {};
    tx_chan_cfg.gpio_num = pin;
    tx_chan_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_chan_cfg.resolution_hz = resolution_hz;
    tx_chan_cfg.mem_block_symbols = 64;
    tx_chan_cfg.trans_queue_depth = 1;
    esp_err_t err = rmt_new_tx_channel(&tx_chan_cfg, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        handle = nullptr;
    }
}

RmtTxChannel::~RmtTxChannel()
{
    if (handle != nullptr)
    {
        rmt_del_channel(handle);
    }
}

RmtCopyEncoder::RmtCopyEncoder()
{
    rmt_copy_encoder_config_t enc_cfg = {};
    esp_err_t err = rmt_new_copy_encoder(&enc_cfg, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create copy encoder: %s", esp_err_to_name(err));
        handle = nullptr;
    }
}

RmtCopyEncoder::~RmtCopyEncoder()
{
    if (handle != nullptr)
    {
        rmt_del_encoder(handle);
    }
}

RgbLed::RgbLed(gpio_num_t pin)
    : pin_(pin),
      channel_(pin, config::LED_RMT_RES_HZ),
      encoder_()
{
    if (channel_.handle == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create RMT TX channel on GPIO %d", pin_);
        return;
    }
    if (encoder_.handle == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create copy encoder");
        return;
    }
    esp_err_t err = rmt_enable(channel_.handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        return;
    }
    initialized_ = true;
    ESP_LOGD(TAG, "RgbLed initialized on GPIO %d", pin_);
}

RgbLed::~RgbLed()
{
    if (initialized_ && channel_.handle != nullptr)
    {
        rmt_disable(channel_.handle);
    }
}

void RgbLed::setColor(uint8_t r, uint8_t g, uint8_t b)
{
    r_ = r;
    g_ = g;
    b_ = b;
}

void RgbLed::refresh()
{
    if (!initialized_ || channel_.handle == nullptr || encoder_.handle == nullptr)
    {
        return;
    }

    // WS2812/SK68XX protocol: 24 bits per LED in GRB order
    // Bit encoding @ 10 MHz (0.1 us/tick):
    //   0: high 0.3 us (3 ticks), low 0.9 us (9 ticks)
    //   1: high 0.7 us (7 ticks), low 0.5 us (5 ticks)
    // Reset: >50 us low (appended as extra symbol)

    rmt_symbol_word_t symbols[25];
    std::memset(symbols, 0, sizeof(symbols));

    // WS2812 uses GRB byte order
    uint8_t bytes[3] = {g_, r_, b_};
    size_t idx = 0;

    constexpr uint16_t T0H = 3;
    constexpr uint16_t T0L = 9;
    constexpr uint16_t T1H = 7;
    constexpr uint16_t T1L = 5;

    for (int byte = 0; byte < 3; ++byte)
    {
        for (int bit = 7; bit >= 0; --bit)
        {
            if (bytes[byte] & (1u << bit))
            {
                symbols[idx].duration0 = T1H;
                symbols[idx].level0 = 1;
                symbols[idx].duration1 = T1L;
                symbols[idx].level1 = 0;
            }
            else
            {
                symbols[idx].duration0 = T0H;
                symbols[idx].level0 = 1;
                symbols[idx].duration1 = T0L;
                symbols[idx].level1 = 0;
            }
            ++idx;
        }
    }

    // Reset symbol: >50 us low
    symbols[idx].duration0 = 0;
    symbols[idx].level0 = 0;
    symbols[idx].duration1 = 600; // 60 us
    symbols[idx].level1 = 0;
    ++idx;

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    tx_cfg.flags.eot_level = 0;

    esp_err_t err = rmt_transmit(channel_.handle, encoder_.handle, symbols,
                                 idx * sizeof(rmt_symbol_word_t), &tx_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        return;
    }
}

void RgbLed::setTransportMode(domain::TransportMode mode, bool error)
{
    if (error)
    {
        setColor(color::RED_R, color::RED_G, color::RED_B);
    }
    else
    {
        switch (mode)
        {
        case domain::TransportMode::UsbActive:
            setColor(color::OFF_R, color::OFF_G, color::OFF_B);
            break;
        case domain::TransportMode::BleAdvertising:
            setColor(color::BLUE_R, color::BLUE_G, color::BLUE_B);
            break;
        case domain::TransportMode::BleConnected:
            setColor(color::GREEN_R, color::GREEN_G, color::GREEN_B);
            break;
        }
    }
    refresh();
}

} // namespace ecotiter::infrastructure::drivers
