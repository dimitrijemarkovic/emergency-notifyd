#include "mqtt_client.h"

#include <mosquitto.h>
#include <string.h>

#include "emergency/log.h"

/* Broker address/port/client id as single named constants, same pattern as
 * the sysfs paths in siren_hw.c/led_hw.c. Remote delivery is a matter of
 * configuring a bridge in mosquitto.conf, not a code change -- these never
 * need to change for that (see research/dnevnik-odluka.md D32). */
#define MQTT_BROKER_HOST "localhost"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "emergency-mqtt-service"
#define MQTT_KEEPALIVE_SEC 60
#define MQTT_QOS 1

/* Reconnecting for every publish (instead of holding a persistent session
 * open) avoids needing a background network loop to keep a long-lived
 * connection's keepalive alive -- alarms are infrequent, so the extra
 * loopback connect per message is negligible, and "connected" then always
 * reflects a genuinely fresh attempt rather than a possibly-stale session. */
#define MQTT_LOOP_ITERATIONS 5
#define MQTT_LOOP_TIMEOUT_MS 100

static struct mosquitto *mosq;
static int connected;

int mqtt_client_init(void)
{
    mosquitto_lib_init();

    mosq = mosquitto_new(MQTT_CLIENT_ID, true, NULL);
    if (!mosq) {
        emergency_log_error("mosquitto_new failed");
        mosquitto_lib_cleanup();
        return -1;
    }

    connected = 0;

    return 0;
}

void mqtt_client_deinit(void)
{
    if (mosq) {
        mosquitto_destroy(mosq);
        mosq = NULL;
    }

    mosquitto_lib_cleanup();
    connected = 0;
}

int mqtt_client_is_connected(void)
{
    return connected;
}

int mqtt_client_publish(const char *topic, const char *payload_json, int retain)
{
    int ret;
    int i;

    if (!mosq) {
        return -1;
    }

    ret = mosquitto_connect(mosq, MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_KEEPALIVE_SEC);
    if (ret != MOSQ_ERR_SUCCESS) {
        emergency_log_warn("broker connect failed: %s", mosquitto_strerror(ret));
        connected = 0;
        return -1;
    }

    connected = 1;

    ret = mosquitto_publish(mosq, NULL, topic,
                            (int)strlen(payload_json), payload_json,
                            MQTT_QOS, retain);
    if (ret != MOSQ_ERR_SUCCESS) {
        emergency_log_error("publish failed: %s", mosquitto_strerror(ret));
        mosquitto_disconnect(mosq);
        connected = 0;
        return -1;
    }

    /* Drive the network loop briefly so the QoS 1 PUBLISH/PUBACK handshake
     * actually completes before disconnecting -- without this, disconnecting
     * immediately would downgrade the effective delivery guarantee to "fire
     * and forget", defeating the point of requesting QoS 1. */
    for (i = 0; i < MQTT_LOOP_ITERATIONS; i++) {
        mosquitto_loop(mosq, MQTT_LOOP_TIMEOUT_MS, 1);
    }

    mosquitto_disconnect(mosq);

    return 0;
}
