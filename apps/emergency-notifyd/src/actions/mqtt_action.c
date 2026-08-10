#include "mqtt_action.h"

#include <stdint.h>
#include <string.h>

#include <libubox/blobmsg.h>

#include "emergency/log.h"

#define MQTT_OBJECT_NAME "emergency.mqtt"
#define MQTT_ALARM_METHOD "alarm"
#define MQTT_CANCEL_METHOD "cancel_alarm"

/* Same set of supported alarm types as siren_action.c/led_action.c. Each
 * action owns its own supported-type check independently (not shared) --
 * these three lists currently happen to coincide, but nothing requires a
 * future action to support the same subset. */
static int is_supported_event_type(const char *event_type)
{
    if (!event_type) {
        return 0;
    }

    return strcmp(event_type, "alarm.fire") == 0 ||
           strcmp(event_type, "alarm.burglary") == 0 ||
           strcmp(event_type, "alarm.panic") == 0 ||
           strcmp(event_type, "alarm.medical") == 0 ||
           strcmp(event_type, "alarm.water_leak") == 0;
}

static int lookup_mqtt_object(struct ubus_context *ctx, uint32_t *object_id)
{
    int ret;

    ret = ubus_lookup_id(ctx, MQTT_OBJECT_NAME, object_id);
    if (ret) {
        emergency_log_error("mqtt action failed: object '%s' not found: %s",
                           MQTT_OBJECT_NAME,
                           ubus_strerror(ret));
        return ret;
    }

    return 0;
}

static int cancel_mqtt_alarm(struct ubus_context *ctx, uint32_t object_id)
{
    int ret;

    ret = ubus_invoke(ctx,
                      object_id,
                      MQTT_CANCEL_METHOD,
                      NULL,
                      NULL,
                      NULL,
                      3000);

    if (ret) {
        emergency_log_error("mqtt cancel failed: %s", ubus_strerror(ret));
        return ret;
    }

    emergency_log_info("mqtt cancel request sent");

    return 0;
}

/* Builds the "alarm" RPC payload from the already-validated event fields.
 * blobmsg_add_string() copies the string bytes into the blob_buf, so this is
 * safe even though payload's own pointers only live for the duration of
 * handle_alarm_event() -- the copy happens here, before that function
 * returns. No duration field: a publish is instantaneous, it does not run
 * for a length of time like the siren/LED patterns. */
static int call_mqtt_alarm(struct ubus_context *ctx,
                           uint32_t object_id,
                           const char *event_type,
                           const alarm_event_payload_t *payload)
{
    struct blob_buf b = {0};
    int ret;

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "type", payload->type ? payload->type : "unknown");
    blobmsg_add_string(&b, "severity", payload->severity ? payload->severity : "unknown");
    blobmsg_add_string(&b, "source", payload->source ? payload->source : "unknown");
    blobmsg_add_string(&b, "message", payload->message ? payload->message : "");

    ret = ubus_invoke(ctx,
                      object_id,
                      MQTT_ALARM_METHOD,
                      b.head,
                      NULL,
                      NULL,
                      3000);

    blob_buf_free(&b);

    if (ret) {
        emergency_log_error("mqtt alarm request failed: %s", ubus_strerror(ret));
        return ret;
    }

    emergency_log_info("mqtt alarm request sent: event=%s", event_type);

    return 0;
}

action_result_t mqtt_action_handle_alarm_event(struct ubus_context *ctx,
                                               const char *event_type,
                                               const alarm_event_payload_t *payload)
{
    uint32_t object_id;

    if (!ctx) {
        emergency_log_error("mqtt action failed: missing ubus context");
        return ACTION_RESULT_ERROR;
    }

    if (!is_supported_event_type(event_type)) {
        emergency_log_info("unsupported alarm event ignored: %s",
                          event_type ? event_type : "unknown");
        return ACTION_RESULT_IGNORED;
    }

    if (lookup_mqtt_object(ctx, &object_id) != 0) {
        return ACTION_RESULT_ERROR;
    }

    cancel_mqtt_alarm(ctx, object_id);

    if (call_mqtt_alarm(ctx, object_id, event_type, payload) != 0) {
        return ACTION_RESULT_ERROR;
    }

    return ACTION_RESULT_ACCEPTED;
}
