#pragma once

typedef enum {
    ALARM_EVENT_ACCEPTED,
    ALARM_EVENT_IGNORED,
    ALARM_EVENT_INVALID,
} alarm_event_classification_t;

const char *event_classification_str(alarm_event_classification_t classification);
