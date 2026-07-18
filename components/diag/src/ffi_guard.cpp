#include "diag/ffi_guard.hpp"
#include "diag/black_box.hpp"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ecotiter::diag
{

static uint8_t getThreadId() noexcept
{
    auto current = xTaskGetCurrentTaskHandle();
    // Use pcTaskGetName for name lookup (also available as pcTaskGetTaskName)
    const char* name = pcTaskGetName(current);

    struct NameIdPair
    {
        const char* name;
        uint8_t id;
    };
    static constexpr NameIdPair kKnown[] = {
        {"main", 1},       {"motor", 2},      {"temp", 3},    {"net_owner", 4},
        {"ble_notify", 5}, {"log_worker", 6}, {"Tmr Svc", 7},
    };
    for (auto& entry : kKnown)
    {
        if (std::strcmp(name, entry.name) == 0)
            return entry.id;
    }
    return 0xFF; // unknown thread
}

FfiGuard::FfiGuard(uint16_t boundaryId) noexcept
    : boundaryId_(boundaryId)
{
    auto& bb = BlackBox::instance();
    BlackBox::Event ev = {};
    ev.type = BlackBox::EventType::FfiEnter;
    ev.threadId = getThreadId();
    ev.payloadId = boundaryId_;
    ev.payloadValue = 0;
    bb.record(ev);
}

FfiGuard::~FfiGuard() noexcept
{
    if (!exited_)
    {
        auto& bb = BlackBox::instance();
        BlackBox::Event ev = {};
        ev.type = BlackBox::EventType::FfiExit;
        ev.threadId = getThreadId();
        ev.payloadId = boundaryId_;
        ev.payloadValue = 0;
        bb.record(ev);
    }
}

} // namespace ecotiter::diag
