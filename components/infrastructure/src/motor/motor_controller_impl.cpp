#include "infrastructure/motor_controller_impl.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "domain/calibration.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/motor_task.hpp"

namespace ecotiter::infrastructure
{

// ============================================================================
// Construction
// ============================================================================

MotorControllerImpl::MotorControllerImpl(QueueHandle_t cmdQueue, QueueHandle_t resultQueue)
    : cmdQueue_(cmdQueue),
      resultQueue_(resultQueue),
      useExplicitHandles_(true)
{}

QueueHandle_t MotorControllerImpl::getCmdQueue() const noexcept
{
    return useExplicitHandles_ ? cmdQueue_ : gMotorCmdQueue;
}

QueueHandle_t MotorControllerImpl::getResultQueue() const noexcept
{
    return useExplicitHandles_ ? resultQueue_ : gSmResultQueue;
}

// ============================================================================
// Minimal JSON helpers
//
// These operate on well-formed JSON strings only (as produced by the
// application layer). They are NOT general-purpose JSON parsers.
// ============================================================================

/// Find the value of a string key in a JSON object.
/// Looks for `"key":"<value>"` (no escaping supported).
/// Returns pointer to the start of the value (after the opening quote)
/// and sets *valueLen to the length of the value.
const char* MotorControllerImpl::findJsonString(const char* json, size_t len, const char* key,
                                                size_t* valueLen)
{
    // Build search pattern: "key":
    size_t keyLen = std::strlen(key);
    // We need room for "key": at minimum
    if (len < keyLen + 3)
        return nullptr;

    const char* end = json + len;
    const char* p = json;

    while ((p = static_cast<const char*>(std::memchr(p, '"', static_cast<size_t>(end - p)))) !=
           nullptr)
    {
        ++p; // skip opening quote
        // Check if this is our key
        if (static_cast<size_t>(end - p) < keyLen + 2)
            break;
        if (std::strncmp(p, key, keyLen) == 0 && p[keyLen] == '"')
        {
            // Found "key" — now look for :
            p += keyLen + 1;
            // Skip whitespace and colon
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end || *p != ':')
                continue;
            ++p; // skip colon
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end || *p != '"')
                continue;
            ++p; // skip opening quote of value
            const char* valStart = p;
            // Find closing quote
            while (p < end && *p != '"')
                ++p;
            if (p >= end)
                return nullptr;
            *valueLen = static_cast<size_t>(p - valStart);
            return valStart;
        }
        // Skip to next potential key
        while (p < end && *p != '"')
            ++p;
    }
    return nullptr;
}

/// Find an integer value for a key.
/// Looks for `"key":<integer>`.
bool MotorControllerImpl::findJsonInt(const char* json, size_t len, const char* key, int32_t& value)
{
    size_t keyLen = std::strlen(key);
    const char* end = json + len;
    const char* p = json;

    while ((p = static_cast<const char*>(std::memchr(p, '"', static_cast<size_t>(end - p)))) !=
           nullptr)
    {
        ++p;
        if (static_cast<size_t>(end - p) < keyLen + 2)
            break;
        if (std::strncmp(p, key, keyLen) == 0 && p[keyLen] == '"')
        {
            p += keyLen + 1;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end || *p != ':')
                continue;
            ++p;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end)
                continue;

            // Parse integer (possibly negative)
            const char* numStart = p;
            bool negative = false;
            if (*p == '-')
            {
                negative = true;
                ++p;
            }
            if (p >= end || !std::isdigit(static_cast<unsigned char>(*p)))
                continue;
            char* endPtr = nullptr;
            long val = std::strtol(numStart, &endPtr, 10);
            if (endPtr == numStart)
                continue;
            value = static_cast<int32_t>(val);
            (void)negative; // sign is handled by strtol
            return true;
        }
        while (p < end && *p != '"')
            ++p;
    }
    return false;
}

