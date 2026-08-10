#include <signal.h>
#include <time.h>

#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>

#include "emergency/log.h"
#include "mqtt_publisher.h"

#define MQTT_OBJECT_NAME "emergency.mqtt"

/* Must describe the same address/port as mqtt_client.c's own constants --
 * kept as a separate literal here rather than shared across the ubus/hw
 * boundary, same as the rest of this codebase does not reach across that
 * boundary for internal details. */
#define MQTT_BROKER_DESC "localhost:1883"

static struct ubus_context *ctx;

/* Whether the last "alarm" call is still considered active, and for which
 * type. Local to this file's ubus handler functions only -- both "alarm"
 * and "cancel_alarm" run on the same uloop thread, so no locking is needed.
 * This tracks whether an alarm was reported, not whether it was actually
 * delivered to the broker (that distinction lives in mqtt_publisher's
 * stats) -- see research/dnevnik-odluka.md D28. */
static int has_active_alarm;
static char active_alarm_type[MQTT_RECORD_TYPE_MAX];

enum {
    ALARM_TYPE,
    ALARM_SEVERITY,
    ALARM_SOURCE,
    ALARM_MESSAGE,
    __ALARM_MAX
};

static const struct blobmsg_policy alarm_policy[__ALARM_MAX] = {
    [ALARM_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING },
    [ALARM_SEVERITY] = { .name = "severity", .type = BLOBMSG_TYPE_STRING },
    [ALARM_SOURCE] = { .name = "source", .type = BLOBMSG_TYPE_STRING },
    [ALARM_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
};

static void send_mqtt_reply(struct ubus_context *ctx,
                            struct ubus_request_data *req,
                            const char *result,
                            const char *error)
{
    struct blob_buf b = {0};
    mqtt_publisher_stats_t stats;

    mqtt_publisher_get_stats(&stats);

    blob_buf_init(&b, 0);

    if (result) {
        blobmsg_add_string(&b, "result", result);
    }

    blobmsg_add_u8(&b, "connected", stats.connected);
    blobmsg_add_string(&b, "broker", MQTT_BROKER_DESC);
    blobmsg_add_u32(&b, "published", stats.published);
    blobmsg_add_u32(&b, "failed", stats.failed);
    blobmsg_add_u32(&b, "queued", stats.queued);
    blobmsg_add_string(&b, "last_topic", stats.last_topic);
    blobmsg_add_string(&b, "last_state", stats.last_state);

    if (error) {
        blobmsg_add_string(&b, "error", error);
    }

    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
}

static int ubus_mqtt_status(struct ubus_context *ctx,
                            struct ubus_object *obj,
                            struct ubus_request_data *req,
                            const char *method,
                            struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    send_mqtt_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static int ubus_mqtt_alarm(struct ubus_context *ctx,
                           struct ubus_object *obj,
                           struct ubus_request_data *req,
                           const char *method,
                           struct blob_attr *msg)
{
    struct blob_attr *tb[__ALARM_MAX] = {0};
    mqtt_record_t record;

    (void)obj;
    (void)method;

    if (!msg) {
        send_mqtt_reply(ctx, req, "error", "missing_payload");
        return UBUS_STATUS_OK;
    }

    blobmsg_parse(alarm_policy, __ALARM_MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[ALARM_TYPE]) {
        send_mqtt_reply(ctx, req, "error", "missing_type");
        return UBUS_STATUS_OK;
    }

    /* Copy every string field out of msg here, synchronously, before this
     * handler returns -- msg is only valid for the duration of this
     * callback, but the record gets handed to the worker thread and read
     * much later. This is the second, easy-to-miss copy boundary beyond
     * alarm_event_payload_t in notifyd (see D27). */
    memset(&record, 0, sizeof(record));
    snprintf(record.type, sizeof(record.type), "%s", blobmsg_get_string(tb[ALARM_TYPE]));
    snprintf(record.severity, sizeof(record.severity), "%s",
             tb[ALARM_SEVERITY] ? blobmsg_get_string(tb[ALARM_SEVERITY]) : "unknown");
    snprintf(record.source, sizeof(record.source), "%s",
             tb[ALARM_SOURCE] ? blobmsg_get_string(tb[ALARM_SOURCE]) : "unknown");
    snprintf(record.message, sizeof(record.message), "%s",
             tb[ALARM_MESSAGE] ? blobmsg_get_string(tb[ALARM_MESSAGE]) : "");
    snprintf(record.state, sizeof(record.state), "%s", "active");
    record.timestamp = (long long)time(NULL);

    mqtt_publisher_enqueue(&record);

    has_active_alarm = 1;
    snprintf(active_alarm_type, sizeof(active_alarm_type), "%s", record.type);

    send_mqtt_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static int ubus_mqtt_cancel_alarm(struct ubus_context *ctx,
                                  struct ubus_object *obj,
                                  struct ubus_request_data *req,
                                  const char *method,
                                  struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    /* Only publish "cleared" if some alarm is actually considered active.
     * notifyd calls cancel_alarm before every new alarm (decision 4.5), so
     * without this check every alarm would also produce a spurious
     * "cleared" message right before its own "active" one. */
    if (has_active_alarm) {
        mqtt_record_t record;

        memset(&record, 0, sizeof(record));
        snprintf(record.type, sizeof(record.type), "%s", active_alarm_type);
        snprintf(record.state, sizeof(record.state), "%s", "cleared");
        record.timestamp = (long long)time(NULL);

        mqtt_publisher_enqueue(&record);

        has_active_alarm = 0;
    }

    send_mqtt_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static const struct ubus_method mqtt_methods[] = {
    UBUS_METHOD_NOARG("status", ubus_mqtt_status),
    UBUS_METHOD("alarm", ubus_mqtt_alarm, alarm_policy),
    UBUS_METHOD_NOARG("cancel_alarm", ubus_mqtt_cancel_alarm),
};

static struct ubus_object_type mqtt_object_type =
    UBUS_OBJECT_TYPE(MQTT_OBJECT_NAME, mqtt_methods);

static struct ubus_object mqtt_object = {
    .name = MQTT_OBJECT_NAME,
    .type = &mqtt_object_type,
    .methods = mqtt_methods,
    .n_methods = ARRAY_SIZE(mqtt_methods),
};

static void handle_signal(int signo)
{
    (void)signo;
    uloop_end();
}

int main(void)
{
    int ret;

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    emergency_log_init("mqtt");

    if (mqtt_publisher_init() != 0) {
        emergency_log_error("mqtt publisher init failed");
        emergency_log_deinit();
        return 1;
    }

    uloop_init();

    ctx = ubus_connect(NULL);
    if (!ctx) {
        emergency_log_error("ubus connect failed");
        mqtt_publisher_deinit();
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    ubus_add_uloop(ctx);

    ret = ubus_add_object(ctx, &mqtt_object);
    if (ret) {
        emergency_log_error("ubus add object failed: %s", ubus_strerror(ret));
        ubus_free(ctx);
        mqtt_publisher_deinit();
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    emergency_log_info("emergency-mqtt-service started");
    emergency_log_info("registered ubus object: %s", MQTT_OBJECT_NAME);

    uloop_run();

    mqtt_publisher_deinit();
    ubus_free(ctx);
    uloop_done();
    emergency_log_deinit();

    return 0;
}
