#include "siren_action.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libubox/blobmsg.h>

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
        fprintf(stderr,
                "[emergency-notifyd] siren action failed: object '%s' not found: %s\n",
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
        fprintf(stderr,
                "[emergency-notifyd] siren cancel failed: %s\n",
                ubus_strerror(ret));
        return ret;
    }

    fprintf(stdout, "[emergency-notifyd] siren cancel request sent\n");
    fflush(stdout);

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
        fprintf(stderr,
                "[emergency-notifyd] siren alarm request failed: %s\n",
                ubus_strerror(ret));
        return ret;
    }

    fprintf(stdout,
            "[emergency-notifyd] siren alarm request sent: siren_id=%d, duration=%d, pattern=%s\n",
            config->siren_id,
            config->duration_sec,
            config->pattern_name);
    fflush(stdout);

    return 0;
}

int siren_action_handle_alarm_event(struct ubus_context *ctx,
                                    const char *event_type)
{
    uint32_t object_id;
    struct siren_action_config config;
    int ret;

    if (!ctx) {
        fprintf(stderr, "[emergency-notifyd] siren action failed: missing ubus context\n");
        return -1;
    }

    config = resolve_siren_action_config(event_type);

    if (!config.valid) {
        fprintf(stdout,
                "[emergency-notifyd] unsupported alarm event ignored: %s\n",
                event_type ? event_type : "unknown");
        fflush(stdout);
        return 0;
    }

    fprintf(stdout,
            "[emergency-notifyd] siren action resolved: event=%s, siren_id=%d, duration=%d, pattern=%s\n",
            event_type,
            config.siren_id,
            config.duration_sec,
            config.pattern_name);
    fflush(stdout);

    ret = lookup_siren_object(ctx, &object_id);
    if (ret) {
        return ret;
    }

    cancel_siren_alarm(ctx, object_id);

    return call_siren_alarm(ctx, object_id, &config);
}