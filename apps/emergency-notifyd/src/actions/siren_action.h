#pragma once

#include <libubus.h>

int siren_action_handle_alarm_event(struct ubus_context *ctx,
                                    const char *event_type);
