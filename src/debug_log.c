#include "debug_log.h"

#include <raylib.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

void debugLog(const char *scope, const char *format, ...)
{
    char timeBuffer[32];
    char messageBuffer[512];
    struct tm localTime;
    time_t now;
    const double runtimeSeconds = GetTime();
    const int runtimeMilliseconds = (int)(runtimeSeconds * 1000.0) % 1000;
    va_list args;

    now = time(NULL);
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d.%03d +%.3f",
             localTime.tm_hour,
             localTime.tm_min,
             localTime.tm_sec,
             runtimeMilliseconds,
             runtimeSeconds);

    va_start(args, format);
    vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
    va_end(args);

    fprintf(stdout, "[%s][%s] %s\n", timeBuffer, scope == NULL ? "LOG" : scope, messageBuffer);
    fflush(stdout);
}
