#pragma once

#include <cstdint>

namespace ecotiter::diag
{

// Boundary ID convention:
//   50       main boot
//   60-69    BLE
//   70-79    WiFi
//   80-85    HTTP server
// RAII guard for ESP-IDF C API boundaries.
// Records FfiEnter in constructor, FfiExit in destructor.
// Must wrap every ESP-IDF C API call (GR-7).
class FfiGuard
{
public:
    explicit FfiGuard(uint16_t boundaryId) noexcept;
    ~FfiGuard() noexcept;

    FfiGuard(const FfiGuard&) = delete;
    FfiGuard& operator=(const FfiGuard&) = delete;

private:
    uint16_t boundaryId_;
    bool exited_ = false;
};

} // namespace ecotiter::diag
