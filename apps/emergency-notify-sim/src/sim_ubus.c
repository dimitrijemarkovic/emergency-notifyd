#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <libubox/blobmsg.h>
#include <libubus.h>

#define NOTIFY_OBJECT_NAME "emergency.notify"
#define SIREN_OBJECT_NAME  "emergency.siren"
#define LED_OBJECT_NAME    "emergency.led"

#define UBUS_TIMEOUT_MS 3000
#define TEST_SETTLE_USEC 500000

/* Gap between sending the first and second event in the overlap test: long
 * enough that the first alarm has genuinely started (proven by the mid-test
 * status check), short enough that it is still active when the second event
 * preempts it without an explicit cancel_alarm in between. */
#define TEST_OVERLAP_GAP_USEC 200000

/* Number of registered alarm actions in notifyd's action registry
 * (apps/emergency-notifyd/src/alarm_action.c) that recognize every one of
 * the supported alarm.* types below -- both siren and led currently support
 * the exact same 5 types. Used only to predict the actions_sent delta in
 * run_stats_check(); update this if a third universally-supported action is
 * registered. */
#define SIM_ACTIONS_PER_ACCEPTED_EVENT 2

/* run_alarm_overlap_test() sends two alarm.* events directly (fire, then
 * panic) outside of the declarative test_cases table -- run_stats_check()
 * needs to account for these when predicting the events_received delta. */
#define SIM_OVERLAP_ALARM_EVENT_COUNT 2

struct sim_test_case {
    const char *name;
    const char *event_name;
    const char *type;
    const char *severity;
    const char *message;

    /* When set, the corresponding blobmsg field is omitted from the payload
     * entirely (used to exercise event_validation_check() failure paths). */
    int skip_type;
    int skip_severity;
    int skip_source;
    int skip_message;

    int expect_siren_trigger;
    int expected_siren_id;
    const char *expected_pattern;

    int expected_led_id;
    const char *expected_led_pattern;
};

struct siren_status {
    int received;
    char state[32];
    int siren_id;
    char pattern[64];
};

enum {
    STATUS_STATE,
    STATUS_SIREN_ID,
    STATUS_PATTERN,
    __STATUS_MAX
};

static const struct blobmsg_policy status_policy[__STATUS_MAX] = {
    [STATUS_STATE] = { .name = "state", .type = BLOBMSG_TYPE_STRING },
    [STATUS_SIREN_ID] = { .name = "siren_id", .type = BLOBMSG_TYPE_INT32 },
    [STATUS_PATTERN] = { .name = "pattern", .type = BLOBMSG_TYPE_STRING },
};

struct led_status {
    int received;
    char state[32];
    int led_id;
    char pattern[64];
};

enum {
    LED_STATUS_STATE,
    LED_STATUS_LED_ID,
    LED_STATUS_PATTERN,
    __LED_STATUS_MAX
};

static const struct blobmsg_policy led_status_policy[__LED_STATUS_MAX] = {
    [LED_STATUS_STATE] = { .name = "state", .type = BLOBMSG_TYPE_STRING },
    [LED_STATUS_LED_ID] = { .name = "led_id", .type = BLOBMSG_TYPE_INT32 },
    [LED_STATUS_PATTERN] = { .name = "pattern", .type = BLOBMSG_TYPE_STRING },
};

struct notify_status {
    int received;
    unsigned int events_received;
    unsigned int events_accepted;
    unsigned int events_ignored;
    unsigned int events_invalid;
    unsigned int actions_sent;
    unsigned int actions_failed;
    char last_event[64];
    char last_result[16];
};

enum {
    NOTIFY_STATUS_EVENTS_RECEIVED,
    NOTIFY_STATUS_EVENTS_ACCEPTED,
    NOTIFY_STATUS_EVENTS_IGNORED,
    NOTIFY_STATUS_EVENTS_INVALID,
    NOTIFY_STATUS_ACTIONS_SENT,
    NOTIFY_STATUS_ACTIONS_FAILED,
    NOTIFY_STATUS_LAST_EVENT,
    NOTIFY_STATUS_LAST_RESULT,
    __NOTIFY_STATUS_MAX
};

static const struct blobmsg_policy notify_status_policy[__NOTIFY_STATUS_MAX] = {
    [NOTIFY_STATUS_EVENTS_RECEIVED] = { .name = "events_received", .type = BLOBMSG_TYPE_INT32 },
    [NOTIFY_STATUS_EVENTS_ACCEPTED] = { .name = "events_accepted", .type = BLOBMSG_TYPE_INT32 },
    [NOTIFY_STATUS_EVENTS_IGNORED] = { .name = "events_ignored", .type = BLOBMSG_TYPE_INT32 },
    [NOTIFY_STATUS_EVENTS_INVALID] = { .name = "events_invalid", .type = BLOBMSG_TYPE_INT32 },
    [NOTIFY_STATUS_ACTIONS_SENT] = { .name = "actions_sent", .type = BLOBMSG_TYPE_INT32 },
    [NOTIFY_STATUS_ACTIONS_FAILED] = { .name = "actions_failed", .type = BLOBMSG_TYPE_INT32 },
    [NOTIFY_STATUS_LAST_EVENT] = { .name = "last_event", .type = BLOBMSG_TYPE_STRING },
    [NOTIFY_STATUS_LAST_RESULT] = { .name = "last_result", .type = BLOBMSG_TYPE_STRING },
};

