#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define EMERGENCY_LOG_PRINTF_ATTR __attribute__((format(printf, 2, 3)))
#else
#define EMERGENCY_LOG_PRINTF_ATTR
#endif

typedef enum {
    EMERGENCY_LOG_DEBUG,
    EMERGENCY_LOG_INFO,
    EMERGENCY_LOG_WARN,
    EMERGENCY_LOG_ERROR,
} emergency_log_level_t;

/* service_name is used as the log category/prefix (e.g. "notifyd", "siren").
 * Returns 0 on success. On backend failure, logging automatically falls back
 * to stdout and this returns != 0 — logging must never be able to disable
 * itself or take the caller down with it. */
int emergency_log_init(const char *service_name);
void emergency_log_deinit(void);

void emergency_log(emergency_log_level_t level, const char *fmt, ...) EMERGENCY_LOG_PRINTF_ATTR;

#define emergency_log_debug(...) emergency_log(EMERGENCY_LOG_DEBUG, __VA_ARGS__)
#define emergency_log_info(...)  emergency_log(EMERGENCY_LOG_INFO,  __VA_ARGS__)
#define emergency_log_warn(...)  emergency_log(EMERGENCY_LOG_WARN,  __VA_ARGS__)
#define emergency_log_error(...) emergency_log(EMERGENCY_LOG_ERROR, __VA_ARGS__)
