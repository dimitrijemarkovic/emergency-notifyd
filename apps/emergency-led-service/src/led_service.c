#include <signal.h>

#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>

#include "emergency/log.h"
#include "led_controller.h"

#define LED_OBJECT_NAME "emergency.led"

static struct ubus_context *ctx;

enum {
    ALARM_LED_ID,
    ALARM_DURATION,
    __ALARM_MAX
};

static const struct blobmsg_policy alarm_policy[__ALARM_MAX] = {
    [ALARM_LED_ID] = { .name = "led_id", .type = BLOBMSG_TYPE_INT32 },
    [ALARM_DURATION] = { .name = "duration", .type = BLOBMSG_TYPE_INT32 },
};

static const char *current_state(void)
{
    return led_controller_is_running() ? "playing" : "stopped";
}

static void send_led_reply(struct ubus_context *ctx,
                           struct ubus_request_data *req,
                           const char *result,
                           const char *error)
{
    struct blob_buf b = {0};
    int duration;

    blob_buf_init(&b, 0);

    if (result) {
        blobmsg_add_string(&b, "result", result);
    }

    blobmsg_add_string(&b, "state", current_state());
    blobmsg_add_u32(&b, "led_id", led_controller_current_id());
    blobmsg_add_string(&b, "pattern", led_controller_current_pattern_name());

    duration = led_controller_current_duration();
    if (duration == LED_DURATION_INFINITE) {
        blobmsg_add_string(&b, "duration", "infinite");
    } else {
        blobmsg_add_u32(&b, "duration", duration);
    }

    if (error) {
        blobmsg_add_string(&b, "error", error);
    }

    ubus_send_reply(ctx, req, b.head);
    blob_buf_free(&b);
}

static int ubus_led_status(struct ubus_context *ctx,
                           struct ubus_object *obj,
                           struct ubus_request_data *req,
                           const char *method,
                           struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    send_led_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static int ubus_led_alarm(struct ubus_context *ctx,
                          struct ubus_object *obj,
                          struct ubus_request_data *req,
                          const char *method,
                          struct blob_attr *msg)
{
    struct blob_attr *tb[__ALARM_MAX] = {0};
    int led_id;
    int duration_sec = LED_DURATION_INFINITE;
    int ret;

    (void)obj;
    (void)method;

    if (!msg) {
        send_led_reply(ctx, req, "error", "missing_payload");
        return UBUS_STATUS_OK;
    }

    blobmsg_parse(alarm_policy, __ALARM_MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[ALARM_LED_ID]) {
        send_led_reply(ctx, req, "error", "missing_led_id");
        return UBUS_STATUS_OK;
    }

    led_id = blobmsg_get_u32(tb[ALARM_LED_ID]);

    if (tb[ALARM_DURATION]) {
        duration_sec = blobmsg_get_u32(tb[ALARM_DURATION]);
    }

    if (!led_controller_is_valid_id(led_id)) {
        send_led_reply(ctx, req, "error", "invalid_led_id");
        return UBUS_STATUS_OK;
    }

    ret = led_controller_start_by_id(led_id, duration_sec);
    if (ret != 0) {
        send_led_reply(ctx, req, "error", "start_failed");
        return UBUS_STATUS_OK;
    }

    send_led_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static int ubus_led_cancel_alarm(struct ubus_context *ctx,
                                 struct ubus_object *obj,
                                 struct ubus_request_data *req,
                                 const char *method,
                                 struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    led_controller_stop();

    send_led_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static const struct ubus_method led_methods[] = {
    UBUS_METHOD_NOARG("status", ubus_led_status),
    UBUS_METHOD("alarm", ubus_led_alarm, alarm_policy),
    UBUS_METHOD_NOARG("cancel_alarm", ubus_led_cancel_alarm),
};

static struct ubus_object_type led_object_type =
    UBUS_OBJECT_TYPE(LED_OBJECT_NAME, led_methods);

static struct ubus_object led_object = {
    .name = LED_OBJECT_NAME,
    .type = &led_object_type,
    .methods = led_methods,
    .n_methods = ARRAY_SIZE(led_methods),
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

    emergency_log_init("led");

    if (led_controller_init() != 0) {
        emergency_log_error("led controller init failed");
        emergency_log_deinit();
        return 1;
    }

    uloop_init();

    ctx = ubus_connect(NULL);
    if (!ctx) {
        emergency_log_error("ubus connect failed");
        led_controller_deinit();
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    ubus_add_uloop(ctx);

    ret = ubus_add_object(ctx, &led_object);
    if (ret) {
        emergency_log_error("ubus add object failed: %s", ubus_strerror(ret));
        ubus_free(ctx);
        led_controller_deinit();
        uloop_done();
        emergency_log_deinit();
        return 1;
    }

    emergency_log_info("emergency-led-service started");
    emergency_log_info("registered ubus object: %s", LED_OBJECT_NAME);

    uloop_run();

    led_controller_deinit();
    ubus_free(ctx);
    uloop_done();
    emergency_log_deinit();

    return 0;
}
