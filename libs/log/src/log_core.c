#include "emergency/log.h"
#include "log_backend.h"

#include <stdarg.h>
#include <stdio.h>

#if ENABLE_ZLOG
static int using_zlog = 0;
#endif

static int backend_ready = 0;

int emergency_log_init(const char *service_name)
{
#if ENABLE_ZLOG
    if (log_backend_zlog_init(service_name) == 0) {
        using_zlog = 1;
        backend_ready = 1;
        return 0;
    }

    fprintf(stderr,
            "[%s] zlog backend init failed, falling back to stdout logging\n",
            service_name ? service_name : "unknown");
    using_zlog = 0;
#endif

    backend_ready = (log_backend_stdout_init(service_name) == 0);
    return backend_ready ? 0 : -1;
}

void emergency_log_deinit(void)
{
    if (backend_ready) {
#if ENABLE_ZLOG
        if (using_zlog) {
            log_backend_zlog_deinit();
        } else
#endif
        {
            log_backend_stdout_deinit();
        }
    }

    backend_ready = 0;
#if ENABLE_ZLOG
    using_zlog = 0;
#endif
}

void emergency_log(emergency_log_level_t level, const char *fmt, ...)
{
    char message[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    if (!backend_ready) {
        /* emergency_log_init() was never called, or every backend failed:
         * still write the message out instead of silently dropping it. */
        log_backend_stdout_write(level, message);
        return;
    }

#if ENABLE_ZLOG
    if (using_zlog) {
        log_backend_zlog_write(level, message);
        return;
    }
#endif

    log_backend_stdout_write(level, message);
}
