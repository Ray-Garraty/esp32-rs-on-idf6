#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ecotiter::diag
{

// Lock-free ring buffer for pre-mortem events.
// 64 events x 16 bytes = 1 KB SRAM. ~5 us per event.
// Designed to be dumpable from panic handler context (no heap, no mutex).
class BlackBox
{
public:
    enum class EventType : uint8_t
    {
        FfiEnter,
        FfiExit,
        StateTransition,
        ThreadStart,
        TickBegin,
        TickEnd,
        AllocRequest,
        Error,
        TickDuration
    };

    struct Event
    {
        uint64_t timestampUs;
        EventType type;
        uint8_t threadId;
        uint16_t payloadId;
        uint32_t payloadValue;
    };

    [[nodiscard]] static BlackBox& instance() noexcept
    {
        static BlackBox bb;
        return bb;
    }

    void init() noexcept;
    void record(Event e) noexcept;
    void dump() const noexcept; // uses printf — not safe when UART driver is broken
    void dump(void (*write)(const char*)) const noexcept; // panic-safe: writes via callback

    BlackBox(const BlackBox&) = delete;
    BlackBox& operator=(const BlackBox&) = delete;

private:
    BlackBox() = default;

    static constexpr size_t RING_SIZE = 64;
    volatile Event ring_[RING_SIZE]{};
    std::atomic<size_t> head_{0};
};

} // namespace ecotiter::diag
