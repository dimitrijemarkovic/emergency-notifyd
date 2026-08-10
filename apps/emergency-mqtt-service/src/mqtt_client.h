#pragma once

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
