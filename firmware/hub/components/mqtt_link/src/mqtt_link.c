/*
 * mqtt_link.c — Cliente MQTT sobre WSS del HUB-S3
 * [HUB-F-010] publish event/response/hello/status (QoS1)  [AT-01]
 * [HUB-F-011] subscribe command (QoS1, retained)
 * [HUB-F-012] LWT state=offline,reason=lwt, retain
 * [HUB-F-013] auth NVS o anónimo (dev)
 * TLS: bundle de CAs (ISRG X1 / Cloudflare) vía esp_crt_bundle_attach
 */
#include "mqtt_link.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "mqtt_link";

#define TOPIC_BUF_MAX     160
#define LWT_PAYLOAD       "{\"state\":\"offline\",\"reason\":\"lwt\"}"

static struct {
    bool                     initialized;
    bool                     connected;
    mqtt_link_cfg_t          cfg;
    esp_mqtt_client_handle_t client;
    char                     base_topic[TOPIC_BUF_MAX]; /* sin '/' final */
    char                     lwt_topic[TOPIC_BUF_MAX + 16];  /* vida del programa */
    SemaphoreHandle_t        mutex;
} s_mqtt;

/* ---------- Construcción de topics ---------- */
static const char *sub_name(mqtt_link_sub_t s)
{
    switch (s) {
        case MQTT_LINK_SUB_STATUS:   return "status";
        case MQTT_LINK_SUB_EVENT:    return "event";
        case MQTT_LINK_SUB_RESPONSE: return "response";
        case MQTT_LINK_SUB_HELLO:    return "hello";
        case MQTT_LINK_SUB_COMMAND:  return "command";
    }
    return "unknown";
}

static int build_topic(mqtt_link_sub_t sub, char *out, size_t out_sz)
{
    return snprintf(out, out_sz, "%s/%s", s_mqtt.base_topic, sub_name(sub));
}

/* ---------- Handler de eventos MQTT ---------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args; (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "MQTT connected");
        xSemaphoreTake(s_mqtt.mutex, portMAX_DELAY);
        s_mqtt.connected = true;
        xSemaphoreGive(s_mqtt.mutex);

        /* Subscribe .../command QoS1 (retained) [HUB-F-011] */
        char topic[TOPIC_BUF_MAX];
        build_topic(MQTT_LINK_SUB_COMMAND, topic, sizeof(topic));
        esp_mqtt_client_subscribe(s_mqtt.client, topic, 1);

        /* Hello de arranque */
        cJSON *hello = cJSON_CreateObject();
        cJSON_AddStringToObject(hello, "type", "hello");
        cJSON_AddStringToObject(hello, "fw", s_mqtt.cfg.fw_version);
        cJSON_AddNumberToObject(hello, "ts",
                                (double)esp_timer_get_time() / 1e6);
        char *js = cJSON_PrintUnformatted(hello);
        if (js) {
            build_topic(MQTT_LINK_SUB_HELLO, topic, sizeof(topic));
            esp_mqtt_client_publish(s_mqtt.client, topic, js, 0, 1, 0);
            free(js);
        }
        cJSON_Delete(hello);

        if (s_mqtt.cfg.on_evt)
            s_mqtt.cfg.on_evt(MQTT_LINK_EVT_CONNECTED, s_mqtt.cfg.on_evt_ctx);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        xSemaphoreTake(s_mqtt.mutex, portMAX_DELAY);
        s_mqtt.connected = false;
        xSemaphoreGive(s_mqtt.mutex);
        if (s_mqtt.cfg.on_evt)
            s_mqtt.cfg.on_evt(MQTT_LINK_EVT_DISCONNECTED, s_mqtt.cfg.on_evt_ctx);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "subscribed msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA: {
        char topic[TOPIC_BUF_MAX];
        build_topic(MQTT_LINK_SUB_COMMAND, topic, sizeof(topic));
        if (event->topic_len > 0 &&
            (int)strlen(topic) == event->topic_len &&
            memcmp(event->topic, topic, (size_t)event->topic_len) == 0) {
            if (s_mqtt.cfg.on_cmd && event->data && event->data_len > 0) {
                s_mqtt.cfg.on_cmd(event->data, (size_t)event->data_len,
                                  s_mqtt.cfg.on_cmd_ctx);
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        if (s_mqtt.cfg.on_evt)
            s_mqtt.cfg.on_evt(MQTT_LINK_EVT_ERROR, s_mqtt.cfg.on_evt_ctx);
        break;

    default:
        break;
    }
}

