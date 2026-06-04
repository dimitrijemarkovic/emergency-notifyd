#include "siren_controller.h"

#include "siren_hw.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int on_ms;
    int off_ms;
} siren_step_t;

typedef struct {
    siren_pattern_id_t id;
    const char *name;
    const siren_step_t *steps;
    int step_count;
} siren_pattern_def_t;

typedef struct {
    siren_pattern_id_t pattern_id;
    int duration_sec;
} siren_worker_arg_t;

static const siren_step_t temporal_3_steps[] = {
    {500, 500},
    {500, 500},
    {500, 1500},
};

static const siren_step_t temporal_4_steps[] = {
    {500, 500},
    {500, 500},
    {500, 500},
    {500, 2000},
};

static const siren_step_t panic_pulse_steps[] = {
    {250, 250},
};

static const siren_step_t medical_slow_pulse_steps[] = {
    {500, 2000},
};

static const siren_pattern_def_t pattern_table[] = {
    {
        .id = SIREN_PATTERN_STEADY,
        .name = "steady",
        .steps = NULL,
        .step_count = 0,
    },
    {
        .id = SIREN_PATTERN_TEMPORAL_3,
        .name = "temporal_3",
        .steps = temporal_3_steps,
        .step_count = sizeof(temporal_3_steps) / sizeof(temporal_3_steps[0]),
    },
    {
        .id = SIREN_PATTERN_TEMPORAL_4,
        .name = "temporal_4",
        .steps = temporal_4_steps,
        .step_count = sizeof(temporal_4_steps) / sizeof(temporal_4_steps[0]),
    },
    {
        .id = SIREN_PATTERN_PANIC_PULSE,
        .name = "panic_pulse",
        .steps = panic_pulse_steps,
        .step_count = sizeof(panic_pulse_steps) / sizeof(panic_pulse_steps[0]),
    },
    {
        .id = SIREN_PATTERN_MEDICAL_SLOW_PULSE,
        .name = "medical_slow_pulse",
        .steps = medical_slow_pulse_steps,
        .step_count = sizeof(medical_slow_pulse_steps) / sizeof(medical_slow_pulse_steps[0]),
    },
};

static pthread_t pattern_thread;
static pthread_mutex_t pattern_lock = PTHREAD_MUTEX_INITIALIZER;

static int thread_active = 0;
static int keep_running = 0;
static siren_pattern_id_t current_pattern = SIREN_PATTERN_INVALID;
static int current_duration_sec = 0;

static long long get_time_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static const siren_pattern_def_t *find_pattern_by_id(siren_pattern_id_t id)
{
    unsigned int i;

    for (i = 0; i < sizeof(pattern_table) / sizeof(pattern_table[0]); i++) {
        if (pattern_table[i].id == id) {
            return &pattern_table[i];
        }
    }

    return NULL;
}

int siren_controller_is_valid_id(int siren_id)
{
    return find_pattern_by_id((siren_pattern_id_t)siren_id) != NULL;
}

const char *siren_controller_pattern_name_by_id(int siren_id)
{
    const siren_pattern_def_t *pattern;

    pattern = find_pattern_by_id((siren_pattern_id_t)siren_id);
    if (!pattern) {
        return "unknown";
    }

    return pattern->name;
}

static int is_running_locked(void)
{
    return keep_running;
}

int siren_controller_is_running(void)
{
    int running;

    pthread_mutex_lock(&pattern_lock);
    running = keep_running;
    pthread_mutex_unlock(&pattern_lock);

    return running;
}

int siren_controller_current_id(void)
{
    int id;

    pthread_mutex_lock(&pattern_lock);
    id = current_pattern;
    pthread_mutex_unlock(&pattern_lock);

    return id;
}

int siren_controller_current_duration(void)
{
    int duration;

    pthread_mutex_lock(&pattern_lock);
    duration = current_duration_sec;
    pthread_mutex_unlock(&pattern_lock);

    return duration;
}

const char *siren_controller_current_pattern_name(void)
{
    siren_pattern_id_t id;

    pthread_mutex_lock(&pattern_lock);
    id = current_pattern;
    pthread_mutex_unlock(&pattern_lock);

    return siren_controller_pattern_name_by_id(id);
}

static int duration_expired(long long start_ms, int duration_sec)
{
    long long now_ms;
    long long duration_ms;

    if (duration_sec == SIREN_DURATION_INFINITE) {
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
    current_pattern = SIREN_PATTERN_INVALID;
    current_duration_sec = 0;
    pthread_mutex_unlock(&pattern_lock);
}

static void *pattern_worker(void *arg)
{
    siren_worker_arg_t *worker_arg = (siren_worker_arg_t *)arg;
    siren_pattern_id_t pattern_id = worker_arg->pattern_id;
    int duration_sec = worker_arg->duration_sec;
    const siren_pattern_def_t *pattern;
    long long start_ms;
    int i;

    free(worker_arg);

    pattern = find_pattern_by_id(pattern_id);
    if (!pattern) {
        siren_hw_off();
        mark_stopped_from_worker();
        return NULL;
    }

    start_ms = get_time_ms();

    fprintf(stdout,
            "[emergency-siren-service] pattern started: id=%d, pattern=%s, duration=%d\n",
            pattern->id,
            pattern->name,
            duration_sec);
    fflush(stdout);

    if (pattern->id == SIREN_PATTERN_STEADY) {
        siren_hw_on();

        while (should_continue(start_ms, duration_sec)) {
            interruptible_sleep_ms(100, start_ms, duration_sec);
        }

        siren_hw_off();
        mark_stopped_from_worker();

        fprintf(stdout, "[emergency-siren-service] pattern finished\n");
        fflush(stdout);

        return NULL;
    }

    while (should_continue(start_ms, duration_sec)) {
        for (i = 0; i < pattern->step_count && should_continue(start_ms, duration_sec); i++) {
            siren_hw_on();
            interruptible_sleep_ms(pattern->steps[i].on_ms, start_ms, duration_sec);

            siren_hw_off();
            interruptible_sleep_ms(pattern->steps[i].off_ms, start_ms, duration_sec);
        }
    }

    siren_hw_off();
    mark_stopped_from_worker();

    fprintf(stdout, "[emergency-siren-service] pattern finished\n");
    fflush(stdout);

    return NULL;
}

int siren_controller_init(void)
{
    return siren_hw_init();
}

void siren_controller_deinit(void)
{
    siren_controller_stop();
    siren_hw_deinit();
}

int siren_controller_stop(void)
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
    current_pattern = SIREN_PATTERN_INVALID;
    current_duration_sec = 0;
    pthread_mutex_unlock(&pattern_lock);

    siren_hw_off();

    return 0;
}

int siren_controller_start_by_id(int siren_id, int duration_sec)
{
    const siren_pattern_def_t *pattern;
    siren_worker_arg_t *thread_arg;
    int ret;

    pattern = find_pattern_by_id((siren_pattern_id_t)siren_id);
    if (!pattern) {
        return -1;
    }

    if (duration_sec <= 0) {
        duration_sec = SIREN_DURATION_INFINITE;
    }

    siren_controller_stop();

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
        current_pattern = SIREN_PATTERN_INVALID;
        current_duration_sec = 0;
        pthread_mutex_unlock(&pattern_lock);

        siren_hw_off();
        return -1;
    }

    return 0;
}