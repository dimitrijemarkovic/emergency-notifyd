#include "led_action.h"

#include <stdint.h>
#include <string.h>

#include <libubox/blobmsg.h>

#include "emergency/log.h"

#define LED_OBJECT_NAME "emergency.led"
#define LED_ALARM_METHOD "alarm"
#define LED_CANCEL_METHOD "cancel_alarm"

#define LED_DURATION_SEC 10

#define LED_PATTERN_RED_FAST 1
#define LED_PATTERN_RED_SOLID 2
#define LED_PATTERN_BLUE_FAST 3
#define LED_PATTERN_BLUE_SLOW 4

struct led_action_config {
    int valid;
    int led_id;
    int duration_sec;
    const char *pattern_name;
};

static struct led_action_config make_invalid_config(void)
{
    return (struct led_action_config) {
        .valid = 0,
        .led_id = 0,
        .duration_sec = 0,
        .pattern_name = "none",
    };
}

static struct led_action_config resolve_led_action_config(const char *event_type)
{
    if (!event_type) {
        return make_invalid_config();
    }

    if (strcmp(event_type, "alarm.fire") == 0) {
        return (struct led_action_config) {
            .valid = 1,
            .led_id = LED_PATTERN_RED_FAST,
            .duration_sec = LED_DURATION_SEC,
            .pattern_name = "red_fast",
        };
    }

    if (strcmp(event_type, "alarm.burglary") == 0) {
        return (struct led_action_config) {
            .valid = 1,
            .led_id = LED_PATTERN_RED_SOLID,
            .duration_sec = LED_DURATION_SEC,
            .pattern_name = "red_solid",
        };
    }

    if (strcmp(event_type, "alarm.panic") == 0) {
        return (struct led_action_config) {
            .valid = 1,
            .led_id = LED_PATTERN_RED_SOLID,
            .duration_sec = LED_DURATION_SEC,
            .pattern_name = "red_solid",
        };
    }

    if (strcmp(event_type, "alarm.water_leak") == 0) {
        return (struct led_action_config) {
            .valid = 1,
            .led_id = LED_PATTERN_BLUE_FAST,
            .duration_sec = LED_DURATION_SEC,
            .pattern_name = "blue_fast",
        };
    }

    if (strcmp(event_type, "alarm.medical") == 0) {
        return (struct led_action_config) {
            .valid = 1,
            .led_id = LED_PATTERN_BLUE_SLOW,
            .duration_sec = LED_DURATION_SEC,
            .pattern_name = "blue_slow",
        };
    }

    return make_invalid_config();
}

static int lookup_led_object(struct ubus_context *ctx, uint32_t *object_id)
{
    int ret;

    ret = ubus_lookup_id(ctx, LED_OBJECT_NAME, object_id);
    if (ret) {
        emergency_log_error("led action failed: object '%s' not found: %s",
                           LED_OBJECT_NAME,
                           ubus_strerror(ret));
        return ret;
    }

    return 0;
}

static int cancel_led_alarm(struct ubus_context *ctx, uint32_t object_id)
{
    int ret;

    ret = ubus_invoke(ctx,
                      object_id,
                      LED_CANCEL_METHOD,
                      NULL,
                      NULL,
                      NULL,
                      3000);

    if (ret) {
        emergency_log_error("led cancel failed: %s", ubus_strerror(ret));
        return ret;
    }

    emergency_log_info("led cancel request sent");

    return 0;
}

static int call_led_alarm(struct ubus_context *ctx,
                          uint32_t object_id,
                          const struct led_action_config *config)
{
    struct blob_buf b = {0};
    int ret;

    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "led_id", config->led_id);
    blobmsg_add_u32(&b, "duration", config->duration_sec);

    ret = ubus_invoke(ctx,
                      object_id,
                      LED_ALARM_METHOD,
                      b.head,
                      NULL,
                      NULL,
                      3000);

    blob_buf_free(&b);

    if (ret) {
        emergency_log_error("led alarm request failed: %s", ubus_strerror(ret));
        return ret;
    }

    emergency_log_info("led alarm request sent: led_id=%d, duration=%d, pattern=%s",
                       config->led_id,
                       config->duration_sec,
                       config->pattern_name);

    return 0;
}

action_result_t led_action_handle_alarm_event(struct ubus_context *ctx,
                                              const char *event_type,
                                              const alarm_event_payload_t *payload)
{
    uint32_t object_id;
    struct led_action_config config;
    int ret;

    (void)payload;

    if (!ctx) {
        emergency_log_error("led action failed: missing ubus context");
        return ACTION_RESULT_ERROR;
    }

    config = resolve_led_action_config(event_type);

    if (!config.valid) {
        emergency_log_info("unsupported alarm event ignored: %s",
                          event_type ? event_type : "unknown");
        return ACTION_RESULT_IGNORED;
    }

    emergency_log_info("led action resolved: event=%s, led_id=%d, duration=%d, pattern=%s",
                       event_type,
                       config.led_id,
                       config.duration_sec,
                       config.pattern_name);

    ret = lookup_led_object(ctx, &object_id);
    if (ret) {
        return ACTION_RESULT_ERROR;
    }

    cancel_led_alarm(ctx, object_id);

    if (call_led_alarm(ctx, object_id, &config) != 0) {
        return ACTION_RESULT_ERROR;
    }

    return ACTION_RESULT_ACCEPTED;
}
