#include "led_controller.h"

#include "led_hw.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "emergency/log.h"

typedef struct {
    int on_ms;
    int off_ms;
} led_step_t;

typedef struct {
    led_pattern_id_t id;
    const char *name;
    led_channel_t channel;
    const led_step_t *steps; /* NULL means solid on, like siren's STEADY */
    int step_count;
} led_pattern_def_t;

typedef struct {
    led_pattern_id_t pattern_id;
    int duration_sec;
} led_worker_arg_t;

static const led_step_t fast_blink_steps[] = {
    {150, 150},
};

static const led_step_t slow_blink_steps[] = {
    {500, 1500},
};

static const led_pattern_def_t pattern_table[] = {
    {
        .id = LED_PATTERN_RED_FAST,
        .name = "red_fast",
        .channel = LED_CHANNEL_RED,
        .steps = fast_blink_steps,
        .step_count = sizeof(fast_blink_steps) / sizeof(fast_blink_steps[0]),
    },
    {
        .id = LED_PATTERN_RED_SOLID,
        .name = "red_solid",
        .channel = LED_CHANNEL_RED,
        .steps = NULL,
        .step_count = 0,
    },
    {
        .id = LED_PATTERN_BLUE_FAST,
        .name = "blue_fast",
        .channel = LED_CHANNEL_BLUE,
        .steps = fast_blink_steps,
        .step_count = sizeof(fast_blink_steps) / sizeof(fast_blink_steps[0]),
    },
    {
        .id = LED_PATTERN_BLUE_SLOW,
        .name = "blue_slow",
        .channel = LED_CHANNEL_BLUE,
        .steps = slow_blink_steps,
        .step_count = sizeof(slow_blink_steps) / sizeof(slow_blink_steps[0]),
    },
};

static pthread_t pattern_thread;
static pthread_mutex_t pattern_lock = PTHREAD_MUTEX_INITIALIZER;

static int thread_active = 0;
static int keep_running = 0;
static led_pattern_id_t current_pattern = LED_PATTERN_INVALID;
static int current_duration_sec = 0;

static long long get_time_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static const led_pattern_def_t *find_pattern_by_id(led_pattern_id_t id)
{
    unsigned int i;

    for (i = 0; i < sizeof(pattern_table) / sizeof(pattern_table[0]); i++) {
        if (pattern_table[i].id == id) {
            return &pattern_table[i];
        }
    }

    return NULL;
}

int led_controller_is_valid_id(int led_id)
{
    return find_pattern_by_id((led_pattern_id_t)led_id) != NULL;
}

const char *led_controller_pattern_name_by_id(int led_id)
{
    const led_pattern_def_t *pattern;

    pattern = find_pattern_by_id((led_pattern_id_t)led_id);
    if (!pattern) {
        return "unknown";
    }

    return pattern->name;
}

static int is_running_locked(void)
{
    return keep_running;
}

int led_controller_is_running(void)
{
    int running;

    pthread_mutex_lock(&pattern_lock);
    running = keep_running;
    pthread_mutex_unlock(&pattern_lock);

    return running;
}

int led_controller_current_id(void)
{
    int id;

    pthread_mutex_lock(&pattern_lock);
    id = current_pattern;
    pthread_mutex_unlock(&pattern_lock);

    return id;
}

int led_controller_current_duration(void)
{
    int duration;

    pthread_mutex_lock(&pattern_lock);
    duration = current_duration_sec;
    pthread_mutex_unlock(&pattern_lock);

    return duration;
}

const char *led_controller_current_pattern_name(void)
{
    led_pattern_id_t id;

    pthread_mutex_lock(&pattern_lock);
    id = current_pattern;
    pthread_mutex_unlock(&pattern_lock);

    return led_controller_pattern_name_by_id(id);
}

static int duration_expired(long long start_ms, int duration_sec)
{
    long long now_ms;
    long long duration_ms;

    if (duration_sec == LED_DURATION_INFINITE) {
        return 0;
    }

    duration_ms = (long long)duration_sec * 1000LL;
    now_ms = get_time_ms();

    return (now_ms - start_ms) >= duration_ms;
}

static int should_continue(long long start_ms, int duration_sec)
{
    int result;

    pthread_mutex_lock(&pattern_lock);
    result = is_running_locked() && !duration_expired(start_ms, duration_sec);
    pthread_mutex_unlock(&pattern_lock);

    return result;
}

