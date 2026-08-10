#include "mqtt_publisher.h"

#include "mqtt_client.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "emergency/log.h"

#define MQTT_QUEUE_CAPACITY 16
#define MQTT_TOPIC_PREFIX "emergency/alarm/"

/* Single lock over both the queue and the published/failed/connected state,
 * same as siren_controller.c's pattern_lock -- one servicing thread, one
 * worker thread, no reason for more than one lock at this scale. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

static mqtt_record_t queue[MQTT_QUEUE_CAPACITY];
static int queue_head;
static int queue_tail;
static int queue_count;

static pthread_t worker_thread;
static int thread_active;
static int keep_running;

static int connected;
static unsigned int published;
static unsigned int failed;
static char last_topic[64];
static char last_state[MQTT_RECORD_STATE_MAX];

static void build_topic(char *out, size_t out_size, const char *type)
{
    snprintf(out, out_size, "%s%s", MQTT_TOPIC_PREFIX, type);
}

static void build_payload_json(char *out, size_t out_size, const mqtt_record_t *record)
{
    if (strcmp(record->state, "active") == 0) {
        snprintf(out, out_size,
                 "{\"state\":\"active\",\"type\":\"%s\",\"severity\":\"%s\","
                 "\"source\":\"%s\",\"message\":\"%s\",\"timestamp\":%lld}",
                 record->type, record->severity, record->source, record->message,
                 record->timestamp);
    } else {
        /* "cleared": severity/source/message described the original alarm
         * condition, which cancel_alarm does not know afresh -- omitted
         * rather than repeating stale or fabricated values. */
        snprintf(out, out_size,
                 "{\"state\":\"cleared\",\"type\":\"%s\",\"timestamp\":%lld}",
                 record->type, record->timestamp);
    }
}

static void publish_one(const mqtt_record_t *record)
{
    char topic[96];
    char payload[320];
    int ok;

    build_topic(topic, sizeof(topic), record->type);
    build_payload_json(payload, sizeof(payload), record);

    ok = (mqtt_client_publish(topic, payload, 1) == 0);

    pthread_mutex_lock(&lock);
    connected = mqtt_client_is_connected();
    if (ok) {
        published++;
    } else {
        failed++;
    }
    snprintf(last_topic, sizeof(last_topic), "%s", topic);
    snprintf(last_state, sizeof(last_state), "%s", record->state);
    pthread_mutex_unlock(&lock);

    emergency_log_info("publish %s: topic=%s state=%s",
                       ok ? "succeeded" : "failed", topic, record->state);
}

static void *worker_main(void *arg)
{
    (void)arg;

    for (;;) {
        mqtt_record_t record;

        pthread_mutex_lock(&lock);

        while (queue_count == 0 && keep_running) {
            pthread_cond_wait(&queue_cond, &lock);
        }

        if (!keep_running) {
            /* Clean exit condition, checked both before and after waiting --
             * the queue is not persistent (see D31), so shutdown does not
             * try to drain remaining records first: it must stay prompt and
             * bounded even if the broker is unreachable. */
            pthread_mutex_unlock(&lock);
            break;
        }

        record = queue[queue_head];
        queue_head = (queue_head + 1) % MQTT_QUEUE_CAPACITY;
        queue_count--;

        pthread_mutex_unlock(&lock);

        publish_one(&record);
    }

    return NULL;
}

int mqtt_publisher_init(void)
{
    int ret;

    if (mqtt_client_init() != 0) {
        return -1;
    }

    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    connected = 0;
    published = 0;
    failed = 0;
    snprintf(last_topic, sizeof(last_topic), "%s", "none");
    snprintf(last_state, sizeof(last_state), "%s", "none");

    keep_running = 1;

    ret = pthread_create(&worker_thread, NULL, worker_main, NULL);
    if (ret != 0) {
        emergency_log_error("failed to start publisher worker thread");
        keep_running = 0;
        mqtt_client_deinit();
        return -1;
    }

    thread_active = 1;

    return 0;
}

void mqtt_publisher_deinit(void)
{
    pthread_mutex_lock(&lock);
    keep_running = 0;
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&lock);

    if (thread_active) {
        pthread_join(worker_thread, NULL);
        thread_active = 0;
    }

    mqtt_client_deinit();
}

void mqtt_publisher_enqueue(const mqtt_record_t *record)
{
    pthread_mutex_lock(&lock);

    if (queue_count == MQTT_QUEUE_CAPACITY) {
        queue_head = (queue_head + 1) % MQTT_QUEUE_CAPACITY;
        queue_count--;
        failed++;
        emergency_log_warn("queue full, dropped oldest record");
    }

    queue[queue_tail] = *record;
    queue_tail = (queue_tail + 1) % MQTT_QUEUE_CAPACITY;
    queue_count++;

    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&lock);
}

void mqtt_publisher_get_stats(mqtt_publisher_stats_t *out)
{
    pthread_mutex_lock(&lock);
    out->connected = connected;
    out->published = published;
    out->failed = failed;
    out->queued = (unsigned int)queue_count;
    snprintf(out->last_topic, sizeof(out->last_topic), "%s", last_topic);
    snprintf(out->last_state, sizeof(out->last_state), "%s", last_state);
    pthread_mutex_unlock(&lock);
}
