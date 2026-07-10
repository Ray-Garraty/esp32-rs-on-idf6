#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

#include "nvs_flash.h"

#include "domain/calibration.hpp"
#include "domain/errors.hpp"

namespace ecotiter::infrastructure::storage {

class NvsHandle {
public:
    NvsHandle(const char* ns, bool readWrite);
    ~NvsHandle();

    NvsHandle(const NvsHandle&) = delete;
    NvsHandle& operator=(const NvsHandle&) = delete;
    NvsHandle(NvsHandle&& other) noexcept;
    NvsHandle& operator=(NvsHandle&& other) noexcept;

    [[nodiscard]] bool isValid() const noexcept { return open_; }

    [[nodiscard]] domain::Result<std::optional<uint8_t>, domain::ResourceError> getU8(const char* key);
    [[nodiscard]] domain::Result<void, domain::ResourceError> setU8(const char* key, uint8_t value);
    [[nodiscard]] domain::Result<std::optional<uint32_t>, domain::ResourceError> getU32(const char* key);
    [[nodiscard]] domain::Result<void, domain::ResourceError> setU32(const char* key, uint32_t value);
    [[nodiscard]] domain::Result<std::optional<int32_t>, domain::ResourceError> getI32(const char* key);
    [[nodiscard]] domain::Result<void, domain::ResourceError> setI32(const char* key, int32_t value);
    [[nodiscard]] domain::Result<std::optional<float>, domain::ResourceError> getF32(const char* key);
    [[nodiscard]] domain::Result<void, domain::ResourceError> setF32(const char* key, float value);
    [[nodiscard]] domain::Result<std::optional<std::string_view>, domain::ResourceError> getStr(
        const char* key, char* buf, size_t bufSize);
    [[nodiscard]] domain::Result<void, domain::ResourceError> setStr(const char* key, const char* value);
    [[nodiscard]] domain::Result<void, domain::ResourceError> eraseKey(const char* key);
    [[nodiscard]] domain::Result<void, domain::ResourceError> eraseAll();

private:
    nvs_handle_t handle_{0};
    bool open_{false};
};

[[nodiscard]] uint8_t stallguardReadThreshold();
[[nodiscard]] domain::Result<void, domain::ResourceError> stallguardWriteThreshold(uint8_t value);
[[nodiscard]] domain::Result<domain::CalibrationData, domain::ResourceError> calibrationRead();
[[nodiscard]] domain::Result<void, domain::ResourceError> calibrationWrite(const domain::CalibrationData& cal);

// ADC calibration — persisted as a_x1000 (uint16_t) and b (int16_t)
void adcCalibrationRead(uint16_t& aX1000, int16_t& b);
[[nodiscard]] domain::Result<void, domain::ResourceError> adcCalibrationWrite(uint16_t aX1000, int16_t b);

template <size_t N>
[[nodiscard]] domain::Result<std::optional<std::string_view>, domain::ResourceError> wifiReadStr(
    const char* key, char (&buf)[N]) {
    auto nvs = NvsHandle("wifi", true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return nvs.getStr(key, buf, N);
}

[[nodiscard]] domain::Result<void, domain::ResourceError> wifiWriteStr(const char* key, const char* value);
[[nodiscard]] domain::Result<void, domain::ResourceError> wifiErase(const char* key);

} // namespace ecotiter::infrastructure::storage
