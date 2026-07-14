#pragma once

#include <cstddef>
#include <cstring>
#include <cstdlib>

namespace ecotiter::domain {

/// Extract a quoted string value from a simple JSON object.
/// Searches for `"fieldname"` then expects `: "value"` pattern.
/// Returns malloc'd string (caller must free) or nullptr on failure.
/// Does NOT handle escaped quotes inside values.
[[nodiscard]] inline char* findJsonField(const char* json, const char* field) {
    auto* pos = std::strstr(json, field);
    if (!pos) return nullptr;
    pos += std::strlen(field);
    while (*pos == ' ') ++pos;
    if (*pos != ':') return nullptr;
    ++pos;
    while (*pos == ' ') ++pos;
    if (*pos != '"') return nullptr;
    ++pos;
    auto* end = std::strchr(pos, '"');
    if (!end) return nullptr;
    auto* val = static_cast<char*>(malloc(static_cast<size_t>(end - pos) + 1));
    if (!val) return nullptr;
    std::memcpy(val, pos, static_cast<size_t>(end - pos));
    val[end - pos] = '\0';
    return val;
}

} // namespace ecotiter::domain
