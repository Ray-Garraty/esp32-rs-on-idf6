#include "diag/heap_snapshot.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"

static constexpr auto TAG = "heap";

namespace ecotiter::diag
{

bool HeapSnapshot::canAllocate(size_t size) noexcept
{
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return largest >= size;
}

size_t HeapSnapshot::largestFreeBlock() noexcept
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

void HeapSnapshot::log() noexcept
{
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    auto free8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    auto total8 = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "DRAM: total=%u free=%u largest=%u", static_cast<unsigned>(total8),
             static_cast<unsigned>(free8), static_cast<unsigned>(largest));
}

bool HeapSnapshot::assertCanAllocate(size_t size) noexcept
{
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largest < size)
    {
        ESP_LOGW(TAG, "Cannot alloc %u B (largest=%u B)", static_cast<unsigned>(size),
                 static_cast<unsigned>(largest));
        return false;
    }
    return true;
}

} // namespace ecotiter::diag
