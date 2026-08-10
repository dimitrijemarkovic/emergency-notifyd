#pragma once

#include "action_result.h"
#include "event_classification.h"

typedef struct {
    unsigned int events_received;
    unsigned int events_accepted;
    unsigned int events_ignored;
    unsigned int events_invalid;
    /* For a synchronous action (siren, led), "sent"/"failed" reflect the real
     * hardware outcome -- the ubus call only returns after the action has
     * actually happened. For an asynchronous action (mqtt), the action's own
     * ubus handler returns as soon as the message is queued, before it is
     * actually published -- these two fields then mean "dispatched", not
     * "delivered". Delivery truth for an asynchronous action lives in that
     * action's own "status" object (e.g. emergency.mqtt: published/failed),
     * not here. */
    unsigned int actions_sent;
    unsigned int actions_failed;
    char last_event[64];
    char last_result[16];
} notify_stats_t;

/* Resets counters and sets last_event/last_result to "none". Call once at startup. */
void notify_stats_init(void);

/* Records the outcome of a handled alarm.* event. Always increments
 * events_received plus exactly one of accepted/ignored/invalid, and updates
 * last_event/last_result. */
void notify_stats_record_event(const char *event_name,
                               alarm_event_classification_t classification);

/* Records the outcome of one registered action's invocation attempt (see
 * alarm_action.h). Aggregate across all actions, not broken down by name.
 * Only call this when the action was invoked and did not return
 * ACTION_RESULT_IGNORED. */
void notify_stats_record_action_result(action_result_t result);

/* Returns a read-only pointer to the current counters. */
const notify_stats_t *notify_stats_get(void);
