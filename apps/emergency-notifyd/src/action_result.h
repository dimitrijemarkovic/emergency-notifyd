#pragma once

/* Shared outcome type for every registered alarm action (see alarm_action.h).
 * Previously each action defined its own result enum (e.g. siren_action_result_t);
 * now that there is more than one action, the type is shared so notifyd_ubus.c
 * can treat all actions uniformly. */
typedef enum {
    ACTION_RESULT_ERROR = -1,
    ACTION_RESULT_ACCEPTED = 0,
    ACTION_RESULT_IGNORED = 1,
} action_result_t;
