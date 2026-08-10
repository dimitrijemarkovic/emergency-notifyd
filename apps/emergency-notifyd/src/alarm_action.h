#pragma once

#include <stddef.h>

#include <libubus.h>

#include "action_result.h"
#include "event_validation.h"

/* payload reflects the already-validated event fields (see
 * event_validation_check()). Its pointers are only valid for the duration of
 * the call -- they point into the blobmsg buffer of the ubus event being
 * handled. An action that needs the content beyond its own synchronous
 * return must copy it, not remember the pointers. */
typedef action_result_t (*alarm_action_fn)(struct ubus_context *ctx,
                                           const char *event_type,
                                           const alarm_event_payload_t *payload);

typedef struct {
    const char *name; /* "siren", "led", "mqtt" -- used in logs and (aggregate) counters */
    alarm_action_fn handle;
} alarm_action_t;

/* Registered alarm actions, populated in alarm_action.c. Adding a new
 * reaction service is a new entry in that table, not a change here or in
 * notifyd_ubus.c. */
extern const alarm_action_t alarm_actions[];
extern const size_t alarm_actions_count;