/// Find a float value for a key.
/// Looks for `"key":<number>`.
bool MotorControllerImpl::findJsonFloat(const char* json, size_t len, const char* key, float& value)
{
    size_t keyLen = std::strlen(key);
    const char* end = json + len;
    const char* p = json;

    while ((p = static_cast<const char*>(std::memchr(p, '"', static_cast<size_t>(end - p)))) !=
           nullptr)
    {
        ++p;
        if (static_cast<size_t>(end - p) < keyLen + 2)
            break;
        if (std::strncmp(p, key, keyLen) == 0 && p[keyLen] == '"')
        {
            p += keyLen + 1;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end || *p != ':')
                continue;
            ++p;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end)
                continue;

            char* endPtr = nullptr;
            double val = std::strtod(p, &endPtr);
            if (endPtr == p)
                continue;
            value = static_cast<float>(val);
            return true;
        }
        while (p < end && *p != '"')
            ++p;
    }
    return false;
}

/// Find a boolean value for a key.
/// Looks for `"key":true` or `"key":false`.
bool MotorControllerImpl::findJsonBool(const char* json, size_t len, const char* key, bool& value)
{
    size_t keyLen = std::strlen(key);
    const char* end = json + len;
    const char* p = json;

    while ((p = static_cast<const char*>(std::memchr(p, '"', static_cast<size_t>(end - p)))) !=
           nullptr)
    {
        ++p;
        if (static_cast<size_t>(end - p) < keyLen + 2)
            break;
        if (std::strncmp(p, key, keyLen) == 0 && p[keyLen] == '"')
        {
            p += keyLen + 1;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end || *p != ':')
                continue;
            ++p;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
                ++p;
            if (p >= end)
                continue;

            if (static_cast<size_t>(end - p) >= 4 && std::strncmp(p, "true", 4) == 0)
            {
                value = true;
                return true;
            }
            if (static_cast<size_t>(end - p) >= 5 && std::strncmp(p, "false", 5) == 0)
            {
                value = false;
                return true;
            }
        }
        while (p < end && *p != '"')
            ++p;
    }
    return false;
}

// ============================================================================
// Command dispatching — maps JSON command strings to MotorCommand structs
// ============================================================================

bool MotorControllerImpl::sendCommand(const char* cmdJson, size_t len)
{
    // Extract the "cmd" value
    size_t cmdLen = 0;
    const char* cmdVal = findJsonString(cmdJson, len, "cmd", &cmdLen);
    if (!cmdVal || cmdLen == 0)
        return false;

    // Build a null-terminated command name for comparison
    char cmdBuf[32];
    if (cmdLen > sizeof(cmdBuf) - 1)
        return false;
    std::memcpy(cmdBuf, cmdVal, cmdLen);
    cmdBuf[cmdLen] = '\0';

    // --- Begin: construct MotorCommand based on cmd string ---
    MotorCommand cmd{};
    cmd.steps = 0;
    cmd.direction = domain::Direction::LiqIn;
    cmd.speedHz = domain::gSpeed.load(std::memory_order_acquire);
    cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
    cmd.stallThreshold = 0;

    // Helper: read global current direction
    auto currentDir = domain::gDirection.load(std::memory_order_acquire);

    if (std::strcmp(cmdBuf, "stop") == 0)
    {
        cmd.type = MotorCommandType::Stop;
    }
    else if (std::strcmp(cmdBuf, "emergencyStop") == 0)
    {
        cmd.type = MotorCommandType::EmergencyStop;
    }
    else if (std::strcmp(cmdBuf, "home") == 0)
    {
        cmd.type = MotorCommandType::Home;
    }
    else if (std::strcmp(cmdBuf, "setDirection") == 0)
    {
        cmd.type = MotorCommandType::SetDirection;
        size_t dirLen = 0;
        auto dirVal = findJsonString(cmdJson, len, "direction", &dirLen);
        if (!dirVal)
            return false;
        if (dirLen >= 5 && std::strncmp(dirVal, "liqIn", 5) == 0)
        {
            cmd.direction = domain::Direction::LiqIn;
        }
        else if (dirLen >= 6 && std::strncmp(dirVal, "liqOut", 6) == 0)
        {
            cmd.direction = domain::Direction::LiqOut;
        }
        else
        {
            return false;
        }
    }
    else if (std::strcmp(cmdBuf, "setSpeed") == 0)
    {
        cmd.type = MotorCommandType::SetSpeed;
        int32_t speed = 0;
        if (!findJsonInt(cmdJson, len, "speedHz", speed))
            return false;
        if (speed < static_cast<int32_t>(config::STEP_FREQ_MIN_HZ) ||
            speed > static_cast<int32_t>(config::STEP_FREQ_MAX_HZ))
        {
            return false;
        }
        cmd.speedHz = static_cast<uint32_t>(speed);
    }
    else if (std::strcmp(cmdBuf, "setAccel") == 0)
    {
        cmd.type = MotorCommandType::SetAccel;
        int32_t accel = 0;
        if (!findJsonInt(cmdJson, len, "accelHzPerS", accel))
            return false;
        cmd.accelHzPerS = static_cast<uint32_t>(accel);
    }
    else if (std::strcmp(cmdBuf, "moveSteps") == 0)
    {
        cmd.type = MotorCommandType::MoveSteps;
        int32_t steps = 0;
        if (!findJsonInt(cmdJson, len, "steps", steps))
            return false;
        cmd.steps = steps;
        // Optional direction override
        size_t dirLen = 0;
        auto dirVal = findJsonString(cmdJson, len, "direction", &dirLen);
        if (dirVal)
        {
            if (dirLen >= 5 && std::strncmp(dirVal, "liqIn", 5) == 0)
            {
                cmd.direction = domain::Direction::LiqIn;
            }
            else if (dirLen >= 6 && std::strncmp(dirVal, "liqOut", 6) == 0)
            {
                cmd.direction = domain::Direction::LiqOut;
            }
            else
            {
                return false;
            }
        }
        else
        {
            cmd.direction = currentDir;
        }
        // Optional speed override
        int32_t speed = 0;
        if (findJsonInt(cmdJson, len, "speedHz", speed))
        {
            cmd.speedHz = static_cast<uint32_t>(speed);
        }
        // Optional accel override
        int32_t accel = 0;
        if (findJsonInt(cmdJson, len, "accelHzPerS", accel))
        {
            cmd.accelHzPerS = static_cast<uint32_t>(accel);
        }
    }
    else if (std::strcmp(cmdBuf, "setStallThreshold") == 0)
    {
        cmd.type = MotorCommandType::SetStallThreshold;
        int32_t threshold = 0;
        if (!findJsonInt(cmdJson, len, "threshold", threshold))
            return false;
        cmd.stallThreshold = static_cast<uint8_t>(threshold);
    }
    else
    {
        // Unknown command — not handled by this interface
        return false;
    }

    // Send to queue
    QueueHandle_t q = getCmdQueue();
    if (q == nullptr)
        return false;
    return xQueueSend(q, &cmd, 0) == pdTRUE;
}

