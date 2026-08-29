#pragma once

/* Resolves the broker address from the environment, EMERGENCY_MQTT_HOST and
 * EMERGENCY_MQTT_PORT, falling back to localhost:1883 when either is unset or
 * unusable. Call once from main(), before the publisher thread starts: after
 * that the resolved address is read from two threads and never written again.
 * Sending alarms to a broker on another machine is then a matter of setting
 * these two variables, with no change to this code. */
void mqtt_client_configure_from_env(void);

/* "host:port" of the broker this service will use. Valid after
 * mqtt_client_configure_from_env(), and safe to call from the ubus thread. */
const char *mqtt_client_broker_desc(void);

int mqtt_client_init(void);
void mqtt_client_deinit(void);

/* Whether the most recent publish attempt succeeded in reaching the broker. */
int mqtt_client_is_connected(void);

/* Connects, publishes payload_json (NUL-terminated) to topic at QoS 1, drives
 * the network loop briefly so the QoS 1 handshake actually completes, then
 * disconnects. Synchronous and bounded -- safe to call only from the
 * publisher worker thread, never from a ubus method handler (see
 * research/dnevnik-odluka.md D30). Returns 0 on success. retain marks the
 * message as the topic's last-known state for late subscribers. */
int mqtt_client_publish(const char *topic, const char *payload_json, int retain);
