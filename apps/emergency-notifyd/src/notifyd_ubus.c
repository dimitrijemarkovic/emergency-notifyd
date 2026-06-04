#include <signal.h>
#include <stdio.h>

#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>

static struct ubus_context *ctx;

static void send_status_reply(struct ubus_context *ctx,
                              struct ubus_request_data *req)
{
    struct blob_buf b = {};

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "status", "running");
    blobmsg_add_string(&b, "service", "emergency-notifyd");
    blobmsg_add_string(&b, "version", "0.1.0");

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

    (void)ctx;
    (void)ev;

    if (msg) {
        json = blobmsg_format_json(msg, true);
    }

    fprintf(stdout, "[emergency-notifyd] received ubus event: %s\n", type);

    if (json) {
        fprintf(stdout, "[emergency-notifyd] payload: %s\n", json);
        free(json);
    } else {
        fprintf(stdout, "[emergency-notifyd] payload: {}\n");
    }

    fflush(stdout);
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

    uloop_init();

    ctx = ubus_connect(NULL);
    if (!ctx) {
        fprintf(stderr, "ERROR: ubus connect failed\n");
        uloop_done();
        return 1;
    }

    ubus_add_uloop(ctx);

    ret = ubus_add_object(ctx, &notify_object);
    if (ret) {
        fprintf(stderr, "ERROR: ubus add object failed: %s\n", ubus_strerror(ret));
        ubus_free(ctx);
        uloop_done();
        return 1;
    }

    ret = ubus_register_event_handler(ctx, &alarm_event_handler, "alarm.*");
    if (ret) {
        fprintf(stderr, "ERROR: ubus register event handler failed: %s\n", ubus_strerror(ret));
        ubus_free(ctx);
        uloop_done();
        return 1;
    }

    fprintf(stdout, "emergency-notifyd ubus service started\n");
    fprintf(stdout, "registered ubus object: emergency.notify\n");
    fprintf(stdout, "listening for ubus events: alarm.*\n");
    fflush(stdout);

    uloop_run();

    ubus_free(ctx);
    uloop_done();

    return 0;
}