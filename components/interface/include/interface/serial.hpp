#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <string_view>

#include "domain/memory.hpp"

namespace ecotiter::interface {

enum class SerialError : uint8_t {
    InitFailed,
    WriteFailed
};

template <typename T>
using Result = std::expected<T, SerialError>;

class SerialReader {
public:
    SerialReader() = default;
#ifdef ESP_PLATFORM
    ~SerialReader();
#else
    ~SerialReader() = default;
#endif

    SerialReader(const SerialReader&) = delete;
    SerialReader& operator=(const SerialReader&) = delete;
    SerialReader(SerialReader&&) = delete;
    SerialReader& operator=(SerialReader&&) = delete;

    [[nodiscard]] Result<void> init() noexcept;

    // Non-blocking read from UART fd; returns one complete line at a time.
    // Empty optional means no complete line available yet.
    std::optional<std::string_view> process() noexcept;

    // Overload for testing: process inline data through the same splitting logic.
    // Does NOT read from fd_.
    std::optional<std::string_view> process(std::string_view data) noexcept {
        return splitBuffer(data);
    }

    void write(std::string_view s) noexcept;
    void setSilent(bool s) noexcept { silent_.store(s, std::memory_order_release); }
    [[nodiscard]] bool isSilent() const noexcept { return silent_.load(std::memory_order_acquire); }
    [[nodiscard]] bool isInitialized() const noexcept { return fd_ >= 0; }

    static constexpr size_t INPUT_BUF_SIZE = 256;

private:
    // Feed raw data through the line-split state machine.
    // Uses a two-cursor design: readPos_ tracks start of unconsumed buffer,
    // linePos_ tracks the write position. Fully consumed regions are compacted
    // at the start of each call.
    // Returns a view into lineBuf_ for the first complete line found,
    // or nullopt if no complete line is available yet.
    // WARNING: returned view is valid only until the next process() call.
    std::optional<std::string_view> splitBuffer(std::string_view data) noexcept {
        // Compact consumed data at start of buffer
        if (readPos_ > 0) {
            if (readPos_ < linePos_) {
                size_t remaining = linePos_ - readPos_;
                std::memmove(lineBuf_.data(),
                             lineBuf_.data() + readPos_, remaining);
                linePos_ = remaining;
            } else {
                linePos_ = 0;
            }
            readPos_ = 0;
        }

        // Append all incoming data to the buffer
        for (size_t i = 0; i < data.size(); ++i) {
            char c = data[i];

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                if (linePos_ == readPos_) {
                    continue;
                }
                lineBuf_[linePos_++] = '\n';
                continue;
            }

            if (linePos_ >= domain::memory::MAX_CMD_SIZE) {
                linePos_ = 0;
                readPos_ = 0;
                break;
            }

            lineBuf_[linePos_++] = c;
        }

        // Scan for first complete line
        for (size_t i = readPos_; i < linePos_; ++i) {
            if (lineBuf_[i] == '\n') {
                auto result = std::string_view(lineBuf_.data() + readPos_,
                                                i - readPos_);
                readPos_ = i + 1;
                return result;
            }
        }

        return std::nullopt;
    }

    int fd_{-1};
    std::atomic<bool> silent_{false};
    std::array<char, domain::memory::MAX_CMD_SIZE> lineBuf_{};
    size_t linePos_{0};
    size_t readPos_{0};
};

} // namespace ecotiter::interface
