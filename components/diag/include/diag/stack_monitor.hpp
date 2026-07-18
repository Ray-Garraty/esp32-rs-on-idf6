#pragma once

#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ecotiter::diag
{

class StackMonitor
{
public:
    [[nodiscard]] static StackMonitor& instance() noexcept
    {
        static StackMonitor sm;
        return sm;
    }

    void registerMainTask() noexcept;
    void registerThread(const char* name, size_t stackSize) noexcept;
    void registerByHandle(TaskHandle_t handle, const char* name, size_t stackSize) noexcept;
    void registerLazyTasks() noexcept;
    [[nodiscard]] uint32_t watermarkMain() const noexcept;
    void logAllWatermarks() const noexcept;
    void logAllWatermarks(void (*print)(const char*)) const noexcept;

    StackMonitor(const StackMonitor&) = delete;
    StackMonitor& operator=(const StackMonitor&) = delete;

private:
    StackMonitor() = default;
    static constexpr size_t MAX_THREADS = 16;
    const char* names_[MAX_THREADS]{};
    size_t stackSizes_[MAX_THREADS]{};
    TaskHandle_t handles_[MAX_THREADS]{};
    size_t count_{0};
};

} // namespace ecotiter::diag
