#pragma once

#include <libubus.h>

#include "../action_result.h"

action_result_t siren_action_handle_alarm_event(struct ubus_context *ctx,
                                                const char *event_type);
