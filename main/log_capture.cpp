#include "log_capture.hpp"

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "domain/log_buffer.hpp"
#include "infrastructure/config.hpp"

#include "net_owner.hpp"

int logVprintf(const char* fmt, va_list args)
{
    char buf[ecotiter::config::WS_BUF_SIZE];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0 && static_cast<size_t>(n) < sizeof(buf))
    {
        const char* level = "INFO";
        if (buf[0] == 'W' && buf[1] == ' ')
            level = "WARN";
        else if (buf[0] == 'E' && buf[1] == ' ')
            level = "ERROR";
        else if (buf[0] == 'D' && buf[1] == ' ')
            level = "DEBUG";
        uint32_t ts = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ecotiter::domain::LogBuffer::instance().push(ts, level, buf);
    }
    if (n > 0)
    {
        fwrite(buf, 1, static_cast<size_t>(n), stdout);
        fflush(stdout);
    }
    return n;
}

void wsLogCallback(const ecotiter::domain::LogEntry& entry)
{
    char buf[ecotiter::config::WS_BUF_SIZE];
    int n = std::snprintf(buf, sizeof(buf), R"({"event":"log","data":{"level":"%s","msg":")",
                          entry.level);

    const char* src = entry.message;
    while (*src && static_cast<size_t>(n) < sizeof(buf) - 6)
    {
        if (*src == '"')
        {
            buf[n++] = '\'';
        }
        else if (*src == '\\')
        {
            buf[n++] = '/';
        }
        else if (*src == '\n')
        {
            buf[n++] = '\\';
            buf[n++] = 'n';
        }
        else if (*src == '\r')
        {
            buf[n++] = '\\';
            buf[n++] = 'r';
        }
        else if (*src == '\t')
        {
            buf[n++] = '\\';
            buf[n++] = 't';
        }
        else
        {
            buf[n++] = *src;
        }
        ++src;
    }
    buf[n] = '\0';

    n = std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), R"("}})");
    if (n < 0)
        return;
    n = static_cast<int>(std::strlen(buf));
    if (n <= 0)
        return;

    WsSendEntry wsEntry;
    size_t copyLen = static_cast<size_t>(n) > sizeof(wsEntry.data) - 1 ? sizeof(wsEntry.data) - 1
                                                                       : static_cast<size_t>(n);
    std::memcpy(wsEntry.data, buf, copyLen);
    wsEntry.data[copyLen] = '\0';
    wsEntry.len = copyLen;
    if (gWsSendQueue)
    {
        xQueueSend(gWsSendQueue, &wsEntry, 0);
    }
}
