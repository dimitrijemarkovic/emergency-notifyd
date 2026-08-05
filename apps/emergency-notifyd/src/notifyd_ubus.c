#include <signal.h>
#include <stdio.h>

#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>
#include "alarm_action.h"
#include "emergency/log.h"
#include "event_classification.h"
#include "event_validation.h"
#include "notify_stats.h"

static struct ubus_context *ctx;

static void send_status_reply(struct ubus_context *ctx,
                              struct ubus_request_data *req)
{
    struct blob_buf b = {};
    const notify_stats_t *stats = notify_stats_get();

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "status", "running");
    blobmsg_add_string(&b, "service", "emergency-notifyd");
    blobmsg_add_string(&b, "version", "0.1.0");
    blobmsg_add_u32(&b, "events_received", stats->events_received);
    blobmsg_add_u32(&b, "events_accepted", stats->events_accepted);
    blobmsg_add_u32(&b, "events_ignored", stats->events_ignored);
    blobmsg_add_u32(&b, "events_invalid", stats->events_invalid);
    blobmsg_add_u32(&b, "actions_sent", stats->actions_sent);
    blobmsg_add_u32(&b, "actions_failed", stats->actions_failed);
    blobmsg_add_string(&b, "last_event", stats->last_event);
    blobmsg_add_string(&b, "last_result", stats->last_result);

    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
}

static int ubus_notify_status(struct ubus_context *ctx,
                              struct ubus_object *obj,
                              struct ubus_request_data *req,
                              const char *method,
                              struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    send_status_reply(ctx, req);
    return UBUS_STATUS_OK;
}

static const struct ubus_method notify_methods[] = {
    UBUS_METHOD_NOARG("status", ubus_notify_status),
};

static struct ubus_object_type notify_object_type =
    UBUS_OBJECT_TYPE("emergency.notify", notify_methods);

static struct ubus_object notify_object = {
    .name = "emergency.notify",
    .type = &notify_object_type,
    .methods = notify_methods,
    .n_methods = ARRAY_SIZE(notify_methods),
};

static void handle_alarm_event(struct ubus_context *ctx,
                               struct ubus_event_handler *ev,
                               const char *type,
                               struct blob_attr *msg)
{
    char *json = NULL;
    alarm_event_payload_t payload;
    event_validation_status_t validation_status;
    alarm_event_classification_t classification;

    (void)ev;

    if (msg) {
        json = blobmsg_format_json(msg, true);
    }

    validation_status = event_validation_check(type, msg, &payload);

    if (validation_status != EVENT_VALIDATION_OK) {
        classification = ALARM_EVENT_INVALID;
        emergency_log_info("event rejected: event=%s reason=%s",
                           type ? type : "unknown",
                           event_validation_status_str(validation_status));
    } else {
        size_t i;
        int any_recognized = 0;

        for (i = 0; i < alarm_actions_count; i++) {
            action_result_t result = alarm_actions[i].handle(ctx, type);

            if (result != ACTION_RESULT_IGNORED) {
                /* ACTION_RESULT_ERROR is a transport-level failure, not a
                 * validation outcome; the event itself was recognized by this
                 * action, so it still counts as "recognized" for the overall
                 * classification below (extends D5 to multiple actions). The
                 * failure is logged and counted separately (actions_failed). */
                notify_stats_record_action_result(result);
                any_recognized = 1;
            }

            if (result == ACTION_RESULT_ERROR) {
                emergency_log_error("%s action failed for event: %s",
                                   alarm_actions[i].name,
                                   type ? type : "unknown");
            }
        }

        classification = any_recognized ? ALARM_EVENT_ACCEPTED : ALARM_EVENT_IGNORED;
    }

    notify_stats_record_event(type, classification);

    emergency_log_info("event classified: event=%s result=%s",
                       type ? type : "unknown",
                       event_classification_str(classification));

    if (json) {
        emergency_log_info("payload: %s", json);
        free(json);
    } else {
        emergency_log_info("payload: {}");
    }
}

static struct ubus_event_handler alarm_event_handler = {
    .cb = handle_alarm_event,
};

static void handle_signal(int signo)
{
    (void)signo;
    uloop_end();
}

int emergency_notifyd_run_ubus(void)
{
    int ret;

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    emergency_log_init("notifyd");
    notify_stats_init();

    uloop_init();

    ctx = ubus_connect(NULL);
    if (!ctx) {
        emergency_log_error("ubus connect failed");
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    ubus_add_uloop(ctx);

    ret = ubus_add_object(ctx, &notify_object);
    if (ret) {
        emergency_log_error("ubus add object failed: %s", ubus_strerror(ret));
        ubus_free(ctx);
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    ret = ubus_register_event_handler(ctx, &alarm_event_handler, "alarm.*");
    if (ret) {
        emergency_log_error("ubus register event handler failed: %s", ubus_strerror(ret));
        ubus_free(ctx);
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    emergency_log_info("emergency-notifyd ubus service started");
    emergency_log_info("registered ubus object: emergency.notify");
    emergency_log_info("listening for ubus events: alarm.*");

    uloop_run();

    ubus_free(ctx);
    uloop_done();
    emergency_log_deinit();

    return 0;
}