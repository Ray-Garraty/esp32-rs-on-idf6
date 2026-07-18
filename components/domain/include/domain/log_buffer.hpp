#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace ecotiter::domain
{

struct LogEntry
{
    uint32_t timestampMs;
    char level[8];
    char message[128];
};

class LogBuffer
{
public:
    static constexpr size_t MAX_MSG_LEN = 128;
    static constexpr size_t LOG_QUEUE_LENGTH = 16;

    using Callback = void (*)(const LogEntry& entry);

    /// Initialize the singleton with PSRAM backing.
    /// Must be called before first push() — fail to init = push() is no-op.
    static void init(size_t capacity, std::pmr::memory_resource* res);

    static LogBuffer& instance();

    void push(uint32_t timestampMs, const char* level, const char* message);
    void clear();
    void setCallback(Callback cb);

    [[nodiscard]] size_t fetch(LogEntry* out, size_t maxCount,
                               const char* levelFilter = nullptr) const;

    static void workerTaskEntry(void* pvParameters);

private:
    LogBuffer() = default;

    struct Slot
    {
        std::atomic<uint32_t> timestampMs{0};
        char level[8]{};
        char message[MAX_MSG_LEN]{};

        Slot() = default;
        Slot(const Slot& other) noexcept
            : level{},
              message{}
        {
            timestampMs.store(other.timestampMs.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
            std::memcpy(level, other.level, sizeof(level));
            std::memcpy(message, other.message, sizeof(message));
        }
        Slot& operator=(const Slot& other) noexcept
        {
            if (this != &other)
            {
                timestampMs.store(other.timestampMs.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
                std::memcpy(level, other.level, sizeof(level));
                std::memcpy(message, other.message, sizeof(message));
            }
            return *this;
        }
        Slot(Slot&& other) noexcept
            : Slot()
        {
            timestampMs.store(other.timestampMs.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
            std::memcpy(level, other.level, sizeof(level));
            std::memcpy(message, other.message, sizeof(message));
        }
        Slot& operator=(Slot&& other) noexcept
        {
            if (this != &other)
            {
                timestampMs.store(other.timestampMs.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
                std::memcpy(level, other.level, sizeof(level));
                std::memcpy(message, other.message, sizeof(message));
            }
            return *this;
        }
    };

    std::pmr::vector<Slot> slots_; // PSRAM-backed (after init)
    size_t capacity_ = 0;          // actual capacity (may differ from slots_.size())
    std::atomic<size_t> head_{0};
    std::atomic<bool> pushing_{false};
    Callback callback_{nullptr};
    QueueHandle_t queue_{nullptr};
    std::atomic<bool> initialized_{false};
};

} // namespace ecotiter::domain
