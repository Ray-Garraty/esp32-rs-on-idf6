#include "domain/log_buffer.hpp"

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace ecotiter::domain {

namespace {
    QueueHandle_t ensureQueue(QueueHandle_t& queueSlot) {
        if (queueSlot == nullptr) {
            queueSlot = xQueueCreate(LogBuffer::LOG_QUEUE_LENGTH, sizeof(size_t));
        }
        return queueSlot;
    }
}

void LogBuffer::init(size_t capacity, std::pmr::memory_resource* res) {
    auto& self = instance();
    if (self.initialized_.load(std::memory_order_acquire)) return;

    // Replace default-allocated vector with PSRAM-allocated one
    self.slots_.~vector();
    ::new (&self.slots_) std::pmr::vector<Slot>(res);
    self.slots_.resize(capacity);
    self.capacity_ = capacity;
    self.queue_ = nullptr;
    self.initialized_.store(true, std::memory_order_release);
}

LogBuffer& LogBuffer::instance() {
    static LogBuffer buf;
    return buf;
}

void LogBuffer::push(uint32_t timestampMs, const char* level, const char* message) {
    if (!initialized_.load(std::memory_order_acquire)) return;
    if (pushing_.load(std::memory_order_relaxed)) return;
    pushing_.store(true, std::memory_order_relaxed);

    size_t idx = head_.fetch_add(1, std::memory_order_acq_rel) % capacity_;
    auto& slot = slots_[idx];

    slot.level[0] = '\0';
    slot.message[0] = '\0';
    std::strncpy(slot.level, level, sizeof(slot.level) - 1);
    slot.level[sizeof(slot.level) - 1] = '\0';
    std::strncpy(slot.message, message, sizeof(slot.message) - 1);
    slot.message[sizeof(slot.message) - 1] = '\0';
    slot.timestampMs.store(timestampMs, std::memory_order_release);

    auto q = ensureQueue(queue_);
    if (q) {
        xQueueSend(q, &idx, 0);
    }

    pushing_.store(false, std::memory_order_relaxed);
}

void LogBuffer::workerTaskEntry(void* pvParameters) {
    auto& self = instance();
    auto q = self.queue_;
    if (q == nullptr) {
        return;
    }

    LogEntry entry;
    while (true) {
        size_t idx;
        if (xQueueReceive(q, &idx, portMAX_DELAY) == pdTRUE) {
            auto& slot = self.slots_[idx];
            uint32_t ts = slot.timestampMs.load(std::memory_order_acquire);
            if (ts == 0) continue;

            entry.timestampMs = ts;
            std::strncpy(entry.level, slot.level, sizeof(entry.level) - 1);
            entry.level[sizeof(entry.level) - 1] = '\0';
            std::strncpy(entry.message, slot.message, sizeof(entry.message) - 1);
            entry.message[sizeof(entry.message) - 1] = '\0';

            if (self.callback_) {
                self.callback_(entry);
            }
        }
    }
}

void LogBuffer::clear() {
    if (!initialized_.load(std::memory_order_acquire)) return;
    for (auto& slot : slots_) {
        slot.timestampMs.store(0, std::memory_order_release);
        slot.level[0] = '\0';
        slot.message[0] = '\0';
    }
    head_.store(0, std::memory_order_release);
}

void LogBuffer::setCallback(Callback cb) {
    callback_ = cb;
}

size_t LogBuffer::fetch(LogEntry* out, size_t maxCount,
                         const char* levelFilter) const {
    if (!initialized_.load(std::memory_order_acquire)) return 0;
    size_t currentHead = head_.load(std::memory_order_acquire);
    size_t written = 0;

    for (size_t offset = 1; offset <= capacity_ && written < maxCount; ++offset) {
        size_t idx = (currentHead >= offset)
            ? (currentHead - offset) % capacity_
            : (capacity_ + currentHead - offset) % capacity_;

        uint32_t ts = slots_[idx].timestampMs.load(std::memory_order_acquire);
        if (ts == 0) continue;

        if (levelFilter && levelFilter[0] != '\0') {
            if (std::strcmp(slots_[idx].level, levelFilter) != 0) continue;
        }

        auto& e = out[written];
        e.timestampMs = ts;
        std::strncpy(e.level, slots_[idx].level, sizeof(e.level) - 1);
        e.level[sizeof(e.level) - 1] = '\0';
        std::strncpy(e.message, slots_[idx].message, sizeof(e.message) - 1);
        e.message[sizeof(e.message) - 1] = '\0';
        ++written;
    }

    return written;
}

} // namespace ecotiter::domain
