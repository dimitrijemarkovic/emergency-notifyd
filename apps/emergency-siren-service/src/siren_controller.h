#pragma once

#define SIREN_DURATION_INFINITE (-1)

typedef enum {
    SIREN_PATTERN_INVALID = 0,
    SIREN_PATTERN_STEADY = 1,
    SIREN_PATTERN_TEMPORAL_3 = 2,
    SIREN_PATTERN_TEMPORAL_4 = 3,
    SIREN_PATTERN_PANIC_PULSE = 4,
    SIREN_PATTERN_MEDICAL_SLOW_PULSE = 5
} siren_pattern_id_t;

int siren_controller_init(void);
void siren_controller_deinit(void);

int siren_controller_start_by_id(int siren_id, int duration_sec);
int siren_controller_stop(void);

int siren_controller_is_valid_id(int siren_id);
const char *siren_controller_pattern_name_by_id(int siren_id);

int siren_controller_is_running(void);
int siren_controller_current_id(void);
int siren_controller_current_duration(void);
const char *siren_controller_current_pattern_name(void);