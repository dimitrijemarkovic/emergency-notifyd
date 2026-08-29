#include "led_hw.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emergency/log.h"

#define LED_RED_BRIGHTNESS_PATH   "/sys/class/leds/status_led_red/brightness"
#define LED_GREEN_BRIGHTNESS_PATH "/sys/class/leds/status_led_green/brightness"
#define LED_BLUE_BRIGHTNESS_PATH  "/sys/class/leds/status_led_blue/brightness"

/* Every LED class device exposes the largest value its brightness file
 * accepts. Reading it beats assuming it: writing a larger value happens to
 * work only because the kernel driver clamps, which makes the code correct
 * for the wrong reason and silently wrong on any device with a different
 * range. */
#define LED_RED_MAX_PATH   "/sys/class/leds/status_led_red/max_brightness"
#define LED_GREEN_MAX_PATH "/sys/class/leds/status_led_green/max_brightness"
#define LED_BLUE_MAX_PATH  "/sys/class/leds/status_led_blue/max_brightness"

#define LED_AMBIENT_BRIGHTNESS_PATH "/sys/class/backlight/backlight-lcd/brightness"
#define LED_AMBIENT_MAX_PATH        "/sys/class/backlight/backlight-lcd/max_brightness"

/* Used only when the corresponding max_brightness file cannot be read. An
 * alarm indicator must keep working on an unknown device rather than refuse
 * to start, so a failed reading degrades to these instead of failing init. */
#define LED_BRIGHTNESS_FULL_FALLBACK 255
#define LED_AMBIENT_MAX_FALLBACK 100
#define LED_BRIGHTNESS_DIM_PREFERRED 10

/* Resolved once in led_hw_init() from the device itself. */
static int led_full = LED_BRIGHTNESS_FULL_FALLBACK;
static int led_dim = LED_BRIGHTNESS_DIM_PREFERRED;
static int ambient_threshold = LED_AMBIENT_MAX_FALLBACK / 2;

static int write_sysfs(const char *path, const char *value)
{
    int fd;
    ssize_t ret;

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    ret = write(fd, value, strlen(value));
    close(fd);

    if (ret < 0) {
        perror(path);
        return -1;
    }

    return 0;
}

static int read_sysfs_int(const char *path, int *value)
{
    int fd;
    char buf[32];
    ssize_t n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return -1;
    }

    buf[n] = '\0';
    *value = atoi(buf);

    return 0;
}

/* Takes the smallest of the three channel maxima. They are expected to be
 * equal, but a value written to one channel must be valid for all three,
 * so the smallest is the only safe common ceiling. */
static int read_channel_max(int *value)
{
    static const char *paths[] = {
        LED_RED_MAX_PATH,
        LED_GREEN_MAX_PATH,
        LED_BLUE_MAX_PATH,
    };
    int smallest = 0;
    int found = 0;
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int channel_max;

        if (read_sysfs_int(paths[i], &channel_max) != 0 || channel_max <= 0) {
            continue;
        }

        if (!found || channel_max < smallest) {
            smallest = channel_max;
            found = 1;
        }
    }

    if (!found) {
        return -1;
    }

    *value = smallest;

    return 0;
}

static const char *path_for_channel(led_channel_t channel)
{
    switch (channel) {
    case LED_CHANNEL_RED:
        return LED_RED_BRIGHTNESS_PATH;
    case LED_CHANNEL_GREEN:
        return LED_GREEN_BRIGHTNESS_PATH;
    case LED_CHANNEL_BLUE:
        return LED_BLUE_BRIGHTNESS_PATH;
    default:
        return NULL;
    }
}

int led_hw_init(void)
{
    int value;

    if (read_channel_max(&value) == 0) {
        led_full = value;
    } else {
        emergency_log_warn("channel max brightness unreadable, assuming %d", led_full);
    }

    /* On a device with a small range the preferred dim value can exceed the
     * maximum, in which case dim would be indistinguishable from full. */
    led_dim = (LED_BRIGHTNESS_DIM_PREFERRED < led_full)
                  ? LED_BRIGHTNESS_DIM_PREFERRED
                  : led_full;

    if (read_sysfs_int(LED_AMBIENT_MAX_PATH, &value) == 0 && value > 0) {
        ambient_threshold = value / 2;
    } else {
        emergency_log_warn("ambient max brightness unreadable, assuming threshold %d",
                           ambient_threshold);
    }

    led_hw_all_off();

    emergency_log_info("hardware initialized: channel max=%d, dim=%d, ambient threshold=%d",
                       led_full, led_dim, ambient_threshold);

    return 0;
}

void led_hw_deinit(void)
{
    led_hw_all_off();
}

int led_hw_set_channel(led_channel_t channel, int brightness)
{
    char value[16];
    const char *path = path_for_channel(channel);

    if (!path) {
        return -1;
    }

    /* Clamped here rather than left to the driver, so that what the service
     * asks for and what the device does are the same thing. */
    if (brightness > led_full) {
        brightness = led_full;
    }
    if (brightness < 0) {
        brightness = 0;
    }

    snprintf(value, sizeof(value), "%d", brightness);

    return write_sysfs(path, value);
}

int led_hw_all_off(void)
{
    int ret = 0;

    if (led_hw_set_channel(LED_CHANNEL_RED, 0) != 0) {
        ret = -1;
    }
    if (led_hw_set_channel(LED_CHANNEL_GREEN, 0) != 0) {
        ret = -1;
    }
    if (led_hw_set_channel(LED_CHANNEL_BLUE, 0) != 0) {
        ret = -1;
    }

    return ret;
}

int led_hw_ambient_brightness(void)
{
    int fd;
    char buf[16];
    ssize_t n;
    int screen_brightness;

    fd = open(LED_AMBIENT_BRIGHTNESS_PATH, O_RDONLY);
    if (fd < 0) {
        /* Fail-safe fallback: stay fully visible rather than dim when we
         * cannot read ambient brightness at all. */
        return led_full;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return led_full;
    }

    buf[n] = '\0';
    screen_brightness = atoi(buf);

    return (screen_brightness > ambient_threshold) ? led_full : led_dim;
}
