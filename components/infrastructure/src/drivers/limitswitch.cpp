#include "infrastructure/drivers/limitswitch.hpp"
#include "esp_check.h"
#include "esp_log.h"

static constexpr auto TAG = "limitswitch";

// CONTRACT: ISR handler runs in interrupt context. Only atomic store with
// relaxed ordering — no blocking, no heap, no logging. IRAM_ATTR ensures
// the handler remains mapped during flash cache misses.
static void IRAM_ATTR limitswitch_isr_handler(void* arg)
{
    auto* flag = static_cast<std::atomic<bool>*>(arg);
    flag->store(true, std::memory_order_relaxed);
}

namespace ecotiter::infrastructure::drivers
{

LimitSwitch::LimitSwitch(gpio_num_t pin, std::atomic<bool>& flag, bool pullDown)
    : pin_(pin),
      flag_(flag)
{
    (void)pullDown;

    puts("DBG: LS ctor done");
    fflush(stdout);

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(pin_, limitswitch_isr_handler, static_cast<void*>(&flag_)));
}

LimitSwitch::~LimitSwitch()
{
    gpio_isr_handler_remove(pin_);
}

bool LimitSwitch::isTriggered() const
{
    return flag_.load(std::memory_order_acquire);
}

void LimitSwitch::clear()
{
    flag_.store(false, std::memory_order_release);
}

domain::Result<void, domain::StepperError> LimitSwitch::rearm()
{
    esp_err_t err = gpio_intr_enable(pin_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to rearm interrupt: %s", esp_err_to_name(err));
        return std::unexpected(domain::StepperError::InitFailed);
    }
    return {};
}

bool LimitSwitch::level() const
{
    return gpio_get_level(pin_) != 0;
}

} // namespace ecotiter::infrastructure::drivers
