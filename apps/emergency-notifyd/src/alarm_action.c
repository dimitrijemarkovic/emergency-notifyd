#include "alarm_action.h"

#include "actions/led_action.h"
#include "actions/siren_action.h"

const alarm_action_t alarm_actions[] = {
    { "siren", siren_action_handle_alarm_event },
    { "led", led_action_handle_alarm_event },
};

const size_t alarm_actions_count = sizeof(alarm_actions) / sizeof(alarm_actions[0]);
