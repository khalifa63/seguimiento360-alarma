/*
 * mqtt_link.h — Cliente MQTT WSS del HUB-S3
 * [HUB-F-010] publicación de eventos/estado  [AT-01] [AT-07]
 * [HUB-F-011] suscripción a comandos retenidos
 * [HUB-F-012] LWT offline  [HUB-F-013] auth NVS o anónimo
 * Topics según contratos §6 (INFRA-REAL v1.0)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Subtópicos bajo seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/ */
typedef enum {
    MQTT_LINK_SUB_STATUS,    /* pub (retain) + LWT */
    MQTT_LINK_SUB_EVENT,     /* pub */
    MQTT_LINK_SUB_RESPONSE,  /* pub */
    MQTT_LINK_SUB_HELLO,     /* pub */
    MQTT_LINK_SUB_COMMAND,   /* sub (retained) */
} mqtt_link_sub_t;

typedef enum {
    MQTT_LINK_EVT_DISCONNECTED,
    MQTT_LINK_EVT_CONNECTING,
    MQTT_LINK_EVT_CONNECTED,
    MQTT_LINK_EVT_ERROR,
} mqtt_link_evt_t;

typedef void (*mqtt_link_evt_cb_t)(mqtt_link_evt_t evt, void *ctx);
typedef void (*mqtt_link_cmd_cb_t)(const char *cmd_json, size_t len, void *ctx);

typedef struct {
    const char *broker_uri;       /* "wss://mqtt.seguimiento360.com:443/mqtt" */
    const char *tenant_id;
    const char *site_id;
    const char *hub_mac_str;      /* "AABBCCDDEEFF" */
    const char *fw_version;       /* "x.y.z" */
    const char *mqtt_user;        /* NULL => anónimo (dev) */
    const char *mqtt_pass;        /* NULL => anónimo (dev) */
    mqtt_link_evt_cb_t on_evt;
    void              *on_evt_ctx;
    mqtt_link_cmd_cb_t on_cmd;
    void              *on_cmd_ctx;
} mqtt_link_cfg_t;

esp_err_t mqtt_link_init(const mqtt_link_cfg_t *cfg);
esp_err_t mqtt_link_start(void);
esp_err_t mqtt_link_stop(void);

/* Publica JSON en el sub-tópico indicado. QoS1. */
esp_err_t mqtt_link_publish(mqtt_link_sub_t sub,
                            const char *json_payload,
                            bool retain);

/* true si conectado y suscrito */
bool mqtt_link_is_connected(void);

#ifdef __cplusplus
}
#endif
