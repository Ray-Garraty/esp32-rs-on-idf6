#include "infrastructure/storage/nvs.hpp"
#include "esp_log.h"

#include <bit>
#include <cstring>

static constexpr auto TAG = "nvs";

namespace ecotiter::infrastructure::storage {

NvsHandle::NvsHandle(const char* ns, bool readWrite) {
    esp_err_t err = nvs_open(ns,
                             readWrite ? NVS_READWRITE : NVS_READONLY,
                             &handle_);
    if (err == ESP_OK) {
        open_ = true;
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s",
                 ns, esp_err_to_name(err));
    }
}

NvsHandle::~NvsHandle() {
    if (open_) {
        nvs_close(handle_);
    }
}

NvsHandle::NvsHandle(NvsHandle&& other) noexcept
    : handle_(other.handle_)
    , open_(other.open_) {
    other.handle_ = 0;
    other.open_ = false;
}

NvsHandle& NvsHandle::operator=(NvsHandle&& other) noexcept {
    if (this != &other) {
        if (open_) {
            nvs_close(handle_);
        }
        handle_ = other.handle_;
        open_ = other.open_;
        other.handle_ = 0;
        other.open_ = false;
    }
    return *this;
}

domain::Result<std::optional<uint8_t>, domain::ResourceError> NvsHandle::getU8(const char* key) {
    uint8_t value = 0;
    esp_err_t err = nvs_get_u8(handle_, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<uint8_t>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<uint8_t>(value);
}

domain::Result<void, domain::ResourceError> NvsHandle::setU8(const char* key, uint8_t value) {
    esp_err_t err = nvs_set_u8(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<uint32_t>, domain::ResourceError> NvsHandle::getU32(const char* key) {
    uint32_t value = 0;
    esp_err_t err = nvs_get_u32(handle_, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<uint32_t>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<uint32_t>(value);
}

domain::Result<void, domain::ResourceError> NvsHandle::setU32(const char* key, uint32_t value) {
    esp_err_t err = nvs_set_u32(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<int32_t>, domain::ResourceError> NvsHandle::getI32(const char* key) {
    int32_t value = 0;
    esp_err_t err = nvs_get_i32(handle_, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<int32_t>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<int32_t>(value);
}

domain::Result<void, domain::ResourceError> NvsHandle::setI32(const char* key, int32_t value) {
    esp_err_t err = nvs_set_i32(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<float>, domain::ResourceError> NvsHandle::getF32(const char* key) {
    uint32_t bits = 0;
    esp_err_t err = nvs_get_u32(handle_, key, &bits);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<float>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<float>(std::bit_cast<float>(bits));
}

domain::Result<void, domain::ResourceError> NvsHandle::setF32(const char* key, float value) {
    uint32_t bits = std::bit_cast<uint32_t>(value);
    esp_err_t err = nvs_set_u32(handle_, key, bits);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<std::string_view>, domain::ResourceError> NvsHandle::getStr(
    const char* key, char* buf, size_t bufSize) {
    size_t len = bufSize;
    esp_err_t err = nvs_get_str(handle_, key, buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<std::string_view>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    // len includes null terminator; strip it for string_view
    if (len > 0) len -= 1;
    return std::optional<std::string_view>(std::string_view(buf, len));
}

domain::Result<void, domain::ResourceError> NvsHandle::setStr(const char* key, const char* value) {
    esp_err_t err = nvs_set_str(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<void, domain::ResourceError> NvsHandle::eraseKey(const char* key) {
    esp_err_t err = nvs_erase_key(handle_, key);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<void, domain::ResourceError> NvsHandle::eraseAll() {
    esp_err_t err = nvs_erase_all(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

uint8_t stallguardReadThreshold() {
    auto nvs = NvsHandle("stallguard", false);
    if (!nvs.isValid()) return 0;
    auto result = nvs.getU32("threshold");
    if (!result || !result->has_value()) return 0;
    return static_cast<uint8_t>(result->value() & 0xFF);
}

domain::Result<void, domain::ResourceError> stallguardWriteThreshold(uint8_t value) {
    auto nvs = NvsHandle("stallguard", true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return nvs.setU32("threshold", static_cast<uint32_t>(value));
}

domain::Result<void, domain::ResourceError> wifiWriteStr(const char* key, const char* value) {
    auto nvs = NvsHandle("wifi", true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return nvs.setStr(key, value);
}

domain::Result<void, domain::ResourceError> wifiErase(const char* key) {
    auto nvs = NvsHandle("wifi", true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return nvs.eraseKey(key);
}

} // namespace ecotiter::infrastructure::storage
