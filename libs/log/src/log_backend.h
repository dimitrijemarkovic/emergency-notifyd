#pragma once

#include "emergency/log.h"

/* Always available, regardless of ENABLE_ZLOG: the default backend and the
 * fail-safe fallback used when the zlog backend fails to initialize. */
int log_backend_stdout_init(const char *service_name);
void log_backend_stdout_deinit(void);
void log_backend_stdout_write(emergency_log_level_t level, const char *message);

#if ENABLE_ZLOG
/* Only declared/compiled when ENABLE_ZLOG is set; see log_backend_zlog.c. */
int log_backend_zlog_init(const char *service_name);
void log_backend_zlog_deinit(void);
void log_backend_zlog_write(emergency_log_level_t level, const char *message);
#endif
