#pragma once

#define MQTT_RECORD_TYPE_MAX 32
#define MQTT_RECORD_SEVERITY_MAX 16
#define MQTT_RECORD_SOURCE_MAX 64
#define MQTT_RECORD_MESSAGE_MAX 160
#define MQTT_RECORD_STATE_MAX 8

typedef struct {
    char type[MQTT_RECORD_TYPE_MAX];
    char severity[MQTT_RECORD_SEVERITY_MAX];
    char source[MQTT_RECORD_SOURCE_MAX];
    char message[MQTT_RECORD_MESSAGE_MAX];
    char state[MQTT_RECORD_STATE_MAX]; /* "active" or "cleared" */
    long long timestamp;
} mqtt_record_t;

typedef struct {
    int connected;
    unsigned int published;
    unsigned int failed;
    unsigned int queued;
    char last_topic[64];
    char last_state[MQTT_RECORD_STATE_MAX];
} mqtt_publisher_stats_t;

int mqtt_publisher_init(void);
void mqtt_publisher_deinit(void);

/* Copies record into a bounded ring buffer and wakes the worker thread.
 * Never blocks meaningfully and never fails from the caller's perspective --
 * on overflow, the oldest queued record is dropped and counted as failed.
 * Safe to call from a ubus method handler: this is the only thing the ubus
 * thread does, everything else happens on the worker thread. */
void mqtt_publisher_enqueue(const mqtt_record_t *record);

void mqtt_publisher_get_stats(mqtt_publisher_stats_t *out);
