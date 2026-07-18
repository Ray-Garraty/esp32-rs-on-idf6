#pragma once

#include <atomic>
#include <memory>

// gsl::owner alias for clang-tidy cppcoreguidelines-owning-memory annotation.
// This is a transparent alias — clang-tidy recognizes it as marking ownership.
namespace gsl
{
template <typename T>
using owner = T;
}

namespace ecotiter::domain
{

template <typename T>
class AtomicOwner
{
    std::atomic<T*> ptr_;
public:
    AtomicOwner() noexcept : ptr_(nullptr) {}
    ~AtomicOwner() { delete ptr_.exchange(nullptr, std::memory_order_acq_rel); }
    AtomicOwner(const AtomicOwner&) = delete;
    AtomicOwner& operator=(const AtomicOwner&) = delete;
    gsl::owner<T*> exchange(gsl::owner<T*> p, std::memory_order mo = std::memory_order_acq_rel) noexcept
    {
        return ptr_.exchange(p, mo);
    }
    T* load(std::memory_order mo = std::memory_order_acquire) const noexcept { return ptr_.load(mo); }
    void store(gsl::owner<T*> p, std::memory_order mo = std::memory_order_release) noexcept { ptr_.store(p, mo); }
    bool isNull(std::memory_order mo = std::memory_order_acquire) const noexcept { return load(mo) == nullptr; }
};

} // namespace ecotiter::domain
