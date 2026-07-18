#pragma once

#include <cstddef>
#include <cstdint>

namespace ecotiter::diag
{

class HeapSnapshot
{
public:
    // Check if a contiguous allocation of `size` bytes is possible
    [[nodiscard]] static bool canAllocate(size_t size) noexcept;

    // Get largest free block in internal DRAM
    [[nodiscard]] static size_t largestFreeBlock() noexcept;

    // Log current heap state
    static void log() noexcept;

    // Assert that a contiguous allocation of `size` bytes is possible (GR-7)
    // Logs warning if insufficient; returns false if cannot allocate
    [[nodiscard]] static bool assertCanAllocate(size_t size) noexcept;
};

} // namespace ecotiter::diag
