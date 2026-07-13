#pragma once

#include <memory_resource>
#include <cstdlib>
#include <esp_heap_caps.h>

namespace ecotiter::memory {

/// PMR memory resource that allocates from PSRAM via heap_caps_malloc.
/// Usage:
///   std::pmr::vector<uint32_t> ramp{&psram_resource()};
///   std::pmr::string json_out{&psram_resource()};
class PsramResource : public std::pmr::memory_resource {
protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ptr) {
            std::abort();  // PSRAM exhaustion is fatal
        }
        return ptr;
    }

    void do_deallocate(void* p, std::size_t, std::size_t) noexcept override {
        heap_caps_free(p);
    }

    bool do_is_equal(const memory_resource& other) const noexcept override {
        return this == &other;
    }
};

/// Global singleton PSRAM resource.
inline PsramResource& psram_resource() {
    static PsramResource instance;
    return instance;
}

} // namespace ecotiter::memory
