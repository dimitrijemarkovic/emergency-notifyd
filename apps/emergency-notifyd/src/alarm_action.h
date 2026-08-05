#pragma once

#include <stddef.h>

#include <libubus.h>

#include "action_result.h"

typedef struct {
    const char *name; /* "siren", "led" -- used in logs and (aggregate) counters */
    action_result_t (*handle)(struct ubus_context *ctx, const char *event_type);
} alarm_action_t;

/* Registered alarm actions, populated in alarm_action.c. Adding a new
 * reaction service is a new entry in that table, not a change here or in
 * notifyd_ubus.c. */
extern const alarm_action_t alarm_actions[];
extern const size_t alarm_actions_count;
