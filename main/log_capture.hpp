#pragma once

#include <cstdarg>

int logVprintf(const char* fmt, va_list args);

namespace ecotiter::domain
{
struct LogEntry;
}

void wsLogCallback(const ecotiter::domain::LogEntry& entry);
