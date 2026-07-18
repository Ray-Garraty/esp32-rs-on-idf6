#include "infrastructure/drivers/adc.hpp"
#include "esp_check.h"
#include "esp_log.h"

#include <algorithm>
#include <limits>

static constexpr auto TAG = "adc";

namespace ecotiter::infrastructure::drivers
{

int16_t calibratedFromRaw(uint16_t raw)
{
    int32_t a = gCoeffAX1000.load(std::memory_order_relaxed);
    int32_t b = gCoeffB.load(std::memory_order_relaxed);
    int32_t result = (a * static_cast<int32_t>(raw)) / 1000 + b;
    return static_cast<int16_t>(
        std::clamp(result, static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                   static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
}

AdcDriver::AdcDriver(adc_unit_t unit, adc_channel_t channel)
    : channel_(channel)
{

    adc_oneshot_unit_init_cfg_t initConfig = {
        .unit_id = unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&initConfig, &handle_));

    adc_oneshot_chan_cfg_t chanConfig = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle_, channel_, &chanConfig));
}

AdcDriver::~AdcDriver()
{
    if (handle_)
    {
        adc_oneshot_del_unit(handle_);
    }
}

domain::Result<uint16_t, domain::SensorError> AdcDriver::readRaw()
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(handle_, channel_, &raw);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(err));
        return std::unexpected(domain::SensorError::AdcReadFailed);
    }

    uint16_t uraw = static_cast<uint16_t>(raw);
    if (count_ >= buf_.size())
    {
        count_ = 0;
    }
    buf_[count_++] = uraw;

    return uraw;
}

int16_t AdcDriver::calibratedMv()
{
    auto rawResult = readRaw();
    if (!rawResult)
    {
        return 0;
    }
    return calibratedFromRaw(*rawResult);
}

} // namespace ecotiter::infrastructure::drivers
