#include "log_backend.h"

#include <stdio.h>
#include <string.h>

static char g_service_name[64] = "unknown";

static const char *level_str(emergency_log_level_t level)
{
    switch (level) {
    case EMERGENCY_LOG_DEBUG:
        return "DEBUG";
    case EMERGENCY_LOG_INFO:
        return "INFO";
    case EMERGENCY_LOG_WARN:
        return "WARN";
    case EMERGENCY_LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

int log_backend_stdout_init(const char *service_name)
{
    snprintf(g_service_name, sizeof(g_service_name), "%s", service_name ? service_name : "unknown");
    return 0;
}

void log_backend_stdout_deinit(void)
{
}

void log_backend_stdout_write(emergency_log_level_t level, const char *message)
{
    FILE *stream = (level == EMERGENCY_LOG_WARN || level == EMERGENCY_LOG_ERROR) ? stderr : stdout;

    fprintf(stream, "[%s] %s: %s\n", g_service_name, level_str(level), message);
    fflush(stream);
}
