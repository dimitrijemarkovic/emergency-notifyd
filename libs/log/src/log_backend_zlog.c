#include "log_backend.h"

#if ENABLE_ZLOG

#include <zlog.h>

#include <errno.h>
#include <sys/stat.h>

#ifndef EMERGENCY_ZLOG_CONF_PATH
#define EMERGENCY_ZLOG_CONF_PATH "/etc/emergency/zlog.conf"
#endif

/* Must match the output paths used by the rules in yocto/zlog.conf. zlog does
 * not create its own output directory, so it has to exist before zlog_init()
 * runs, whether the binary is started manually (e.g. from /tmp during board
 * bring-up) or under systemd. This is a single-level mkdir,
 * sufficient for /tmp/emergency-logs since /tmp itself always exists; a
 * deeper production path would need a real mkdir -p. */
#ifndef EMERGENCY_ZLOG_LOG_DIR
#define EMERGENCY_ZLOG_LOG_DIR "/tmp/emergency-logs"
#endif

static zlog_category_t *category;

static int ensure_log_dir_exists(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }

    return -1;
}

int log_backend_zlog_init(const char *service_name)
{
    if (ensure_log_dir_exists(EMERGENCY_ZLOG_LOG_DIR) != 0) {
        return -1;
    }

    if (zlog_init(EMERGENCY_ZLOG_CONF_PATH) != 0) {
        return -1;
    }

    category = zlog_get_category(service_name ? service_name : "unknown");
    if (!category) {
        zlog_fini();
        return -1;
    }

    return 0;
}

void log_backend_zlog_deinit(void)
{
    category = NULL;
    zlog_fini();
}

void log_backend_zlog_write(emergency_log_level_t level, const char *message)
{
    if (!category) {
        return;
    }

    switch (level) {
    case EMERGENCY_LOG_DEBUG:
        zlog_debug(category, "%s", message);
        break;
    case EMERGENCY_LOG_INFO:
        zlog_info(category, "%s", message);
        break;
    case EMERGENCY_LOG_WARN:
        zlog_warn(category, "%s", message);
        break;
    case EMERGENCY_LOG_ERROR:
        zlog_error(category, "%s", message);
        break;
    }
}

#endif /* ENABLE_ZLOG */
