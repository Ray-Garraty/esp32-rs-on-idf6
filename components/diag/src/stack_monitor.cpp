#include "diag/stack_monitor.hpp"
#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static constexpr auto TAG = "stack_monitor";

namespace ecotiter::diag {
namespace {
struct KnownTask { const char* name; size_t stack; };
} // anonymous namespace

void StackMonitor::registerMainTask() noexcept {
    registerThread("main", 32768);
    static constexpr KnownTask kInternal[] = {
        {"ipc0", 4096},
        {"ipc1", 4096},
    };
    for (auto& t : kInternal) {
        TaskHandle_t h = xTaskGetHandle(t.name);
        if (h != nullptr) {
            registerByHandle(h, t.name, t.stack);
        }
    }
}

void StackMonitor::registerLazyTasks() noexcept {
    static constexpr KnownTask kLazy[] = {
        {"wifi", 8192},
    };
    for (auto& t : kLazy) {
        TaskHandle_t h = nullptr;
        for (int attempt = 0; attempt < 5; ++attempt) {
            h = xTaskGetHandle(t.name);
            if (h != nullptr) {
                registerByHandle(h, t.name, t.stack);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (h == nullptr) {
            ESP_LOGW("stack_monitor", "Deferred task '%s' not found after 5 retries", t.name);
        }
    }
}

void StackMonitor::registerThread(const char* name, size_t stackSize) noexcept {
    if (count_ >= MAX_THREADS) {
        ESP_LOGW(TAG, "Too many threads registered");
        return;
    }
    names_[count_] = name;
    stackSizes_[count_] = stackSize;
    handles_[count_] = xTaskGetCurrentTaskHandle();
    ++count_;
}

void StackMonitor::registerByHandle(TaskHandle_t handle, const char* name, size_t stackSize) noexcept {
    if (count_ >= MAX_THREADS) {
        ESP_LOGW(TAG, "Too many threads registered");
        return;
    }
    names_[count_] = name;
    stackSizes_[count_] = stackSize;
    handles_[count_] = handle;
    ++count_;
}

uint32_t StackMonitor::watermarkMain() const noexcept {
    UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
    return static_cast<uint32_t>(wm);
}

void StackMonitor::logAllWatermarks() const noexcept { // NOLINT(readability-function-cognitive-complexity) // reason: aggregates watermarks across all registered tasks
    for (size_t i = 0; i < count_; ++i) {
        UBaseType_t wm = uxTaskGetStackHighWaterMark(handles_[i]);
        uint32_t usedBytes = static_cast<uint32_t>(
            stackSizes_[i] - static_cast<size_t>(wm));
        uint32_t usedPct = (stackSizes_[i] > 0)
            ? (usedBytes * 100 / static_cast<uint32_t>(stackSizes_[i])) : 0;
        ESP_LOGI(TAG, "Thread %s: cfg=%zuB wmark=%u used=%u%%",
                 names_[i], stackSizes_[i],
                 static_cast<unsigned>(wm),
                 static_cast<unsigned>(usedPct));
        if (usedPct > 90) {
            ESP_LOGW(TAG, "Thread %s: LOW STACK! %u%% used",
                     names_[i], static_cast<unsigned>(usedPct));
        }
    }
}

void StackMonitor::logAllWatermarks(void (*print)(const char*)) const noexcept {
    for (size_t i = 0; i < count_; ++i) {
        UBaseType_t wm = uxTaskGetStackHighWaterMark(handles_[i]);
        uint32_t usedBytes = static_cast<uint32_t>(
            stackSizes_[i] - static_cast<size_t>(wm));
        uint32_t usedPct = (stackSizes_[i] > 0)
            ? (usedBytes * 100 / static_cast<uint32_t>(stackSizes_[i])) : 0;

        char buf[128];
        int n = std::snprintf(buf, sizeof(buf),
            "Thread %s: cfg=%zuB wmark=%u used=%u%%\n",
            names_[i], stackSizes_[i],
            static_cast<unsigned>(wm),
            static_cast<unsigned>(usedPct));
        if (n > 0 && static_cast<size_t>(n) < sizeof(buf)) {
            print(buf);
        }
        if (usedPct > 90) {
            char warn[64];
            int wn = std::snprintf(warn, sizeof(warn),
                "Thread %s: LOW STACK! %u%% used\n",
                names_[i], static_cast<unsigned>(usedPct));
            if (wn > 0 && static_cast<size_t>(wn) < sizeof(warn)) {
                print(warn);
            }
        }
    }
}

} // namespace ecotiter::diag
