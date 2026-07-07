#include "interface/serial.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_vfs_dev.h"

static constexpr auto TAG = "serial";

namespace ecotiter::interface {

SerialReader::~SerialReader() {
    if (fd_ >= 0) {
        uart_driver_delete(UART_NUM_0);
        fd_ = -1;
    }
}

Result<void> SerialReader::init() noexcept {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {.allow_pd = 0, .backup_before_sleep = 0},
    };

    esp_err_t err = uart_driver_install(
        UART_NUM_0,
        INPUT_BUF_SIZE,
        0,
        10,
        nullptr,
        0
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return std::unexpected(SerialError::InitFailed);
    }

    err = uart_param_config(UART_NUM_0, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(UART_NUM_0);
        return std::unexpected(SerialError::InitFailed);
    }

    err = uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(UART_NUM_0);
        return std::unexpected(SerialError::InitFailed);
    }

    uart_vfs_dev_register();
    uart_vfs_dev_use_driver(UART_NUM_0);

    fd_ = open("/dev/uart/0", O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        ESP_LOGE(TAG, "open /dev/uart/0 failed");
        uart_driver_delete(UART_NUM_0);
        return std::unexpected(SerialError::InitFailed);
    }

    ESP_LOGI(TAG, "SerialReader initialized, fd=%d", fd_);
    return {};
}

std::optional<std::string_view> SerialReader::process() noexcept {
    fd_set readFds{};
    FD_ZERO(&readFds);
    FD_SET(fd_, &readFds);

    struct timeval timeout = {0, 0};
    int ret = select(fd_ + 1, &readFds, nullptr, nullptr, &timeout);
    if (ret <= 0) {
        return std::nullopt;
    }

    if (!FD_ISSET(fd_, &readFds)) {
        return std::nullopt;
    }

    std::array<char, INPUT_BUF_SIZE> readBuf{};
    ssize_t n = ::read(fd_, readBuf.data(), readBuf.size());
    if (n <= 0) {
        return std::nullopt;
    }

    return splitBuffer(std::string_view(readBuf.data(), static_cast<size_t>(n)));
}

void SerialReader::write(std::string_view s) noexcept {
    if (fd_ < 0 || s.empty()) {
        return;
    }
    ssize_t written = ::write(fd_, s.data(), s.size());
    if (written < 0) {
        ESP_LOGE(TAG, "write failed: errno=%d", errno);
    }
}

} // namespace ecotiter::interface
