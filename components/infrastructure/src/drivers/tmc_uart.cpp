#include "infrastructure/drivers/tmc_uart.hpp"

#include <cstdio>
#include <cstring>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr auto TAG = "tmc_uart";

namespace ecotiter::infrastructure::drivers
{

TmcUart::~TmcUart()
{
    deinit();
}

static constexpr uint32_t TMC_UART_TIMEOUT_MS = 50;

uint8_t TmcUart::computeCrc(const uint8_t* data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x80)
            {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x07);
            }
            else
            {
                crc = static_cast<uint8_t>(crc << 1);
            }
        }
    }
    return crc;
}

bool TmcUart::init(gpio_num_t txPin, gpio_num_t rxPin, uint32_t baud)
{
    if (initialized_)
        deinit();

    txPin_ = txPin;
    rxPin_ = rxPin;
    uartNum_ = 2;

    uart_config_t uartConfig = {
        .baud_rate = static_cast<int>(baud),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {.allow_pd = 0, .backup_before_sleep = 0},
    };

    esp_err_t err = uart_driver_install(static_cast<uart_port_t>(uartNum_), 256, 0, 0, nullptr, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_param_config(static_cast<uart_port_t>(uartNum_), &uartConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(static_cast<uart_port_t>(uartNum_));
        return false;
    }

    err = uart_set_pin(static_cast<uart_port_t>(uartNum_), txPin, rxPin, -1, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(static_cast<uart_port_t>(uartNum_));
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    initialized_ = true;
    ESP_LOGI(TAG, "TMC UART initialized on UART_NUM_2 (%d TX, %d RX)", txPin, rxPin);
    return true;
}

void TmcUart::deinit()
{
    if (initialized_)
    {
        uart_driver_delete(static_cast<uart_port_t>(uartNum_));
        initialized_ = false;
    }
}

bool TmcUart::writeRegister(uint8_t reg, uint32_t value) const
{
    if (!initialized_)
        return false;

    uint8_t buf[8];
    buf[0] = 0x05;
    buf[1] = reg;
    buf[2] = static_cast<uint8_t>((value >> 24) & 0xFF);
    buf[3] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[4] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[5] = static_cast<uint8_t>(value & 0xFF);
    uint8_t crc = computeCrc(buf, 6);
    buf[6] = crc;
    buf[7] = crc;

    int written = uart_write_bytes(static_cast<uart_port_t>(uartNum_), buf, 8);
    if (written != 8)
    {
        ESP_LOGE(TAG, "writeRegister(0x%02X) wrote %d bytes", reg, written);
        return false;
    }
    esp_err_t err =
        uart_wait_tx_done(static_cast<uart_port_t>(uartNum_), pdMS_TO_TICKS(TMC_UART_TIMEOUT_MS));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "writeRegister(0x%02X) TX wait failed", reg);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    return true;
}

namespace
{

bool sendReadRequest(uart_port_t uartNum, uint8_t reg)
{
    uint8_t req[3];
    req[0] = 0x07;
    req[1] = reg;
    req[2] = TmcUart::computeCrc(req, 2);

    int written = uart_write_bytes(uartNum, req, 3);
    if (written != 3)
    {
        ESP_LOGE(TAG, "readRegister(0x%02X) wrote %d bytes", reg, written);
        return false;
    }

    esp_err_t err = uart_wait_tx_done(uartNum, pdMS_TO_TICKS(TMC_UART_TIMEOUT_MS));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "readRegister(0x%02X) TX wait failed", reg);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
    return true;
}

bool receiveResponse(uart_port_t uartNum, uint8_t reg, uint8_t* resp, size_t respSize)
{
    int n = uart_read_bytes(uartNum, resp, static_cast<int>(respSize),
                            pdMS_TO_TICKS(TMC_UART_TIMEOUT_MS));
    if (n < static_cast<int>(respSize))
    {
        ESP_LOGW(TAG, "readRegister(0x%02X) read %d bytes (expected %zu)", reg, n, respSize);
        return false;
    }
    return true;
}

bool validateResponse(uint8_t reg, const uint8_t* resp, uint32_t& value)
{
    if (resp[0] != 0x05 || resp[1] != reg)
    {
        ESP_LOGW(TAG, "readRegister(0x%02X) bad header: 0x%02X 0x%02X", reg, resp[0], resp[1]);
        return false;
    }

    uint8_t expectedCrc = TmcUart::computeCrc(resp, 6);
    uint16_t receivedCrc = static_cast<uint16_t>(resp[6]) | (static_cast<uint16_t>(resp[7]) << 8);
    if (static_cast<uint16_t>(expectedCrc) != (receivedCrc & 0xFF))
    {
        ESP_LOGW(TAG, "readRegister(0x%02X) CRC mismatch", reg);
        return false;
    }

    value = (static_cast<uint32_t>(resp[2]) << 24) | (static_cast<uint32_t>(resp[3]) << 16) |
            (static_cast<uint32_t>(resp[4]) << 8) | (static_cast<uint32_t>(resp[5]));
    return true;
}

} // anonymous namespace

bool TmcUart::readRegister(uint8_t reg, uint32_t& value) const
{
    if (!initialized_)
        return false;

    auto uartNum = static_cast<uart_port_t>(uartNum_);

    if (!sendReadRequest(uartNum, reg))
        return false;

    uint8_t resp[8];
    if (!receiveResponse(uartNum, reg, resp, sizeof(resp)))
        return false;

    return validateResponse(reg, resp, value);
}

bool TmcUart::testConnection() const
{
    uint32_t ioin = 0;
    if (!readRegister(TMC_REG_IOIN, ioin))
    {
        ESP_LOGE(TAG, "testConnection: failed to read IOIN");
        return false;
    }
    // IOIN should be non-zero for a connected TMC2209
    bool ok = (ioin != 0 && ioin != 0xFFFFFFFF);
    ESP_LOGI(TAG, "testConnection: IOIN=0x%08lX %s", static_cast<unsigned long>(ioin),
             ok ? "OK" : "FAIL");
    return ok;
}

} // namespace ecotiter::infrastructure::drivers
