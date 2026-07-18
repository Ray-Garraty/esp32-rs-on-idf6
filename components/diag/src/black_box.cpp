#include "diag/black_box.hpp"
#include <cstdio>
#include <cstring>
#include "esp_timer.h"

namespace ecotiter::diag
{

namespace
{

// Minimal integer-to-string formatter for panic-safe contexts
// Returns pointer to the start of the NUL-terminated string within buf
const char* fmt_uint(unsigned long long val, char* buf, size_t cap)
{
    char* p = buf + cap;
    *--p = '\0';
    if (val == 0)
    {
        *--p = '0';
        return p;
    }
    while (val > 0 && p > buf)
    {
        *--p = '0' + (val % 10);
        val /= 10;
    }
    return p;
}

struct CallbackWriter
{
    void (*write)(const char*);

    void operator()(const char* s) const { write(s); }

    void put(char c) const
    {
        char tmp[2] = {c, '\0'};
        write(tmp);
    }

    void num(unsigned long long v) const
    {
        char buf[24];
        write(fmt_uint(v, buf, sizeof(buf)));
    }
};

} // anonymous namespace

void BlackBox::init() noexcept
{
    head_.store(0, std::memory_order_release);
}

void BlackBox::record(Event e) noexcept
{
    e.timestampUs = static_cast<uint64_t>(esp_timer_get_time());
    size_t idx = head_.fetch_add(1, std::memory_order_acq_rel) % RING_SIZE;
    // CONTRACT: volatile ring buffer — use memcpy for volatile access
    std::memcpy(const_cast<Event*>(&ring_[idx]), &e, sizeof(Event));
}

void BlackBox::dump(void (*write)(const char*)) const noexcept
{
    size_t head = head_.load(std::memory_order_acquire);
    size_t count = head < RING_SIZE ? head : RING_SIZE;

    CallbackWriter w{write};

    w("=== BLACK BOX (");
    w.num(static_cast<unsigned long long>(count));
    w(" events, newest first) ===\n");

    for (size_t i = 0; i < count; ++i)
    {
        size_t idx = (head - 1 - i) % RING_SIZE;
        const auto& e = ring_[idx];
        w("[");
        w.num(e.timestampUs);
        w("] t");
        w.num(static_cast<unsigned long long>(e.threadId));
        w(" type=");
        w.num(static_cast<unsigned long long>(static_cast<uint8_t>(e.type)));
        w(" id=");
        w.num(static_cast<unsigned long long>(e.payloadId));
        w(" val=");
        w.num(static_cast<unsigned long long>(e.payloadValue));
        w("\n");
    }
}

void BlackBox::dump() const noexcept
{
    // Fallback: uses printf (not panic-safe, but available outside panic context)
    size_t head = head_.load(std::memory_order_acquire);
    size_t count = head < RING_SIZE ? head : RING_SIZE;

    printf("=== BLACK BOX (%zu events, newest first) ===\n", count);
    for (size_t i = 0; i < count; ++i)
    {
        size_t idx = (head - 1 - i) % RING_SIZE;
        const auto& e = ring_[idx];
        printf("[%llu] t%hhu type=%hhu id=%hu val=%lu\n",
               static_cast<unsigned long long>(e.timestampUs), e.threadId,
               static_cast<uint8_t>(e.type), e.payloadId,
               static_cast<unsigned long>(e.payloadValue));
    }
}

} // namespace ecotiter::diag