static const struct sim_test_case test_cases[] = {
    {
        .name = "fire alarm",
        .event_name = "alarm.fire",
        .type = "fire",
        .severity = "critical",
        .message = "Simulated fire alarm",
        .expect_siren_trigger = 1,
        .expected_siren_id = 2,
        .expected_pattern = "temporal_3",
        .expected_led_id = 1,
        .expected_led_pattern = "red_fast",
    },
    {
        .name = "burglary alarm",
        .event_name = "alarm.burglary",
        .type = "burglary",
        .severity = "high",
        .message = "Simulated burglary alarm",
        .expect_siren_trigger = 1,
        .expected_siren_id = 4,
        .expected_pattern = "panic_pulse",
        .expected_led_id = 2,
        .expected_led_pattern = "red_solid",
    },
    {
        .name = "panic alarm",
        .event_name = "alarm.panic",
        .type = "panic",
        .severity = "critical",
        .message = "Simulated panic alarm",
        .expect_siren_trigger = 1,
        .expected_siren_id = 4,
        .expected_pattern = "panic_pulse",
        .expected_led_id = 2,
        .expected_led_pattern = "red_solid",
    },
    {
        .name = "water leak alarm",
        .event_name = "alarm.water_leak",
        .type = "water_leak",
        .severity = "medium",
        .message = "Simulated water leak alarm",
        .expect_siren_trigger = 1,
        .expected_siren_id = 3,
        .expected_pattern = "temporal_4",
        .expected_led_id = 3,
        .expected_led_pattern = "blue_fast",
    },
    {
        .name = "medical alarm",
        .event_name = "alarm.medical",
        .type = "medical",
        .severity = "high",
        .message = "Simulated medical alarm",
        .expect_siren_trigger = 1,
        .expected_siren_id = 5,
        .expected_pattern = "medical_slow_pulse",
        .expected_led_id = 4,
        .expected_led_pattern = "blue_slow",
    },
    {
        .name = "unsupported alarm event",
        .event_name = "alarm.invalid",
        .type = "invalid",
        .severity = "low",
        .message = "Unsupported alarm test",
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
    {
        .name = "unknown alarm event",
        .event_name = "alarm.unknown",
        .type = "unknown",
        .severity = "low",
        .message = "Unknown alarm test",
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
    {
        .name = "non-alarm event",
        .event_name = "system.low_battery",
        .type = "low_battery",
        .severity = "high",
        .message = "Non-alarm low battery test",
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
    {
        .name = "fire alarm missing severity",
        .event_name = "alarm.fire",
        .type = "fire",
        .message = "Missing severity field",
        .skip_severity = 1,
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
    {
        .name = "fire alarm bad severity value",
        .event_name = "alarm.fire",
        .type = "fire",
        .severity = "katastrofalno",
        .message = "Invalid severity value",
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
    {
        .name = "fire alarm type mismatch",
        .event_name = "alarm.fire",
        .type = "burglary",
        .severity = "critical",
        .message = "Type does not match event name",
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
    {
        .name = "fire alarm empty payload",
        .event_name = "alarm.fire",
        .skip_type = 1,
        .skip_severity = 1,
        .skip_source = 1,
        .skip_message = 1,
        .expect_siren_trigger = 0,
        .expected_siren_id = 0,
        .expected_pattern = "unknown",
        .expected_led_id = 0,
        .expected_led_pattern = "unknown",
    },
};

static void status_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
    struct siren_status *status = (struct siren_status *)req->priv;
    struct blob_attr *tb[__STATUS_MAX] = {0};
    const char *state;
    const char *pattern;

    (void)type;

    if (!status || !msg) {
        return;
    }

    blobmsg_parse(status_policy,
                  __STATUS_MAX,
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    status->received = 1;

    if (tb[STATUS_STATE]) {
        state = blobmsg_get_string(tb[STATUS_STATE]);
        snprintf(status->state, sizeof(status->state), "%s", state);
    }

    if (tb[STATUS_SIREN_ID]) {
        status->siren_id = blobmsg_get_u32(tb[STATUS_SIREN_ID]);
    }

    if (tb[STATUS_PATTERN]) {
        pattern = blobmsg_get_string(tb[STATUS_PATTERN]);
        snprintf(status->pattern, sizeof(status->pattern), "%s", pattern);
    }
}

static void led_status_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
    struct led_status *status = (struct led_status *)req->priv;
    struct blob_attr *tb[__LED_STATUS_MAX] = {0};
    const char *state;
    const char *pattern;

    (void)type;

    if (!status || !msg) {
        return;
    }

    blobmsg_parse(led_status_policy,
                  __LED_STATUS_MAX,
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    status->received = 1;

    if (tb[LED_STATUS_STATE]) {
        state = blobmsg_get_string(tb[LED_STATUS_STATE]);
        snprintf(status->state, sizeof(status->state), "%s", state);
    }

    if (tb[LED_STATUS_LED_ID]) {
        status->led_id = blobmsg_get_u32(tb[LED_STATUS_LED_ID]);
    }

    if (tb[LED_STATUS_PATTERN]) {
        pattern = blobmsg_get_string(tb[LED_STATUS_PATTERN]);
        snprintf(status->pattern, sizeof(status->pattern), "%s", pattern);
    }
}

static void notify_status_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
    struct notify_status *status = (struct notify_status *)req->priv;
    struct blob_attr *tb[__NOTIFY_STATUS_MAX] = {0};

    (void)type;

    if (!status || !msg) {
        return;
    }

    blobmsg_parse(notify_status_policy,
                  __NOTIFY_STATUS_MAX,
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    status->received = 1;

    if (tb[NOTIFY_STATUS_EVENTS_RECEIVED]) {
        status->events_received = blobmsg_get_u32(tb[NOTIFY_STATUS_EVENTS_RECEIVED]);
    }
    if (tb[NOTIFY_STATUS_EVENTS_ACCEPTED]) {
        status->events_accepted = blobmsg_get_u32(tb[NOTIFY_STATUS_EVENTS_ACCEPTED]);
    }
    if (tb[NOTIFY_STATUS_EVENTS_IGNORED]) {
        status->events_ignored = blobmsg_get_u32(tb[NOTIFY_STATUS_EVENTS_IGNORED]);
    }
    if (tb[NOTIFY_STATUS_EVENTS_INVALID]) {
        status->events_invalid = blobmsg_get_u32(tb[NOTIFY_STATUS_EVENTS_INVALID]);
    }
    if (tb[NOTIFY_STATUS_ACTIONS_SENT]) {
        status->actions_sent = blobmsg_get_u32(tb[NOTIFY_STATUS_ACTIONS_SENT]);
    }
    if (tb[NOTIFY_STATUS_ACTIONS_FAILED]) {
        status->actions_failed = blobmsg_get_u32(tb[NOTIFY_STATUS_ACTIONS_FAILED]);
    }
    if (tb[NOTIFY_STATUS_LAST_EVENT]) {
        snprintf(status->last_event, sizeof(status->last_event),
                 "%s", blobmsg_get_string(tb[NOTIFY_STATUS_LAST_EVENT]));
    }
    if (tb[NOTIFY_STATUS_LAST_RESULT]) {
        snprintf(status->last_result, sizeof(status->last_result),
                 "%s", blobmsg_get_string(tb[NOTIFY_STATUS_LAST_RESULT]));
    }
}

static int lookup_required_object(struct ubus_context *ctx, const char *object_name, uint32_t *object_id)
{
    int ret;

    ret = ubus_lookup_id(ctx, object_name, object_id);
    if (ret) {
        fprintf(stderr,
                "[ERROR] required ubus object not found: %s (%s)\n",
                object_name,
                ubus_strerror(ret));
        return ret;
    }

    fprintf(stdout, "[INFO] found ubus object: %s\n", object_name);
    return 0;
}

static int cancel_siren(struct ubus_context *ctx, uint32_t siren_object_id)
{
    int ret;

    ret = ubus_invoke(ctx,
                      siren_object_id,
                      "cancel_alarm",
                      NULL,
                      NULL,
                      NULL,
                      UBUS_TIMEOUT_MS);

    if (ret) {
        fprintf(stderr, "[ERROR] failed to cancel siren: %s\n", ubus_strerror(ret));
        return ret;
    }

    usleep(200000);
    return 0;
}

static int cancel_led(struct ubus_context *ctx, uint32_t led_object_id)
{
    int ret;

    ret = ubus_invoke(ctx,
                      led_object_id,
                      "cancel_alarm",
                      NULL,
                      NULL,
                      NULL,
                      UBUS_TIMEOUT_MS);

    if (ret) {
        fprintf(stderr, "[ERROR] failed to cancel led: %s\n", ubus_strerror(ret));
        return ret;
    }

    usleep(200000);
    return 0;
}

static int get_siren_status(struct ubus_context *ctx,
                            uint32_t siren_object_id,
                            struct siren_status *status)
{
    int ret;

    memset(status, 0, sizeof(*status));
    snprintf(status->state, sizeof(status->state), "%s", "unknown");
    snprintf(status->pattern, sizeof(status->pattern), "%s", "unknown");
    status->siren_id = -1;

    ret = ubus_invoke(ctx,
                      siren_object_id,
                      "status",
                      NULL,
                      status_cb,
                      status,
                      UBUS_TIMEOUT_MS);

    if (ret) {
        fprintf(stderr, "[ERROR] failed to get siren status: %s\n", ubus_strerror(ret));
        return ret;
    }

    if (!status->received) {
        fprintf(stderr, "[ERROR] siren status reply not received\n");
        return -1;
    }

    return 0;
}

static int get_led_status(struct ubus_context *ctx,
                          uint32_t led_object_id,
                          struct led_status *status)
{
    int ret;

    memset(status, 0, sizeof(*status));
    snprintf(status->state, sizeof(status->state), "%s", "unknown");
    snprintf(status->pattern, sizeof(status->pattern), "%s", "unknown");
    status->led_id = -1;

    ret = ubus_invoke(ctx,
                      led_object_id,
                      "status",
                      NULL,
                      led_status_cb,
                      status,
                      UBUS_TIMEOUT_MS);

    if (ret) {
        fprintf(stderr, "[ERROR] failed to get led status: %s\n", ubus_strerror(ret));
        return ret;
    }

    if (!status->received) {
        fprintf(stderr, "[ERROR] led status reply not received\n");
        return -1;
    }

    return 0;
}

static int get_notify_status(struct ubus_context *ctx,
                             uint32_t notify_object_id,
                             struct notify_status *status)
{
    int ret;

    memset(status, 0, sizeof(*status));
    snprintf(status->last_event, sizeof(status->last_event), "%s", "none");
    snprintf(status->last_result, sizeof(status->last_result), "%s", "none");

    ret = ubus_invoke(ctx,
                      notify_object_id,
                      "status",
                      NULL,
                      notify_status_cb,
                      status,
                      UBUS_TIMEOUT_MS);

    if (ret) {
        fprintf(stderr, "[ERROR] failed to get notify status: %s\n", ubus_strerror(ret));
        return ret;
    }

    if (!status->received) {
        fprintf(stderr, "[ERROR] notify status reply not received\n");
        return -1;
    }

    return 0;
}

/* Captures the "alarm" RPC reply's own result/error fields (as opposed to a
 * subsequent "status" query) -- used to verify that a rejected direct RPC
 * call reports the failure in its own reply, without needing to change any
 * service behavior. */
struct alarm_reply {
    int received;
    char result[16];
    char error[32];
};

enum {
    ALARM_REPLY_RESULT,
    ALARM_REPLY_ERROR,
    __ALARM_REPLY_MAX
};

static const struct blobmsg_policy alarm_reply_policy[__ALARM_REPLY_MAX] = {
    [ALARM_REPLY_RESULT] = { .name = "result", .type = BLOBMSG_TYPE_STRING },
    [ALARM_REPLY_ERROR] = { .name = "error", .type = BLOBMSG_TYPE_STRING },
};

static void alarm_reply_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
    struct alarm_reply *reply = (struct alarm_reply *)req->priv;
    struct blob_attr *tb[__ALARM_REPLY_MAX] = {0};

    (void)type;

    if (!reply || !msg) {
        return;
    }

    blobmsg_parse(alarm_reply_policy,
                  __ALARM_REPLY_MAX,
                  tb,
                  blob_data(msg),
                  blob_len(msg));

    reply->received = 1;

    if (tb[ALARM_REPLY_RESULT]) {
        snprintf(reply->result, sizeof(reply->result),
                 "%s", blobmsg_get_string(tb[ALARM_REPLY_RESULT]));
    }
    if (tb[ALARM_REPLY_ERROR]) {
        snprintf(reply->error, sizeof(reply->error),
                 "%s", blobmsg_get_string(tb[ALARM_REPLY_ERROR]));
    }
}

/* Direct "alarm" RPC invoke, bypassing notifyd entirely -- used to exercise
 * a reaction service's own payload validation. payload may be NULL to send
 * no blobmsg data at all (the "missing_payload" path). */
static int invoke_alarm_raw(struct ubus_context *ctx,
                            uint32_t object_id,
                            struct blob_attr *payload,
                            struct alarm_reply *reply)
{
    int ret;

    memset(reply, 0, sizeof(*reply));

    ret = ubus_invoke(ctx,
                      object_id,
                      "alarm",
                      payload,
                      alarm_reply_cb,
                      reply,
                      UBUS_TIMEOUT_MS);

    if (ret) {
        fprintf(stderr, "[ERROR] alarm RPC invoke failed: %s\n", ubus_strerror(ret));
        return ret;
    }

    if (!reply->received) {
        fprintf(stderr, "[ERROR] alarm RPC reply not received\n");
        return -1;
    }

    return 0;
}

static int send_alarm_event(struct ubus_context *ctx,
                            const struct sim_test_case *test_case)
{
    struct blob_buf b = {0};
    int ret;

    blob_buf_init(&b, 0);

    if (!test_case->skip_type) {
        blobmsg_add_string(&b, "type", test_case->type);
    }
    if (!test_case->skip_severity) {
        blobmsg_add_string(&b, "severity", test_case->severity);
    }
    if (!test_case->skip_source) {
        blobmsg_add_string(&b, "source", "emergency-notify-sim");
    }
    if (!test_case->skip_message) {
        blobmsg_add_string(&b, "message", test_case->message);
    }

    ret = ubus_send_event(ctx, test_case->event_name, b.head);

    blob_buf_free(&b);

    if (ret) {
        fprintf(stderr,
                "[ERROR] failed to send event %s: %s\n",
                test_case->event_name,
                ubus_strerror(ret));
        return ret;
    }

    return 0;
}

static int validate_expected_result(const struct sim_test_case *test_case,
                                    const struct siren_status *status)
{
    if (test_case->expect_siren_trigger) {
        if (strcmp(status->state, "playing") != 0) {
            fprintf(stderr,
                    "[FAIL] %s: expected state=playing, got state=%s\n",
                    test_case->event_name,
                    status->state);
            return -1;
        }

        if (status->siren_id != test_case->expected_siren_id) {
            fprintf(stderr,
                    "[FAIL] %s: expected siren_id=%d, got siren_id=%d\n",
                    test_case->event_name,
                    test_case->expected_siren_id,
                    status->siren_id);
            return -1;
        }

        if (strcmp(status->pattern, test_case->expected_pattern) != 0) {
            fprintf(stderr,
                    "[FAIL] %s: expected pattern=%s, got pattern=%s\n",
                    test_case->event_name,
                    test_case->expected_pattern,
                    status->pattern);
            return -1;
        }

        return 0;
    }

    if (strcmp(status->state, "stopped") != 0) {
        fprintf(stderr,
                "[FAIL] %s: expected state=stopped, got state=%s\n",
                test_case->event_name,
                status->state);
        return -1;
    }

    if (status->siren_id != test_case->expected_siren_id) {
        fprintf(stderr,
                "[FAIL] %s: expected siren_id=%d, got siren_id=%d\n",
                test_case->event_name,
                test_case->expected_siren_id,
                status->siren_id);
        return -1;
    }

    return 0;
}

static int validate_led_expected_result(const struct sim_test_case *test_case,
                                        const struct led_status *status)
{
    const char *expected_state = test_case->expect_siren_trigger ? "playing" : "stopped";

    if (strcmp(status->state, expected_state) != 0) {
        fprintf(stderr,
                "[FAIL] %s: expected led state=%s, got state=%s\n",
                test_case->event_name,
                expected_state,
                status->state);
        return -1;
    }

    if (status->led_id != test_case->expected_led_id) {
        fprintf(stderr,
                "[FAIL] %s: expected led_id=%d, got led_id=%d\n",
                test_case->event_name,
                test_case->expected_led_id,
                status->led_id);
        return -1;
    }

    if (test_case->expect_siren_trigger &&
        strcmp(status->pattern, test_case->expected_led_pattern) != 0) {
        fprintf(stderr,
                "[FAIL] %s: expected led pattern=%s, got pattern=%s\n",
                test_case->event_name,
                test_case->expected_led_pattern,
                status->pattern);
        return -1;
    }

    return 0;
}

static int run_test_case(struct ubus_context *ctx,
                         uint32_t siren_object_id,
                         uint32_t led_object_id,
                         const struct sim_test_case *test_case,
                         int index,
                         int total)
{
    struct siren_status status;
    struct led_status led;

    fprintf(stdout,
            "\n[TEST %d/%d] %s (%s)\n",
            index,
            total,
            test_case->name,
            test_case->event_name);

    if (cancel_siren(ctx, siren_object_id) != 0) {
        fprintf(stderr, "[FAIL] %s: pre-test siren cancel failed\n", test_case->event_name);
        return -1;
    }

    if (cancel_led(ctx, led_object_id) != 0) {
        fprintf(stderr, "[FAIL] %s: pre-test led cancel failed\n", test_case->event_name);
        return -1;
    }

    fprintf(stdout, "[INFO] sending event: %s\n", test_case->event_name);

    if (send_alarm_event(ctx, test_case) != 0) {
        fprintf(stderr, "[FAIL] %s: event send failed\n", test_case->event_name);
        return -1;
    }

    usleep(TEST_SETTLE_USEC);

    if (get_siren_status(ctx, siren_object_id, &status) != 0) {
        fprintf(stderr, "[FAIL] %s: status validation failed\n", test_case->event_name);
        return -1;
    }

    fprintf(stdout,
            "[INFO] siren status: state=%s, siren_id=%d, pattern=%s\n",
            status.state,
            status.siren_id,
            status.pattern);

    if (validate_expected_result(test_case, &status) != 0) {
        return -1;
    }

    if (get_led_status(ctx, led_object_id, &led) != 0) {
        fprintf(stderr, "[FAIL] %s: led status validation failed\n", test_case->event_name);
        return -1;
    }

    fprintf(stdout,
            "[INFO] led status: state=%s, led_id=%d, pattern=%s\n",
            led.state,
            led.led_id,
            led.pattern);

    if (validate_led_expected_result(test_case, &led) != 0) {
        return -1;
    }

    fprintf(stdout, "[PASS] %s\n", test_case->event_name);
    return 0;
}

/* Sends alarm.fire, then -- without any cancel_alarm in between -- sends
 * alarm.panic while fire is still active. Exercises the cancel-before-start
 * behavior already built into siren_action_handle_alarm_event()/
 * led_action_handle_alarm_event() (each cancels the reaction service before
 * calling "alarm"), which in turn relies on siren_controller_start_by_id()/
 * led_controller_start_by_id() always stopping any running pattern before
 * starting a new one. No new service behavior is introduced by this test. */
static int run_alarm_overlap_test(struct ubus_context *ctx,
                                  uint32_t siren_object_id,
                                  uint32_t led_object_id,
                                  int index,
                                  int total)
{
    struct sim_test_case fire_case = {
        .name = "overlap: fire",
        .event_name = "alarm.fire",
        .type = "fire",
        .severity = "critical",
        .message = "Overlap test: fire alarm",
    };
    struct sim_test_case panic_case = {
        .name = "overlap: panic",
        .event_name = "alarm.panic",
        .type = "panic",
        .severity = "critical",
        .message = "Overlap test: panic alarm, no cancel before this",
    };
    struct siren_status mid_siren;
    struct siren_status siren;
    struct led_status led;

    fprintf(stdout,
            "\n[TEST %d/%d] alarm overlap without cancel in between\n",
            index, total);

    if (cancel_siren(ctx, siren_object_id) != 0 || cancel_led(ctx, led_object_id) != 0) {
        fprintf(stderr, "[FAIL] alarm overlap: pre-test cancel failed\n");
        return -1;
    }

    fprintf(stdout, "[INFO] sending event: alarm.fire\n");
    if (send_alarm_event(ctx, &fire_case) != 0) {
        fprintf(stderr, "[FAIL] alarm overlap: fire event send failed\n");
        return -1;
    }

    usleep(TEST_OVERLAP_GAP_USEC);

    /* Confirm fire is genuinely active before preempting it -- otherwise
     * this test would not actually exercise an overlap. */
    if (get_siren_status(ctx, siren_object_id, &mid_siren) != 0) {
        fprintf(stderr, "[FAIL] alarm overlap: mid-test siren status query failed\n");
        return -1;
    }

    fprintf(stdout,
            "[INFO] mid-test siren status (should be fire's pattern): state=%s, siren_id=%d, pattern=%s\n",
            mid_siren.state, mid_siren.siren_id, mid_siren.pattern);

    if (strcmp(mid_siren.state, "playing") != 0 ||
        mid_siren.siren_id != 2 ||
        strcmp(mid_siren.pattern, "temporal_3") != 0) {
        fprintf(stderr,
                "[FAIL] alarm overlap: fire alarm was not active before panic was sent "
                "(state=%s, siren_id=%d, pattern=%s) -- not a genuine overlap\n",
                mid_siren.state, mid_siren.siren_id, mid_siren.pattern);
        return -1;
    }

    fprintf(stdout, "[INFO] sending event: alarm.panic (no cancel_alarm in between)\n");
    if (send_alarm_event(ctx, &panic_case) != 0) {
        fprintf(stderr, "[FAIL] alarm overlap: panic event send failed\n");
        return -1;
    }

    usleep(TEST_SETTLE_USEC);

    if (get_siren_status(ctx, siren_object_id, &siren) != 0) {
        fprintf(stderr, "[FAIL] alarm overlap: siren status query failed\n");
        return -1;
    }

    fprintf(stdout,
            "[INFO] final siren status: state=%s, siren_id=%d, pattern=%s\n",
            siren.state, siren.siren_id, siren.pattern);

    if (strcmp(siren.state, "playing") != 0 ||
        siren.siren_id != 4 ||
        strcmp(siren.pattern, "panic_pulse") != 0) {
        fprintf(stderr,
                "[FAIL] alarm overlap: expected siren state=playing siren_id=4 pattern=panic_pulse, "
                "got state=%s siren_id=%d pattern=%s\n",
                siren.state, siren.siren_id, siren.pattern);
        return -1;
    }

    if (get_led_status(ctx, led_object_id, &led) != 0) {
        fprintf(stderr, "[FAIL] alarm overlap: led status query failed\n");
        return -1;
    }

    fprintf(stdout,
            "[INFO] final led status: state=%s, led_id=%d, pattern=%s\n",
            led.state, led.led_id, led.pattern);

    if (strcmp(led.state, "playing") != 0 ||
        led.led_id != 2 ||
        strcmp(led.pattern, "red_solid") != 0) {
        fprintf(stderr,
                "[FAIL] alarm overlap: expected led state=playing led_id=2 pattern=red_solid, "
                "got state=%s led_id=%d pattern=%s\n",
                led.state, led.led_id, led.pattern);
        return -1;
    }

    fprintf(stdout, "[PASS] alarm overlap without cancel in between\n");
    return 0;
}

/* Direct invalid "alarm" RPC calls to emergency.siren, bypassing notifyd
 * entirely -- proves the service rejects bad input on its own (already
 * implemented in siren_service.c) and stays alive/responsive afterward. */
static int run_siren_invalid_rpc_test(struct ubus_context *ctx,
                                      uint32_t siren_object_id,
                                      int index,
                                      int total)
{
    struct blob_buf b = {0};
    struct alarm_reply reply;
    struct siren_status status;

    fprintf(stdout,
            "\n[TEST %d/%d] direct invalid alarm RPC to emergency.siren\n",
            index, total);

    if (cancel_siren(ctx, siren_object_id) != 0) {
        fprintf(stderr, "[FAIL] siren invalid rpc: pre-test cancel failed\n");
        return -1;
    }

    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "siren_id", 99);

    if (invoke_alarm_raw(ctx, siren_object_id, b.head, &reply) != 0) {
        blob_buf_free(&b);
        fprintf(stderr, "[FAIL] siren invalid rpc: invoke with siren_id=99 failed\n");
        return -1;
    }
    blob_buf_free(&b);

    fprintf(stdout, "[INFO] siren_id=99 reply: result=%s error=%s\n", reply.result, reply.error);

    if (strcmp(reply.result, "error") != 0) {
        fprintf(stderr,
                "[FAIL] siren invalid rpc: expected result=error for siren_id=99, got result=%s\n",
                reply.result);
        return -1;
    }

    if (invoke_alarm_raw(ctx, siren_object_id, NULL, &reply) != 0) {
        fprintf(stderr, "[FAIL] siren invalid rpc: invoke with empty payload failed\n");
        return -1;
    }

    fprintf(stdout, "[INFO] empty payload reply: result=%s error=%s\n", reply.result, reply.error);

    if (strcmp(reply.result, "error") != 0) {
        fprintf(stderr,
                "[FAIL] siren invalid rpc: expected result=error for empty payload, got result=%s\n",
                reply.result);
        return -1;
    }

    /* The service must still be alive and responsive after two rejected
     * calls -- an unhandled bad request must not take it down. */
    if (get_siren_status(ctx, siren_object_id, &status) != 0) {
        fprintf(stderr, "[FAIL] siren invalid rpc: status query failed after invalid calls\n");
        return -1;
    }

    fprintf(stdout,
            "[INFO] siren status after invalid calls: state=%s, siren_id=%d, pattern=%s\n",
            status.state, status.siren_id, status.pattern);

    fprintf(stdout, "[PASS] direct invalid alarm RPC to emergency.siren\n");
    return 0;
}

/* Same as run_siren_invalid_rpc_test(), for emergency.led. */
static int run_led_invalid_rpc_test(struct ubus_context *ctx,
                                    uint32_t led_object_id,
                                    int index,
                                    int total)
{
    struct blob_buf b = {0};
    struct alarm_reply reply;
    struct led_status status;

    fprintf(stdout,
            "\n[TEST %d/%d] direct invalid alarm RPC to emergency.led\n",
            index, total);

    if (cancel_led(ctx, led_object_id) != 0) {
        fprintf(stderr, "[FAIL] led invalid rpc: pre-test cancel failed\n");
        return -1;
    }

    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "led_id", 99);

    if (invoke_alarm_raw(ctx, led_object_id, b.head, &reply) != 0) {
        blob_buf_free(&b);
        fprintf(stderr, "[FAIL] led invalid rpc: invoke with led_id=99 failed\n");
        return -1;
    }
    blob_buf_free(&b);

    fprintf(stdout, "[INFO] led_id=99 reply: result=%s error=%s\n", reply.result, reply.error);

    if (strcmp(reply.result, "error") != 0) {
        fprintf(stderr,
                "[FAIL] led invalid rpc: expected result=error for led_id=99, got result=%s\n",
                reply.result);
        return -1;
    }

    if (invoke_alarm_raw(ctx, led_object_id, NULL, &reply) != 0) {
        fprintf(stderr, "[FAIL] led invalid rpc: invoke with empty payload failed\n");
        return -1;
    }

    fprintf(stdout, "[INFO] empty payload reply: result=%s error=%s\n", reply.result, reply.error);

    if (strcmp(reply.result, "error") != 0) {
        fprintf(stderr,
                "[FAIL] led invalid rpc: expected result=error for empty payload, got result=%s\n",
                reply.result);
        return -1;
    }

    /* The service must still be alive and responsive after two rejected
     * calls -- an unhandled bad request must not take it down. */
    if (get_led_status(ctx, led_object_id, &status) != 0) {
        fprintf(stderr, "[FAIL] led invalid rpc: status query failed after invalid calls\n");
        return -1;
    }

    fprintf(stdout,
            "[INFO] led status after invalid calls: state=%s, led_id=%d, pattern=%s\n",
            status.state, status.led_id, status.pattern);

    fprintf(stdout, "[PASS] direct invalid alarm RPC to emergency.led\n");
    return 0;
}

static int count_alarm_test_cases(void)
{
    int total = sizeof(test_cases) / sizeof(test_cases[0]);
    int count = 0;
    int i;

    for (i = 0; i < total; i++) {
        if (strncmp(test_cases[i].event_name, "alarm.", 6) == 0) {
            count++;
        }
    }

    return count;
}

/* Counters in emergency-notifyd are cumulative for the life of the process,
 * so this compares the delta between a baseline snapshot (taken before any
 * test case ran) and a snapshot taken after all test cases ran. That makes
 * the check correct whether emergency-notifyd was just started or has been
 * running across multiple simulator runs. */
static int run_stats_check(struct ubus_context *ctx,
                           uint32_t notify_object_id,
                           const struct notify_status *before,
                           int index,
                           int total)
{
    struct notify_status after;
    unsigned int delta_received;
    unsigned int delta_accepted;
    unsigned int delta_ignored;
    unsigned int delta_invalid;
    unsigned int delta_sent;
    unsigned int delta_failed;
    unsigned int expected_actions_sent;
    int expected_alarm_events;

    fprintf(stdout, "\n[TEST %d/%d] runtime status counters\n", index, total);

    if (get_notify_status(ctx, notify_object_id, &after) != 0) {
        fprintf(stderr, "[FAIL] stats check: status query failed\n");
        return -1;
    }

    delta_received = after.events_received - before->events_received;
    delta_accepted = after.events_accepted - before->events_accepted;
    delta_ignored = after.events_ignored - before->events_ignored;
    delta_invalid = after.events_invalid - before->events_invalid;
    delta_sent = after.actions_sent - before->actions_sent;
    delta_failed = after.actions_failed - before->actions_failed;

    fprintf(stdout,
            "[INFO] stats delta: received=%u accepted=%u ignored=%u invalid=%u "
            "actions_sent=%u actions_failed=%u last_event=%s last_result=%s\n",
            delta_received,
            delta_accepted,
            delta_ignored,
            delta_invalid,
            delta_sent,
            delta_failed,
            after.last_event,
            after.last_result);

    /* + SIM_OVERLAP_ALARM_EVENT_COUNT: run_alarm_overlap_test() sends two
     * alarm.* events (fire, panic) directly, outside the test_cases table. */
    expected_alarm_events = count_alarm_test_cases() + SIM_OVERLAP_ALARM_EVENT_COUNT;

    if (delta_received != (unsigned int)expected_alarm_events) {
        fprintf(stderr,
                "[FAIL] stats check: expected events_received delta=%d, got %u\n",
                expected_alarm_events,
                delta_received);
        return -1;
    }

    if (delta_received != delta_accepted + delta_ignored + delta_invalid) {
        fprintf(stderr,
                "[FAIL] stats check: invariant violated: received=%u != accepted=%u + ignored=%u + invalid=%u\n",
                delta_received,
                delta_accepted,
                delta_ignored,
                delta_invalid);
        return -1;
    }

    /* Every accepted event is recognized by both registered actions (siren
     * and led) in this test suite -- see SIM_ACTIONS_PER_ACCEPTED_EVENT. */
    expected_actions_sent = delta_accepted * SIM_ACTIONS_PER_ACCEPTED_EVENT;

    if (delta_sent + delta_failed != expected_actions_sent) {
        fprintf(stderr,
                "[FAIL] stats check: expected actions_sent+actions_failed=%u, got sent=%u failed=%u\n",
                expected_actions_sent,
                delta_sent,
                delta_failed);
        return -1;
    }

    fprintf(stdout, "[PASS] runtime status counters\n");
    return 0;
}

int main(void)
{
    struct ubus_context *ctx;
    uint32_t notify_object_id;
    uint32_t siren_object_id;
    uint32_t led_object_id;
    struct notify_status stats_before;
    int passed = 0;
    int failed = 0;
    int table_count;
    int total;
    int i;

    fprintf(stdout, "[INFO] emergency-notify-sim started\n");

    ctx = ubus_connect(NULL);
    if (!ctx) {
        fprintf(stderr, "[ERROR] ubus connect failed\n");
        return 1;
    }

    if (lookup_required_object(ctx, NOTIFY_OBJECT_NAME, &notify_object_id) != 0) {
        ubus_free(ctx);
        return 1;
    }

    if (lookup_required_object(ctx, SIREN_OBJECT_NAME, &siren_object_id) != 0) {
        ubus_free(ctx);
        return 1;
    }

    if (lookup_required_object(ctx, LED_OBJECT_NAME, &led_object_id) != 0) {
        ubus_free(ctx);
        return 1;
    }

    if (get_notify_status(ctx, notify_object_id, &stats_before) != 0) {
        ubus_free(ctx);
        return 1;
    }

    table_count = sizeof(test_cases) / sizeof(test_cases[0]);
    /* + 1 alarm overlap test + 2 direct invalid RPC tests + 1 stats check */
    total = table_count + 4;

    for (i = 0; i < table_count; i++) {
        if (run_test_case(ctx, siren_object_id, led_object_id, &test_cases[i], i + 1, total) == 0) {
            passed++;
        } else {
            failed++;
        }
    }

    if (run_alarm_overlap_test(ctx, siren_object_id, led_object_id, table_count + 1, total) == 0) {
        passed++;
    } else {
        failed++;
    }

    if (run_siren_invalid_rpc_test(ctx, siren_object_id, table_count + 2, total) == 0) {
        passed++;
    } else {
        failed++;
    }

    if (run_led_invalid_rpc_test(ctx, led_object_id, table_count + 3, total) == 0) {
        passed++;
    } else {
        failed++;
    }

    cancel_siren(ctx, siren_object_id);
    cancel_led(ctx, led_object_id);

    if (run_stats_check(ctx, notify_object_id, &stats_before, total, total) == 0) {
        passed++;
    } else {
        failed++;
    }

    fprintf(stdout,
            "\n[SUMMARY] total=%d passed=%d failed=%d\n",
            total,
            passed,
            failed);

    ubus_free(ctx);

    return failed == 0 ? 0 : 1;
}