#pragma once

#include <cstdint>
#include <optional>

#include "domain/burette.hpp"
#include "domain/errors.hpp"
#include "domain/types.hpp"

namespace ecotiter::application
{

enum class TransportState : uint8_t
{
    UsbActive,
    BleDisconnected,
    BleConnected,
    HttpConnected
};

struct PendingOperation
{
    uint32_t startTick{0};
    uint32_t expectedDurationTicks{0};
    bool active{false};
};

// Max time (ms) for any pending operation before auto-transition to Idle
inline constexpr uint32_t kPendingWatchdogMs = 300000; // 5 min

class ApplicationStateMachine
{
public:
    [[nodiscard]] domain::BuretteState buretteState() const noexcept { return controller_.state; }

    [[nodiscard]] TransportState transportState() const noexcept { return transport_; }

    void setTransportState(TransportState ts) noexcept { transport_ = ts; }

    [[nodiscard]] bool isReady() const noexcept
    {
        return controller_.state == domain::BuretteState::Idle && !pending_.active;
    }

    [[nodiscard]] bool isEmergency() const noexcept
    {
        return controller_.state == domain::BuretteState::Stopping;
    }

    // Apply a high-level command to the burette state machine.
    // Returns the new burette state or an error.
    [[nodiscard]] domain::Result<void, domain::StateError>
    apply(domain::BuretteCommand cmd) noexcept;

    // Start tracking a long-running operation.
    void startOperation(uint32_t currentTick, uint32_t durationTicks) noexcept;

    // Tick the pending operation timeout.
    // Returns true if a pending operation just completed (or timed out).
    bool tick(uint32_t currentTick) noexcept;

    // Forcefully clear pending operation and reset to Idle.
    void reset() noexcept;

private:
    domain::BuretteController controller_;
    TransportState transport_{TransportState::UsbActive};
    PendingOperation pending_;
};

} // namespace ecotiter::application
