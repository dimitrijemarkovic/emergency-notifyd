#include "event_classification.h"

const char *event_classification_str(alarm_event_classification_t classification)
{
    switch (classification) {
    case ALARM_EVENT_ACCEPTED:
        return "accepted";
    case ALARM_EVENT_IGNORED:
        return "ignored";
    case ALARM_EVENT_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}
