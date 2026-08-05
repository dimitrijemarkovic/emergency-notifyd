#pragma once

typedef enum {
    LED_CHANNEL_RED,
    LED_CHANNEL_GREEN,
    LED_CHANNEL_BLUE,
} led_channel_t;

int led_hw_init(void);
void led_hw_deinit(void);

/* brightness: 0 = off, otherwise intensity (see led_hw_ambient_brightness()). */
int led_hw_set_channel(led_channel_t channel, int brightness);
int led_hw_all_off(void);

/* Returns the brightness level to use for a lit channel, adapted to ambient
 * screen brightness (full during the day, dim at night) when that can be
 * read. If the ambient brightness path cannot be read, this returns full
 * brightness rather than dim: an alarm indicator must stay maximally
 * visible when we cannot tell whether it is day or night, not silently dim
 * itself. This never fails the caller.
 */
int led_hw_ambient_brightness(void);
