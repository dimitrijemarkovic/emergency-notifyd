#include "log_backend.h"

#if ENABLE_ZLOG

#include <zlog.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#ifndef EMERGENCY_ZLOG_CONF_PATH
#define EMERGENCY_ZLOG_CONF_PATH "/etc/emergency/zlog.conf"
#endif

/* Must match the output paths used by the rules in yocto/zlog.conf. zlog does
 * not create its own output directory, so it has to exist before zlog_init()
 * runs, whether the binary is started manually during board bring-up or under
 * systemd.
 *
 * The default points at the board's persistent data partition rather than at
 * /tmp or /var/log: on this image the root filesystem is mounted read-only,
 * /var/log is a symlink into /var/volatile, and both /tmp and /var/volatile
 * are tmpfs, so logs written there do not survive a reboot. Measured on the
 * target board: a file written under this path was still there after a
 * restart. The partition is a real systemd mount unit, which is why the
 * service units can order themselves after it with RequiresMountsFor=.
 *
 * Override at build time with -DEMERGENCY_ZLOG_LOG_DIR=... on a board whose
 * partition layout differs. Whatever it is set to, the rules in zlog.conf
 * must name the same directory. */
#ifndef EMERGENCY_ZLOG_LOG_DIR
#define EMERGENCY_ZLOG_LOG_DIR "/run/media/data-mmcblk0p9/emergency/logs"
#endif

static zlog_category_t *category;

/* Creates every missing component of path, like "mkdir -p". The production
 * path lies several levels below an existing mount point, so the earlier
 * single-level version would have failed there. Components that already exist
 * are left alone, which is what makes this safe on a read-only root: only the
 * parts under the writable partition are actually created. */
static int ensure_log_dir_exists(const char *path)
{
    char buf[PATH_MAX];
    struct stat st;
    size_t len;
    char *p;

    len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        return -1;
    }

    memcpy(buf, path, len + 1);

    /* A trailing slash would turn the last mkdir into a repeat of the one
     * before it. */
    if (buf[len - 1] == '/') {
        buf[len - 1] = '\0';
    }

    for (p = buf + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }

        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        *p = '/';
    }

    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    /* EEXIST above only says something is there, not that it is a directory. */
    if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1;
    }

    return 0;
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
