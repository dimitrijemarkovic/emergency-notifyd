#include "event_validation.h"

#include <string.h>

enum {
    ALARM_PAYLOAD_TYPE,
    ALARM_PAYLOAD_SEVERITY,
    ALARM_PAYLOAD_SOURCE,
    ALARM_PAYLOAD_MESSAGE,
    __ALARM_PAYLOAD_MAX
};

static const struct blobmsg_policy alarm_payload_policy[__ALARM_PAYLOAD_MAX] = {
    [ALARM_PAYLOAD_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING },
    [ALARM_PAYLOAD_SEVERITY] = { .name = "severity", .type = BLOBMSG_TYPE_STRING },
    [ALARM_PAYLOAD_SOURCE] = { .name = "source", .type = BLOBMSG_TYPE_STRING },
    [ALARM_PAYLOAD_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
};

static const char *allowed_severities[] = {
    "low", "medium", "high", "critical",
};

static int is_allowed_severity(const char *severity)
{
    size_t i;

    for (i = 0; i < sizeof(allowed_severities) / sizeof(allowed_severities[0]); i++) {
        if (strcmp(severity, allowed_severities[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static const char *event_name_suffix(const char *event_name)
{
    const char *dot;

    if (!event_name) {
        return NULL;
    }

    dot = strrchr(event_name, '.');
    return dot ? dot + 1 : event_name;
}

event_validation_status_t event_validation_check(const char *event_name,
                                                 struct blob_attr *msg,
                                                 alarm_event_payload_t *out)
{
    struct blob_attr *tb[__ALARM_PAYLOAD_MAX] = {0};
    const char *type = NULL;
    const char *severity = NULL;
    const char *source = NULL;
    const char *message = NULL;
    const char *suffix;
    int fields_present;

    if (out) {
        memset(out, 0, sizeof(*out));
    }

    if (msg) {
        blobmsg_parse(alarm_payload_policy,
                      __ALARM_PAYLOAD_MAX,
                      tb,
                      blob_data(msg),
                      blob_len(msg));

        if (tb[ALARM_PAYLOAD_TYPE]) {
            type = blobmsg_get_string(tb[ALARM_PAYLOAD_TYPE]);
        }
        if (tb[ALARM_PAYLOAD_SEVERITY]) {
            severity = blobmsg_get_string(tb[ALARM_PAYLOAD_SEVERITY]);
        }
        if (tb[ALARM_PAYLOAD_SOURCE]) {
            source = blobmsg_get_string(tb[ALARM_PAYLOAD_SOURCE]);
        }
        if (tb[ALARM_PAYLOAD_MESSAGE]) {
            message = blobmsg_get_string(tb[ALARM_PAYLOAD_MESSAGE]);
        }
    }

    fields_present = (type != NULL) + (severity != NULL) + (source != NULL);

    /* MISSING_PAYLOAD means the blob carries none of the recognized fields at
     * all (including "message"); MISSING_FIELD means at least one recognized
     * field is present but one of the three required ones is not. Without
     * counting "message" here, a payload containing only "message" would be
     * misclassified as MISSING_PAYLOAD even though it is not empty. */
    if (!msg || (fields_present == 0 && message == NULL)) {
        return EVENT_VALIDATION_MISSING_PAYLOAD;
    }

    if (fields_present < 3) {
        return EVENT_VALIDATION_MISSING_FIELD;
    }

    if (!is_allowed_severity(severity)) {
        return EVENT_VALIDATION_BAD_SEVERITY;
    }

    suffix = event_name_suffix(event_name);
    if (!suffix || strcmp(type, suffix) != 0) {
        return EVENT_VALIDATION_TYPE_MISMATCH;
    }

    if (out) {
        out->type = type;
        out->severity = severity;
        out->source = source;
        out->message = message;
    }

    return EVENT_VALIDATION_OK;
}

const char *event_validation_status_str(event_validation_status_t status)
{
    switch (status) {
    case EVENT_VALIDATION_OK:
        return "ok";
    case EVENT_VALIDATION_MISSING_PAYLOAD:
        return "missing_payload";
    case EVENT_VALIDATION_MISSING_FIELD:
        return "missing_field";
    case EVENT_VALIDATION_BAD_SEVERITY:
        return "bad_severity";
    case EVENT_VALIDATION_TYPE_MISMATCH:
        return "type_mismatch";
    default:
        return "unknown";
    }
}