// ============================================================================
// TMC register access — delegates to global gTmcUart
// ============================================================================

bool MotorControllerImpl::readTmcRegister(uint8_t reg, uint32_t& value)
{
    // Note: readTmcRegister runs in the caller's context (usually the main loop
    // or HTTP handler). This is safe because:
    // 1. TMC register reads are non-destructive
    // 2. The only write-path is SetStallThreshold (infrequent, serialized through motor queue)
    // 3. Half-duplex UART contention is theoretically possible but of negligible probability
    // TODO: Full serialization requires routing reads through motor command queue
    //       paired with a result-callback mechanism (deferred to MotorCommand domain migration)
    return gTmcUart.readRegister(reg, value);
}

// ============================================================================
// Result helpers
// ============================================================================

std::optional<domain::SmResult> MotorControllerImpl::peekResult()
{
    QueueHandle_t q = getResultQueue();
    if (q == nullptr)
        return std::nullopt;

    domain::SmResult result{};
    result.type = domain::SmResult::Type::None;
    result.stepsTaken = 0;
    result.measuredSpeedMlMin = 0.0f;
    result.resultCount = 0;

    // Non-blocking peek
    // Non-destructive — caller must xQueueReceive separately to consume
    if (xQueuePeek(q, &result, 0) == pdTRUE)
    {
        return result;
    }
    return std::nullopt;
}

std::optional<domain::SmResult> MotorControllerImpl::waitResult(uint32_t timeoutMs)
{
    QueueHandle_t q = getResultQueue();
    if (q == nullptr)
        return std::nullopt;

    domain::SmResult result{};
    result.type = domain::SmResult::Type::None;
    result.stepsTaken = 0;
    result.measuredSpeedMlMin = 0.0f;
    result.resultCount = 0;

    if (xQueueReceive(q, &result, pdMS_TO_TICKS(timeoutMs)) == pdTRUE)
    {
        return result;
    }
    return std::nullopt;
}

} // namespace ecotiter::infrastructure
