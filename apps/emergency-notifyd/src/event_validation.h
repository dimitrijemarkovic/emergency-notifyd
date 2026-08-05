#pragma once

#include <libubox/blobmsg.h>

typedef enum {
    EVENT_VALIDATION_OK = 0,
    EVENT_VALIDATION_MISSING_PAYLOAD,
    EVENT_VALIDATION_MISSING_FIELD,
    EVENT_VALIDATION_BAD_SEVERITY,
    EVENT_VALIDATION_TYPE_MISMATCH,
} event_validation_status_t;

typedef struct {
    const char *type;
    const char *severity;
    const char *source;
    const char *message;
} alarm_event_payload_t;

/* Parsira blobmsg payload i validira ga u odnosu na ime eventa (npr. "alarm.fire"). */
event_validation_status_t event_validation_check(const char *event_name,
                                                 struct blob_attr *msg,
                                                 alarm_event_payload_t *out);

const char *event_validation_status_str(event_validation_status_t status);
