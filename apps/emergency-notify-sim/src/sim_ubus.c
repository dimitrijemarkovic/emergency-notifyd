#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <libubox/blobmsg.h>
#include <libubus.h>

#define NOTIFY_OBJECT_NAME "emergency.notify"
#define SIREN_OBJECT_NAME  "emergency.siren"

#define UBUS_TIMEOUT_MS 3000
#define TEST_SETTLE_USEC 500000

struct sim_test_case {
    const char *name;
    const char *event_name;
    const char *type;
    const char *severity;
    const char *message;

    int expect_siren_trigger;
    int expected_siren_id;
    const char *expected_pattern;
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

static int send_alarm_event(struct ubus_context *ctx,
                            const struct sim_test_case *test_case)
{
    struct blob_buf b = {0};
    int ret;

    blob_buf_init(&b, 0);

    blobmsg_add_string(&b, "type", test_case->type);
    blobmsg_add_string(&b, "severity", test_case->severity);
    blobmsg_add_string(&b, "source", "emergency-notify-sim");
    blobmsg_add_string(&b, "message", test_case->message);

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

static int run_test_case(struct ubus_context *ctx,
                         uint32_t siren_object_id,
                         const struct sim_test_case *test_case,
                         int index,
                         int total)
{
    struct siren_status status;

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

    fprintf(stdout, "[PASS] %s\n", test_case->event_name);
    return 0;
}

int main(void)
{
    struct ubus_context *ctx;
    uint32_t notify_object_id;
    uint32_t siren_object_id;
    int passed = 0;
    int failed = 0;
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

    (void)notify_object_id;

    total = sizeof(test_cases) / sizeof(test_cases[0]);

    for (i = 0; i < total; i++) {
        if (run_test_case(ctx, siren_object_id, &test_cases[i], i + 1, total) == 0) {
            passed++;
        } else {
            failed++;
        }
    }

    cancel_siren(ctx, siren_object_id);

    fprintf(stdout,
            "\n[SUMMARY] total=%d passed=%d failed=%d\n",
            total,
            passed,
            failed);

    ubus_free(ctx);

    return failed == 0 ? 0 : 1;
}