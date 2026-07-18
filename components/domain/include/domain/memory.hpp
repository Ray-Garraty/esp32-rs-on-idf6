#pragma once

#include <array>
#include <cstddef>

namespace ecotiter::domain::memory
{

inline constexpr size_t MAX_CMD_SIZE = 256;
inline constexpr size_t MAX_RSP_SIZE = 2048;
inline constexpr size_t LOG_BUF_ENTRIES = 1000;
inline constexpr size_t ADC_BUF_SIZE = 64;
inline constexpr size_t DNS_BUF_SIZE = 512;

using CommandBuffer = std::array<char, MAX_CMD_SIZE>;
using ResponseBuffer = std::array<char, MAX_RSP_SIZE>;
using AdcBuf = std::array<uint16_t, ADC_BUF_SIZE>;
using DnsBuf = std::array<uint8_t, DNS_BUF_SIZE>;

} // namespace ecotiter::domain::memory
