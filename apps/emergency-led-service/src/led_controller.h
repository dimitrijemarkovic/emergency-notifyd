#pragma once

#define LED_DURATION_INFINITE (-1)

typedef enum {
    LED_PATTERN_INVALID = 0,
    LED_PATTERN_RED_FAST = 1,
    LED_PATTERN_RED_SOLID = 2,
    LED_PATTERN_BLUE_FAST = 3,
    LED_PATTERN_BLUE_SLOW = 4,
} led_pattern_id_t;

int led_controller_init(void);
void led_controller_deinit(void);

int led_controller_start_by_id(int led_id, int duration_sec);
int led_controller_stop(void);

int led_controller_is_valid_id(int led_id);
const char *led_controller_pattern_name_by_id(int led_id);

int led_controller_is_running(void);
int led_controller_current_id(void);
int led_controller_current_duration(void);
const char *led_controller_current_pattern_name(void);