static void interruptible_sleep_ms(int total_ms, long long start_ms, int duration_sec)
{
    int elapsed = 0;
    const int step_ms = 50;

    while (elapsed < total_ms && should_continue(start_ms, duration_sec)) {
        usleep(step_ms * 1000);
        elapsed += step_ms;
    }
}

static void mark_stopped_from_worker(void)
{
    pthread_mutex_lock(&pattern_lock);
    keep_running = 0;
    current_pattern = LED_PATTERN_INVALID;
    current_duration_sec = 0;
    pthread_mutex_unlock(&pattern_lock);
}

static void *pattern_worker(void *arg)
{
    led_worker_arg_t *worker_arg = (led_worker_arg_t *)arg;
    led_pattern_id_t pattern_id = worker_arg->pattern_id;
    int duration_sec = worker_arg->duration_sec;
    const led_pattern_def_t *pattern;
    long long start_ms;
    int brightness;
    int i;

    free(worker_arg);

    pattern = find_pattern_by_id(pattern_id);
    if (!pattern) {
        led_hw_all_off();
        mark_stopped_from_worker();
        return NULL;
    }

    start_ms = get_time_ms();
    brightness = led_hw_ambient_brightness();

    emergency_log_info("pattern started: id=%d, pattern=%s, duration=%d",
                       pattern->id,
                       pattern->name,
                       duration_sec);

    if (!pattern->steps) {
        led_hw_set_channel(pattern->channel, brightness);

        while (should_continue(start_ms, duration_sec)) {
            interruptible_sleep_ms(100, start_ms, duration_sec);
        }

        led_hw_all_off();
        mark_stopped_from_worker();

        emergency_log_info("pattern finished");

        return NULL;
    }

    while (should_continue(start_ms, duration_sec)) {
        for (i = 0; i < pattern->step_count && should_continue(start_ms, duration_sec); i++) {
            led_hw_set_channel(pattern->channel, brightness);
            interruptible_sleep_ms(pattern->steps[i].on_ms, start_ms, duration_sec);

            led_hw_set_channel(pattern->channel, 0);
            interruptible_sleep_ms(pattern->steps[i].off_ms, start_ms, duration_sec);
        }
    }

    led_hw_all_off();
    mark_stopped_from_worker();

    emergency_log_info("pattern finished");

    return NULL;
}

int led_controller_init(void)
{
    return led_hw_init();
}

void led_controller_deinit(void)
{
    led_controller_stop();
    led_hw_deinit();
}

int led_controller_stop(void)
{
    int should_join = 0;

    pthread_mutex_lock(&pattern_lock);

    if (thread_active) {
        keep_running = 0;
        should_join = 1;
    }

    pthread_mutex_unlock(&pattern_lock);

    if (should_join) {
        pthread_join(pattern_thread, NULL);
    }

    pthread_mutex_lock(&pattern_lock);
    thread_active = 0;
    keep_running = 0;
    current_pattern = LED_PATTERN_INVALID;
    current_duration_sec = 0;
    pthread_mutex_unlock(&pattern_lock);

    led_hw_all_off();

    return 0;
}

int led_controller_start_by_id(int led_id, int duration_sec)
{
    const led_pattern_def_t *pattern;
    led_worker_arg_t *thread_arg;
    int ret;

    pattern = find_pattern_by_id((led_pattern_id_t)led_id);
    if (!pattern) {
        return -1;
    }

    if (duration_sec <= 0) {
        duration_sec = LED_DURATION_INFINITE;
    }

    led_controller_stop();

    thread_arg = malloc(sizeof(*thread_arg));
    if (!thread_arg) {
        return -1;
    }

    thread_arg->pattern_id = pattern->id;
    thread_arg->duration_sec = duration_sec;

    pthread_mutex_lock(&pattern_lock);
    keep_running = 1;
    thread_active = 1;
    current_pattern = pattern->id;
    current_duration_sec = duration_sec;
    pthread_mutex_unlock(&pattern_lock);

    ret = pthread_create(&pattern_thread, NULL, pattern_worker, thread_arg);
    if (ret != 0) {
        free(thread_arg);

        pthread_mutex_lock(&pattern_lock);
        keep_running = 0;
        thread_active = 0;
        current_pattern = LED_PATTERN_INVALID;
        current_duration_sec = 0;
        pthread_mutex_unlock(&pattern_lock);

        led_hw_all_off();
        return -1;
    }

    return 0;
}