/* ---------- API ---------- */
esp_err_t mqtt_link_init(const mqtt_link_cfg_t *cfg)
{
    if (!cfg || !cfg->broker_uri || !cfg->tenant_id ||
        !cfg->site_id || !cfg->hub_mac_str || !cfg->fw_version) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mqtt.initialized) return ESP_ERR_INVALID_STATE;

    s_mqtt.cfg = *cfg;
    snprintf(s_mqtt.base_topic, sizeof(s_mqtt.base_topic),
             "seguimiento360/tenant/%s/site/%s/hub/%s",
             cfg->tenant_id, cfg->site_id, cfg->hub_mac_str);
    snprintf(s_mqtt.lwt_topic, sizeof(s_mqtt.lwt_topic),
             "%s/status", s_mqtt.base_topic);

    s_mqtt.mutex = xSemaphoreCreateMutex();
    if (!s_mqtt.mutex) return ESP_ERR_NO_MEM;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->broker_uri,
        /* TLS: bundle de CAs públicas (ISRG X1) [HUB-F-012] */
        .broker.verification = {
            .crt_bundle_attach = esp_crt_bundle_attach,
        },
        /* LWT retained [HUB-F-012] */
        .session.last_will = {
            .topic   = s_mqtt.lwt_topic,
            .msg     = LWT_PAYLOAD,
            .msg_len = 0,   /* 0 => strlen interno */
            .qos     = 1,
            .retain  = 1,
        },
    };

    /* Auth opcional [HUB-F-013] */
    if (cfg->mqtt_user && strlen(cfg->mqtt_user) > 0) {
        mqtt_cfg.credentials.username = cfg->mqtt_user;
        mqtt_cfg.credentials.authentication.password =
            cfg->mqtt_pass ? cfg->mqtt_pass : "";
    }

    s_mqtt.client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt.client) return ESP_FAIL;

    esp_mqtt_client_register_event(s_mqtt.client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    s_mqtt.initialized = true;
    ESP_LOGI(TAG, "MQTT init: %s", s_mqtt.base_topic);
    return ESP_OK;
}

esp_err_t mqtt_link_start(void)
{
    if (!s_mqtt.initialized) return ESP_ERR_INVALID_STATE;
    if (s_mqtt.cfg.on_evt)
        s_mqtt.cfg.on_evt(MQTT_LINK_EVT_CONNECTING, s_mqtt.cfg.on_evt_ctx);
    return esp_mqtt_client_start(s_mqtt.client);
}

esp_err_t mqtt_link_stop(void)
{
    if (!s_mqtt.initialized) return ESP_ERR_INVALID_STATE;
    esp_mqtt_client_stop(s_mqtt.client);
    esp_mqtt_client_destroy(s_mqtt.client);
    s_mqtt.client = NULL;
    s_mqtt.initialized = false;
    s_mqtt.connected = false;
    return ESP_OK;
}

esp_err_t mqtt_link_publish(mqtt_link_sub_t sub, const char *json_payload,
                            bool retain)
{
    if (!s_mqtt.initialized || !json_payload) return ESP_ERR_INVALID_ARG;
    if (!s_mqtt.connected) return ESP_ERR_INVALID_STATE;

    char topic[TOPIC_BUF_MAX];
    build_topic(sub, topic, sizeof(topic));

    int msg_id = esp_mqtt_client_publish(s_mqtt.client, topic,
                                         json_payload, 0, 1, retain ? 1 : 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

bool mqtt_link_is_connected(void)
{
    bool c;
    xSemaphoreTake(s_mqtt.mutex, portMAX_DELAY);
    c = s_mqtt.connected;
    xSemaphoreGive(s_mqtt.mutex);
    return c;
}
