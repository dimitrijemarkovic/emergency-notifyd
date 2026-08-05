#include "siren_action.h"

#include <stdint.h>
#include <string.h>

#include <libubox/blobmsg.h>

#include "emergency/log.h"

#define SIREN_OBJECT_NAME "emergency.siren"
#define SIREN_ALARM_METHOD "alarm"
#define SIREN_CANCEL_METHOD "cancel_alarm"

#define SIREN_DURATION_SEC 10

#define SIREN_PATTERN_TEMPORAL_3 2
#define SIREN_PATTERN_TEMPORAL_4 3
#define SIREN_PATTERN_PANIC_PULSE 4
#define SIREN_PATTERN_MEDICAL_SLOW_PULSE 5

struct siren_action_config {
    int valid;
    int siren_id;
    int duration_sec;
    const char *pattern_name;
};

static struct siren_action_config make_invalid_config(void)
{
    return (struct siren_action_config) {
        .valid = 0,
        .siren_id = 0,
        .duration_sec = 0,
        .pattern_name = "none",
    };
}

static struct siren_action_config resolve_siren_action_config(const char *event_type)
{
    if (!event_type) {
        return make_invalid_config();
    }

    if (strcmp(event_type, "alarm.fire") == 0) {
        return (struct siren_action_config) {
            .valid = 1,
            .siren_id = SIREN_PATTERN_TEMPORAL_3,
            .duration_sec = SIREN_DURATION_SEC,
            .pattern_name = "temporal_3",
        };
    }

    if (strcmp(event_type, "alarm.burglary") == 0) {
        return (struct siren_action_config) {
            .valid = 1,
            .siren_id = SIREN_PATTERN_PANIC_PULSE,
            .duration_sec = SIREN_DURATION_SEC,
            .pattern_name = "panic_pulse",
        };
    }

    if (strcmp(event_type, "alarm.panic") == 0) {
        return (struct siren_action_config) {
            .valid = 1,
            .siren_id = SIREN_PATTERN_PANIC_PULSE,
            .duration_sec = SIREN_DURATION_SEC,
            .pattern_name = "panic_pulse",
        };
    }

    if (strcmp(event_type, "alarm.medical") == 0) {
        return (struct siren_action_config) {
            .valid = 1,
            .siren_id = SIREN_PATTERN_MEDICAL_SLOW_PULSE,
            .duration_sec = SIREN_DURATION_SEC,
            .pattern_name = "medical_slow_pulse",
        };
    }

    if (strcmp(event_type, "alarm.water_leak") == 0) {
        return (struct siren_action_config) {
            .valid = 1,
            .siren_id = SIREN_PATTERN_TEMPORAL_4,
            .duration_sec = SIREN_DURATION_SEC,
            .pattern_name = "temporal_4",
        };
    }

    return make_invalid_config();
}

static int lookup_siren_object(struct ubus_context *ctx, uint32_t *object_id)
{
    int ret;

    ret = ubus_lookup_id(ctx, SIREN_OBJECT_NAME, object_id);
    if (ret) {
        emergency_log_error("siren action failed: object '%s' not found: %s",
                           SIREN_OBJECT_NAME,
                           ubus_strerror(ret));
        return ret;
    }

    return 0;
}

static int cancel_siren_alarm(struct ubus_context *ctx, uint32_t object_id)
{
    int ret;

    ret = ubus_invoke(ctx,
                      object_id,
                      SIREN_CANCEL_METHOD,
                      NULL,
                      NULL,
                      NULL,
                      3000);

    if (ret) {
        emergency_log_error("siren cancel failed: %s", ubus_strerror(ret));
        return ret;
    }

    emergency_log_info("siren cancel request sent");

    return 0;
}

static int call_siren_alarm(struct ubus_context *ctx,
                            uint32_t object_id,
                            const struct siren_action_config *config)
{
    struct blob_buf b = {0};
    int ret;

    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "siren_id", config->siren_id);
    blobmsg_add_u32(&b, "duration", config->duration_sec);

    ret = ubus_invoke(ctx,
                      object_id,
                      SIREN_ALARM_METHOD,
                      b.head,
                      NULL,
                      NULL,
                      3000);

    blob_buf_free(&b);

    if (ret) {
        emergency_log_error("siren alarm request failed: %s", ubus_strerror(ret));
        return ret;
    }

    emergency_log_info("siren alarm request sent: siren_id=%d, duration=%d, pattern=%s",
                       config->siren_id,
                       config->duration_sec,
                       config->pattern_name);

    return 0;
}

action_result_t siren_action_handle_alarm_event(struct ubus_context *ctx,
                                                const char *event_type)
{
    uint32_t object_id;
    struct siren_action_config config;
    int ret;

    if (!ctx) {
        emergency_log_error("siren action failed: missing ubus context");
        return ACTION_RESULT_ERROR;
    }

    config = resolve_siren_action_config(event_type);

    if (!config.valid) {
        emergency_log_info("unsupported alarm event ignored: %s",
                          event_type ? event_type : "unknown");
        return ACTION_RESULT_IGNORED;
    }

    emergency_log_info("siren action resolved: event=%s, siren_id=%d, duration=%d, pattern=%s",
                       event_type,
                       config.siren_id,
                       config.duration_sec,
                       config.pattern_name);

    ret = lookup_siren_object(ctx, &object_id);
    if (ret) {
        return ACTION_RESULT_ERROR;
    }

    cancel_siren_alarm(ctx, object_id);

    if (call_siren_alarm(ctx, object_id, &config) != 0) {
        return ACTION_RESULT_ERROR;
    }

    return ACTION_RESULT_ACCEPTED;
}