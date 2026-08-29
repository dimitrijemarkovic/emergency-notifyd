#include "mqtt_client.h"

#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emergency/log.h"

/* Defaults, used when the environment says nothing. Delivery to a broker on
 * another machine is then a matter of setting EMERGENCY_MQTT_HOST and
 * EMERGENCY_MQTT_PORT, not of rebuilding this service. */
#define MQTT_BROKER_HOST_DEFAULT "localhost"
#define MQTT_BROKER_PORT_DEFAULT 1883
#define MQTT_BROKER_HOST_MAX 128

#define MQTT_ENV_HOST "EMERGENCY_MQTT_HOST"
#define MQTT_ENV_PORT "EMERGENCY_MQTT_PORT"

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

/* Written once by mqtt_client_configure_from_env() before any thread starts,
 * read-only afterwards. */
static char broker_host[MQTT_BROKER_HOST_MAX] = MQTT_BROKER_HOST_DEFAULT;
static int broker_port = MQTT_BROKER_PORT_DEFAULT;
static char broker_desc[MQTT_BROKER_HOST_MAX + 8] = MQTT_BROKER_HOST_DEFAULT ":1883";

void mqtt_client_configure_from_env(void)
{
    const char *host = getenv(MQTT_ENV_HOST);
    const char *port = getenv(MQTT_ENV_PORT);

    if (host && host[0] != '\0') {
        snprintf(broker_host, sizeof(broker_host), "%s", host);
    }

    if (port && port[0] != '\0') {
        int parsed = atoi(port);

        if (parsed > 0 && parsed <= 65535) {
            broker_port = parsed;
        } else {
            emergency_log_warn("ignoring unusable %s=%s, keeping port %d",
                               MQTT_ENV_PORT, port, broker_port);
        }
    }

    snprintf(broker_desc, sizeof(broker_desc), "%s:%d", broker_host, broker_port);

    emergency_log_info("broker configured: %s", broker_desc);
}

const char *mqtt_client_broker_desc(void)
{
    return broker_desc;
}

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

    ret = mosquitto_connect(mosq, broker_host, broker_port, MQTT_KEEPALIVE_SEC);
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
