#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>

namespace ecotiter::domain {

struct LogEntry {
    uint32_t timestampMs;
    char level[8];
    char message[128];
};

class LogBuffer {
public:
    static constexpr size_t MAX_ENTRIES = 100;
    static constexpr size_t MAX_MSG_LEN = 128;
    static constexpr size_t LOG_QUEUE_LENGTH = 16;

    using Callback = void(*)(const LogEntry& entry);

    static LogBuffer& instance();

    void push(uint32_t timestampMs, const char* level, const char* message);
    void clear();
    void setCallback(Callback cb);

    [[nodiscard]] size_t fetch(LogEntry* out, size_t maxCount,
                                const char* levelFilter = nullptr) const;

    // Async worker — created from main.cpp after setCallback()
    static void workerTaskEntry(void* pvParameters);

private:
    LogBuffer() = default;

    struct Slot {
        std::atomic<uint32_t> timestampMs{0};
        char level[8]{};
        char message[MAX_MSG_LEN]{};
    };

    Slot slots_[MAX_ENTRIES];
    std::atomic<size_t> head_{0};
    std::atomic<bool> pushing_{false};
    Callback callback_{nullptr};
    void* queue_{nullptr};  // QueueHandle_t (FreeRTOS) — opaque
};

} // namespace ecotiter::domain
