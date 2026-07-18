#pragma once

#include <cstdint>
#include <string_view>

namespace ecotiter::diag
{

class StateTracer
{
public:
    static void logBuretteTransition(std::string_view from, std::string_view to) noexcept;

    static void logTransportTransition(std::string_view from, std::string_view to) noexcept;

    static void logError(std::string_view source, std::string_view message) noexcept;
};

} // namespace ecotiter::diag
