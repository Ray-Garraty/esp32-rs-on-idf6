#include "infrastructure/storage/nvs.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/adc.hpp"
#include "domain/calibration.hpp"
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

domain::Result<std::optional<uint8_t>, domain::ResourceError> NvsHandle::getU8(const char* key) const {
    uint8_t value = 0;
    esp_err_t err = nvs_get_u8(handle_, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<uint8_t>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<uint8_t>(value);
}

domain::Result<void, domain::ResourceError> NvsHandle::setU8(const char* key, uint8_t value) const {
    esp_err_t err = nvs_set_u8(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<uint32_t>, domain::ResourceError> NvsHandle::getU32(const char* key) const {
    uint32_t value = 0;
    esp_err_t err = nvs_get_u32(handle_, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<uint32_t>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<uint32_t>(value);
}

domain::Result<void, domain::ResourceError> NvsHandle::setU32(const char* key, uint32_t value) const {
    esp_err_t err = nvs_set_u32(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<int32_t>, domain::ResourceError> NvsHandle::getI32(const char* key) const {
    int32_t value = 0;
    esp_err_t err = nvs_get_i32(handle_, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<int32_t>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<int32_t>(value);
}

domain::Result<void, domain::ResourceError> NvsHandle::setI32(const char* key, int32_t value) const {
    esp_err_t err = nvs_set_i32(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<float>, domain::ResourceError> NvsHandle::getF32(const char* key) const {
    uint32_t bits = 0;
    esp_err_t err = nvs_get_u32(handle_, key, &bits);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<float>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return std::optional<float>(std::bit_cast<float>(bits));
}

domain::Result<void, domain::ResourceError> NvsHandle::setF32(const char* key, float value) const {
    uint32_t bits = std::bit_cast<uint32_t>(value);
    esp_err_t err = nvs_set_u32(handle_, key, bits);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<std::optional<std::string_view>, domain::ResourceError> NvsHandle::getStr(
    const char* key, char* buf, size_t bufSize) const {
    size_t len = bufSize;
    esp_err_t err = nvs_get_str(handle_, key, buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) return std::optional<std::string_view>{};
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    // len includes null terminator; strip it for string_view
    if (len > 0) len -= 1;
    return std::optional<std::string_view>(std::string_view(buf, len));
}

domain::Result<void, domain::ResourceError> NvsHandle::setStr(const char* key, const char* value) const {
    esp_err_t err = nvs_set_str(handle_, key, value);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<void, domain::ResourceError> NvsHandle::eraseKey(const char* key) const {
    esp_err_t err = nvs_erase_key(handle_, key);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

domain::Result<void, domain::ResourceError> NvsHandle::eraseAll() const {
    esp_err_t err = nvs_erase_all(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    err = nvs_commit(handle_);
    if (err != ESP_OK) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return {};
}

void nvsInit() {
    // Create calibration namespaces if they don't exist (NVS_READWRITE auto-creates)
    {
        auto nvs = NvsHandle(config::NVS_NS_BURETTE_CAL, true);
        if (nvs.isValid()) {
            // Check if any key exists — if not, this is a fresh namespace
            auto r = nvs.getF32(config::NVS_KEY_CAL_SPM);
            if (r && !r->has_value()) {
                // First boot — write defaults
                auto cal = domain::CalibrationData{};
                std::ignore = nvs.setF32(config::NVS_KEY_CAL_SPM, cal.stepsPerMl);
                std::ignore = nvs.setF32(config::NVS_KEY_CAL_NOM, cal.nominalVolumeMl);
                std::ignore = nvs.setF32(config::NVS_KEY_CAL_COEFF, cal.speedCoeff);
                std::ignore = nvs.setU32(config::NVS_KEY_CAL_MIN_FREQ, static_cast<uint32_t>(cal.minFreqHz));
                std::ignore = nvs.setU32(config::NVS_KEY_CAL_MAX_FREQ, static_cast<uint32_t>(cal.maxFreqHz));
                std::ignore = nvs.setI32(config::NVS_KEY_CAL_DATE, 0);
                ESP_LOGI(TAG, "Initialized %s namespace with defaults", config::NVS_NS_BURETTE_CAL);
            }
        }
    }
    {
        auto nvs = NvsHandle(config::NVS_NS_ADC_CAL, true);
        if (nvs.isValid()) {
            auto r = nvs.getU32(config::NVS_KEY_ADC_A_X1000);
            if (r && !r->has_value()) {
                std::ignore = nvs.setU32(config::NVS_KEY_ADC_A_X1000,
                    static_cast<uint32_t>(config::ADC_DEFAULT_A_X1000));
                std::ignore = nvs.setI32(config::NVS_KEY_ADC_B,
                    static_cast<int32_t>(config::ADC_DEFAULT_B));
                ESP_LOGI(TAG, "Initialized %s namespace with defaults", config::NVS_NS_ADC_CAL);
            }
        }
    }
    {
        auto nvs = NvsHandle(config::NVS_NS_STALLGUARD, true);
        if (nvs.isValid()) {
            auto r = nvs.getU32(config::NVS_KEY_SG_THRESHOLD);
            if (r && !r->has_value()) {
                std::ignore = nvs.setU32(config::NVS_KEY_SG_THRESHOLD, 0);
                ESP_LOGI(TAG, "Initialized %s namespace with defaults", config::NVS_NS_STALLGUARD);
            }
        }
    }
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

domain::Result<uint8_t, domain::ResourceError> wifiReadCount() {
    auto nvs = NvsHandle("wifi", false);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    auto r = nvs.getU8(config::NVS_KEY_WIFI_COUNT);
    if (!r) return std::unexpected(r.error());
    return r->value_or(0);
}

domain::Result<void, domain::ResourceError> wifiWriteCount(uint8_t count) {
    auto nvs = NvsHandle("wifi", true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);
    return nvs.setU8(config::NVS_KEY_WIFI_COUNT, count);
}

domain::Result<domain::CalibrationData, domain::ResourceError> calibrationRead() {
    auto nvs = NvsHandle(config::NVS_NS_BURETTE_CAL, false);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);

    domain::CalibrationData cal{};
    {
        auto r = nvs.getF32(config::NVS_KEY_CAL_SPM);
        cal.stepsPerMl = (r && r->has_value()) ? r->value() : domain::CalibrationData::kDefaultStepsPerMl;
    }
    {
        auto r = nvs.getF32(config::NVS_KEY_CAL_NOM);
        cal.nominalVolumeMl = (r && r->has_value()) ? r->value() : domain::CalibrationData::kDefaultNominalVolumeMl;
    }
    {
        auto r = nvs.getF32(config::NVS_KEY_CAL_COEFF);
        cal.speedCoeff = (r && r->has_value()) ? r->value() : domain::CalibrationData::kDefaultSpeedCoeff;
    }
    {
        auto r = nvs.getU32(config::NVS_KEY_CAL_MIN_FREQ);
        cal.minFreqHz = (r && r->has_value()) ? static_cast<uint16_t>(r->value() & 0xFFFF) : domain::CalibrationData::kDefaultMinFreqHz;
    }
    {
        auto r = nvs.getU32(config::NVS_KEY_CAL_MAX_FREQ);
        cal.maxFreqHz = (r && r->has_value()) ? static_cast<uint16_t>(r->value() & 0xFFFF) : domain::CalibrationData::kDefaultMaxFreqHz;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* old = domain::gCalCache.exchange(new domain::CalibrationData(cal));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete old;
    return cal;
}

domain::Result<void, domain::ResourceError> calibrationWrite(const domain::CalibrationData& cal) {
    auto nvs = NvsHandle(config::NVS_NS_BURETTE_CAL, true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);

    auto r1 = nvs.setF32(config::NVS_KEY_CAL_SPM, cal.stepsPerMl);
    if (!r1) return std::unexpected(r1.error());
    auto r2 = nvs.setF32(config::NVS_KEY_CAL_NOM, cal.nominalVolumeMl);
    if (!r2) return std::unexpected(r2.error());
    auto r3 = nvs.setF32(config::NVS_KEY_CAL_COEFF, cal.speedCoeff);
    if (!r3) return std::unexpected(r3.error());
    auto r4 = nvs.setU32(config::NVS_KEY_CAL_MIN_FREQ, static_cast<uint32_t>(cal.minFreqHz));
    if (!r4) return std::unexpected(r4.error());
    auto r5 = nvs.setU32(config::NVS_KEY_CAL_MAX_FREQ, static_cast<uint32_t>(cal.maxFreqHz));
    if (!r5) return std::unexpected(r5.error());
    auto r6 = nvs.setI32(config::NVS_KEY_CAL_DATE, 0);
    if (!r6) return std::unexpected(r6.error());
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* old = domain::gCalCache.exchange(new domain::CalibrationData(cal));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete old;
    return {};
}

void adcCalibrationRead(uint16_t& aX1000, int16_t& b) {
    auto nvs = NvsHandle(config::NVS_NS_ADC_CAL, false);
    if (!nvs.isValid()) {
        aX1000 = config::ADC_DEFAULT_A_X1000;
        b = config::ADC_DEFAULT_B;
        return;
    }
    {
        auto r = nvs.getU32(config::NVS_KEY_ADC_A_X1000);
        aX1000 = (r && r->has_value())
            ? static_cast<uint16_t>(r->value() & 0xFFFF)
            : config::ADC_DEFAULT_A_X1000;
    }
    {
        auto r = nvs.getI32(config::NVS_KEY_ADC_B);
        b = (r && r->has_value())
            ? static_cast<int16_t>(r->value())
            : config::ADC_DEFAULT_B;
    }
}

domain::Result<void, domain::ResourceError> adcCalibrationWrite(uint16_t aX1000, int16_t b) {
    auto nvs = NvsHandle(config::NVS_NS_ADC_CAL, true);
    if (!nvs.isValid()) return std::unexpected(domain::ResourceError::NvsOpenFailed);

    auto r1 = nvs.setU32(config::NVS_KEY_ADC_A_X1000, static_cast<uint32_t>(aX1000));
    if (!r1) return std::unexpected(r1.error());
    auto r2 = nvs.setI32(config::NVS_KEY_ADC_B, static_cast<int32_t>(b));
    if (!r2) return std::unexpected(r2.error());

    // Update runtime globals
    drivers::gCoeffAX1000.store(aX1000, std::memory_order_relaxed);
    drivers::gCoeffB.store(b, std::memory_order_relaxed);

    return {};
}

} // namespace ecotiter::infrastructure::storage
