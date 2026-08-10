#pragma once

#include <libubus.h>

#include "../action_result.h"
#include "../event_validation.h"

action_result_t mqtt_action_handle_alarm_event(struct ubus_context *ctx,
                                               const char *event_type,
                                               const alarm_event_payload_t *payload);
