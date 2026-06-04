#include <signal.h>
#include <stdio.h>

#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>

#include "siren_controller.h"

#define SIREN_OBJECT_NAME "emergency.siren"

static struct ubus_context *ctx;

enum {
    ALARM_SIREN_ID,
    ALARM_DURATION,
    __ALARM_MAX
};

static const struct blobmsg_policy alarm_policy[__ALARM_MAX] = {
    [ALARM_SIREN_ID] = { .name = "siren_id", .type = BLOBMSG_TYPE_INT32 },
    [ALARM_DURATION] = { .name = "duration", .type = BLOBMSG_TYPE_INT32 },
};

static const char *current_state(void)
{
    return siren_controller_is_running() ? "playing" : "stopped";
}

static void send_siren_reply(struct ubus_context *ctx,
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
    blobmsg_add_u32(&b, "siren_id", siren_controller_current_id());
    blobmsg_add_string(&b, "pattern", siren_controller_current_pattern_name());

    duration = siren_controller_current_duration();
    if (duration == SIREN_DURATION_INFINITE) {
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

static int ubus_siren_status(struct ubus_context *ctx,
                             struct ubus_object *obj,
                             struct ubus_request_data *req,
                             const char *method,
                             struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    send_siren_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static int ubus_siren_alarm(struct ubus_context *ctx,
                            struct ubus_object *obj,
                            struct ubus_request_data *req,
                            const char *method,
                            struct blob_attr *msg)
{
    struct blob_attr *tb[__ALARM_MAX] = {0};
    int siren_id;
    int duration_sec = SIREN_DURATION_INFINITE;
    int ret;

    (void)obj;
    (void)method;

    if (!msg) {
        send_siren_reply(ctx, req, "error", "missing_payload");
        return UBUS_STATUS_OK;
    }

    blobmsg_parse(alarm_policy, __ALARM_MAX, tb, blob_data(msg), blob_len(msg));

    if (!tb[ALARM_SIREN_ID]) {
        send_siren_reply(ctx, req, "error", "missing_siren_id");
        return UBUS_STATUS_OK;
    }

    siren_id = blobmsg_get_u32(tb[ALARM_SIREN_ID]);

    if (tb[ALARM_DURATION]) {
        duration_sec = blobmsg_get_u32(tb[ALARM_DURATION]);
    }

    if (!siren_controller_is_valid_id(siren_id)) {
        send_siren_reply(ctx, req, "error", "invalid_siren_id");
        return UBUS_STATUS_OK;
    }

    ret = siren_controller_start_by_id(siren_id, duration_sec);
    if (ret != 0) {
        send_siren_reply(ctx, req, "error", "start_failed");
        return UBUS_STATUS_OK;
    }

    send_siren_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static int ubus_siren_cancel_alarm(struct ubus_context *ctx,
                                   struct ubus_object *obj,
                                   struct ubus_request_data *req,
                                   const char *method,
                                   struct blob_attr *msg)
{
    (void)obj;
    (void)method;
    (void)msg;

    siren_controller_stop();

    send_siren_reply(ctx, req, "ok", NULL);
    return UBUS_STATUS_OK;
}

static const struct ubus_method siren_methods[] = {
    UBUS_METHOD_NOARG("status", ubus_siren_status),
    UBUS_METHOD("alarm", ubus_siren_alarm, alarm_policy),
    UBUS_METHOD_NOARG("cancel_alarm", ubus_siren_cancel_alarm),
};

static struct ubus_object_type siren_object_type =
    UBUS_OBJECT_TYPE(SIREN_OBJECT_NAME, siren_methods);

static struct ubus_object siren_object = {
    .name = SIREN_OBJECT_NAME,
    .type = &siren_object_type,
    .methods = siren_methods,
    .n_methods = ARRAY_SIZE(siren_methods),
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

    if (siren_controller_init() != 0) {
        fprintf(stderr, "ERROR: siren controller init failed\n");
        return 1;
    }

    uloop_init();

    ctx = ubus_connect(NULL);
    if (!ctx) {
        fprintf(stderr, "ERROR: ubus connect failed\n");
        siren_controller_deinit();
        uloop_done();
        return 1;
    }

    ubus_add_uloop(ctx);

    ret = ubus_add_object(ctx, &siren_object);
    if (ret) {
        fprintf(stderr, "ERROR: ubus add object failed: %s\n", ubus_strerror(ret));
        ubus_free(ctx);
        siren_controller_deinit();
        uloop_done();
        return 1;
    }

    fprintf(stdout, "emergency-siren-service started\n");
    fprintf(stdout, "registered ubus object: %s\n", SIREN_OBJECT_NAME);
    fflush(stdout);

    uloop_run();

    siren_controller_deinit();
    ubus_free(ctx);
    uloop_done();

    return 0;
}