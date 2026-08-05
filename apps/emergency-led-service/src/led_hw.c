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

/* Not yet verified on the target board: an earlier LED implementation for this
 * hardware reads this path, while its accompanying documentation names
 * /tmp/brightness instead. Kept as a single named constant rather than guessed
 * per call site, so that correcting it touches exactly one line. */
#define LED_AMBIENT_BRIGHTNESS_PATH "/sys/class/backlight/backlight-lcd/brightness"

#define LED_BRIGHTNESS_FULL 255
#define LED_BRIGHTNESS_DIM 10
#define LED_AMBIENT_THRESHOLD 50

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
    led_hw_all_off();

    emergency_log_info("hardware initialized");

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
        /* Fail-safe fallback (D24/D9): stay fully visible rather than dim
         * when we cannot read ambient brightness at all. */
        return LED_BRIGHTNESS_FULL;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return LED_BRIGHTNESS_FULL;
    }

    buf[n] = '\0';
    screen_brightness = atoi(buf);

    return (screen_brightness > LED_AMBIENT_THRESHOLD) ? LED_BRIGHTNESS_FULL : LED_BRIGHTNESS_DIM;
}
