#pragma once

#include <esp_heap_caps.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace ecotiter::memory {

/// RAII wrapper for PSRAM-allocated buffer.
/// Usage:
///   PsramBuffer<8192> http_response;
///   std::snprintf(reinterpret_cast<char*>(http_response.data()), ...);
///   httpd_resp_sendstr_chunk(req, reinterpret_cast<char*>(http_response.data()));
template <size_t N>
class PsramBuffer {
    uint8_t* data_ = nullptr;

public:
    PsramBuffer() {
        data_ = static_cast<uint8_t*>(
            heap_caps_malloc(N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data_) {
            std::abort();
        }
    }

    ~PsramBuffer() noexcept {
        if (data_) {
            heap_caps_free(data_);
        }
    }

    PsramBuffer(const PsramBuffer&) = delete;
    PsramBuffer& operator=(const PsramBuffer&) = delete;

    PsramBuffer(PsramBuffer&& other) noexcept : data_(other.data_) {
        other.data_ = nullptr;
    }

    PsramBuffer& operator=(PsramBuffer&& other) noexcept {
        if (this != &other) {
            if (data_) heap_caps_free(data_);
            data_ = other.data_;
            other.data_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return N; }
};

} // namespace ecotiter::memory
