#include "diag/state_tracer.hpp"
#include "diag/black_box.hpp"
#include "esp_log.h"

static constexpr auto TAG = "state";

namespace ecotiter::diag
{

void StateTracer::logBuretteTransition(std::string_view from, std::string_view to) noexcept
{
    ESP_LOGI(TAG, "Burette: %.*s -> %.*s", static_cast<int>(from.size()), from.data(),
             static_cast<int>(to.size()), to.data());
    BlackBox::Event ev = {};
    ev.type = BlackBox::EventType::StateTransition;
    ev.payloadId = 0;
    ev.payloadValue = 0;
    BlackBox::instance().record(ev);
}

void StateTracer::logTransportTransition(std::string_view from, std::string_view to) noexcept
{
    ESP_LOGI(TAG, "Transport: %.*s -> %.*s", static_cast<int>(from.size()), from.data(),
             static_cast<int>(to.size()), to.data());
    BlackBox::Event ev = {};
    ev.type = BlackBox::EventType::StateTransition;
    ev.payloadId = 1;
    ev.payloadValue = 0;
    BlackBox::instance().record(ev);
}

void StateTracer::logError(std::string_view source, std::string_view message) noexcept
{
    ESP_LOGE(TAG, "Error [%.*s]: %.*s", static_cast<int>(source.size()), source.data(),
             static_cast<int>(message.size()), message.data());
    BlackBox::Event ev = {};
    ev.type = BlackBox::EventType::Error;
    ev.payloadId = 0;
    ev.payloadValue = 0;
    BlackBox::instance().record(ev);
}

} // namespace ecotiter::diag
