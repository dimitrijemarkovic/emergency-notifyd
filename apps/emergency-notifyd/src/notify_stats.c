#include "notify_stats.h"

#include <stdio.h>

/* emergency-notifyd is single-threaded: handle_alarm_event() and the
 * "status" ubus method both run sequentially on the same uloop, so plain
 * static fields are safe here without a mutex. Contrast with
 * emergency-siren-service/emergency-led-service, which run a separate
 * worker pthread and therefore need a mutex to protect their shared state. */
static notify_stats_t g_stats;

void notify_stats_init(void)
{
    g_stats.events_received = 0;
    g_stats.events_accepted = 0;
    g_stats.events_ignored = 0;
    g_stats.events_invalid = 0;
    g_stats.actions_sent = 0;
    g_stats.actions_failed = 0;
    snprintf(g_stats.last_event, sizeof(g_stats.last_event), "%s", "none");
    snprintf(g_stats.last_result, sizeof(g_stats.last_result), "%s", "none");
}

void notify_stats_record_event(const char *event_name,
                               alarm_event_classification_t classification)
{
    g_stats.events_received++;

    switch (classification) {
    case ALARM_EVENT_ACCEPTED:
        g_stats.events_accepted++;
        break;
    case ALARM_EVENT_IGNORED:
        g_stats.events_ignored++;
        break;
    case ALARM_EVENT_INVALID:
        g_stats.events_invalid++;
        break;
    }

    snprintf(g_stats.last_event, sizeof(g_stats.last_event),
             "%s", event_name ? event_name : "unknown");
    snprintf(g_stats.last_result, sizeof(g_stats.last_result),
             "%s", event_classification_str(classification));
}

void notify_stats_record_action_result(action_result_t result)
{
    if (result == ACTION_RESULT_ACCEPTED) {
        g_stats.actions_sent++;
    } else if (result == ACTION_RESULT_ERROR) {
        g_stats.actions_failed++;
    }
}

const notify_stats_t *notify_stats_get(void)
{
    return &g_stats;
}
