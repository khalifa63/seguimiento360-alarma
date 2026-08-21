/*
 * main.c — HUB-S3 M2 (ESP-NOW + MQTT WSS)
 * Integra M1 (BSP + NVS 3 particiones) con M2 (espnow_link, mqtt_link, sm_conn).
 * [HUB-F-001] arranque ordenado  [AT-01] boot->online <=10s
 *
 * Colas: xQ_EspnowLogic(10), xQ_MqttOut(10), xQ_MqttCmd(5), xQ_Tamper(5)
 * Tareas:
 *   t_sys_mon    Core 0, Pri 1  (WDT, tamper, heartbeat LED)   [M1]
 *   t_espnow_rx  Core 0, Pri 24 (lógica ESP-NOW -> JSON)       [A1 v0.2]
 *   t_wifi_mqtt  Core 1, Pri 15 (publicador + comandos)        [A1 v0.2]
 *   sm_conn      Core 1, Pri 10 (FSM WiFi, creada por sm_conn_start)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

#include "bsp.h"
#include "nvs_manager.h"
#include "espnow_link.h"
#include "mqtt_link.h"
#include "sm_conn.h"

static const char *TAG = "MAIN";

#define FW_VERSION "0.2.0"

/* === SOLO DESARROLLO (M4 lo elimina: SoftAP provisioning) === */
#define M2_DEV_FALLBACK  1
#define DEV_WIFI_SSID    "KHALIFA_R"     /* <- TU RED WiFi */
#define DEV_WIFI_PASS    "vito4303"     /* <- TU PASSWORD */
/* 0 = auto-scan (dev). Producción: canal fijo del AP [R1 A6] */
#define M2_FIXED_CHANNEL 0

/* ---------- Colas ---------- */
static QueueHandle_t xQ_EspnowLogic = NULL;
static QueueHandle_t xQ_MqttOut     = NULL;
static QueueHandle_t xQ_MqttCmd     = NULL;
static QueueHandle_t xQ_Tamper      = NULL;

typedef struct {
    uint8_t sub;      /* mqtt_link_sub_t */
    char   *json;     /* heap */
    bool    retain;
} mqtt_out_msg_t;

/* ---------- Config desde NVS (particion 'nvs', ns 'hub_cfg') ---------- */
static esp_err_t cfg_get_str(const char *key, char **out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_CONFIG,
                                            NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t sz = 0;
    err = nvs_get_str(h, key, NULL, &sz);
    if (err == ESP_OK) {
        *out = calloc(1, sz);
        if (*out) {
            err = nvs_get_str(h, key, *out, &sz);
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    nvs_close(h);
    return err;
}

/* ---------- Callback ESP-NOW: encolar hacia t_espnow_rx ---------- */
static void espnow_rx_cb(const espnow_rx_msg_t *msg, void *ctx)
{
    (void)ctx;
    espnow_rx_msg_t copy = *msg;
    if (xQueueSend(xQ_EspnowLogic, &copy, 0) != pdTRUE) {
        ESP_LOGW(TAG, "xQ_EspnowLogic full, drop");
    }
}

/* ---------- Publicación de status (AT-01) ---------- */
static void publish_status(void)
{
    if (!mqtt_link_is_connected()) return;

    char ip_str[16] = "0.0.0.0";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ipi;
        if (esp_netif_get_ip_info(netif, &ipi) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ipi.ip));
        }
    }
    int8_t rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    int64_t up_s = esp_timer_get_time() / 1000000LL;

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", "online");
    cJSON_AddStringToObject(j, "fw", FW_VERSION);
    cJSON_AddStringToObject(j, "ip", ip_str);
    cJSON_AddNumberToObject(j, "rssi", rssi);
    cJSON_AddNumberToObject(j, "uptime", (double)up_s);
    char *js = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (js) {
        mqtt_link_publish(MQTT_LINK_SUB_STATUS, js, true);  /* retained */
        free(js);
    }
}

/* ---------- Callbacks MQTT ---------- */
static void mqtt_evt_cb(mqtt_link_evt_t evt, void *ctx)
{
    (void)ctx;
    switch (evt) {
    case MQTT_LINK_EVT_CONNECTED:
        ESP_LOGI(TAG, "[AT-01] MQTT online");
        publish_status();   /* online inmediato */
        break;
    case MQTT_LINK_EVT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT offline");
        break;
    default:
        break;
    }
}

static void mqtt_cmd_cb(const char *cmd_json, size_t len, void *ctx)
{
    (void)ctx;
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, cmd_json, len);
    copy[len] = 0;
    if (xQueueSend(xQ_MqttCmd, &copy, 0) != pdTRUE) {
        ESP_LOGW(TAG, "xQ_MqttCmd full, drop");
        free(copy);
    }
}

/* ---------- t_sys_mon (M1): WDT + tamper + heartbeat ---------- */
static void t_sys_mon_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    tamper_event_t evt;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t hb = 0;

    while (1) {
        esp_task_wdt_reset();

        if (xQueueReceive(xQ_Tamper, &evt, 0) == pdTRUE) {
            /* M3: transicionar SM_ALARM. Hoy: log + reenvío futuro. */
            ESP_LOGW(TAG, "TAMPER type=%d ts=%lu (M3: SM_ALARM)",
                     evt.type, (unsigned long)evt.timestamp_ms);
        }

        if (bsp_is_power_loss_detected()) {
            ESP_LOGE(TAG, "POWER LOSS: emergency commit NVS");
            nvs_manager_emergency_commit();
        }

        if (++hb >= 10) {   /* pulso LED cada 5s */
            hb = 0;
            bsp_set_status_led(true);
            vTaskDelay(pdMS_TO_TICKS(100));
            bsp_set_status_led(false);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

/* ---------- t_espnow_rx: frame -> JSON -> xQ_MqttOut ---------- */
static void t_espnow_rx_task(void *arg)
{
    (void)arg;
    espnow_rx_msg_t m;
    while (1) {
        if (xQueueReceive(xQ_EspnowLogic, &m, portMAX_DELAY) != pdTRUE) continue;

        cJSON *j = cJSON_CreateObject();
        char mac_str[13];
        snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
                 m.src_mac[0], m.src_mac[1], m.src_mac[2],
                 m.src_mac[3], m.src_mac[4], m.src_mac[5]);
        cJSON_AddStringToObject(j, "src", mac_str);
        cJSON_AddNumberToObject(j, "seq", m.seq);
        cJSON_AddNumberToObject(j, "type", m.type);
        cJSON_AddNumberToObject(j, "len", m.payload_len);

        char *hex = malloc((size_t)m.payload_len * 2 + 1);
        if (hex) {
            for (int i = 0; i < m.payload_len; i++) {
                sprintf(hex + i * 2, "%02X", m.payload[i]);
            }
            cJSON_AddStringToObject(j, "payload", hex);
            free(hex);
        }
        char *js = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);

        if (js) {
            mqtt_out_msg_t om = { .sub = MQTT_LINK_SUB_EVENT,
                                  .json = js, .retain = false };
            if (xQueueSend(xQ_MqttOut, &om, 0) != pdTRUE) {
                ESP_LOGW(TAG, "xQ_MqttOut full, drop");
                free(js);
            }
        }
    }
}

/* ---------- t_wifi_mqtt: publicador + comandos (stub M3) ---------- */
static void t_wifi_mqtt_task(void *arg)
{
    (void)arg;
    mqtt_out_msg_t om;
    char *cmd = NULL;
    int64_t last_status = 0;

    while (1) {
        if (xQueueReceive(xQ_MqttOut, &om, 0) == pdTRUE) {
            if (mqtt_link_is_connected()) {
                mqtt_link_publish((mqtt_link_sub_t)om.sub, om.json, om.retain);
            }
            free(om.json);
        }
        if (xQueueReceive(xQ_MqttCmd, &cmd, 0) == pdTRUE) {
            ESP_LOGI(TAG, "CMD RX (M3->SM_ALARM): %s", cmd);
            free(cmd);
        }
        int64_t now = esp_timer_get_time();
        if (mqtt_link_is_connected() && (now - last_status) > 30000000LL) {
            publish_status();
            last_status = now;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ---------- app_main ---------- */
void app_main(void)
{
    ESP_LOGI(TAG, "=== HUB-S3 M2 boot === fw=%s", FW_VERSION);

    /* NVS 3 particiones (M1) */
    ESP_ERROR_CHECK(nvs_manager_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Colas */
    xQ_EspnowLogic = xQueueCreate(10, sizeof(espnow_rx_msg_t));
    xQ_MqttOut     = xQueueCreate(10, sizeof(mqtt_out_msg_t));
    xQ_MqttCmd     = xQueueCreate(5, sizeof(char *));
    xQ_Tamper      = xQueueCreate(5, sizeof(tamper_event_t));
    if (!xQ_EspnowLogic || !xQ_MqttOut || !xQ_MqttCmd || !xQ_Tamper) {
        ESP_LOGE(TAG, "FATAL: colas");
        abort();
    }

    /* BSP M1 (LED, tamper ISR, ADC) */
    ESP_ERROR_CHECK(bsp_init(xQ_Tamper));

    /* t_sys_mon (WDT + heartbeat) */
    xTaskCreatePinnedToCore(t_sys_mon_task, "t_sys_mon", 4096, NULL, 1, NULL, 0);

    /* ---- Config desde NVS, fallback dev ---- */
    char *ssid = NULL, *pass = NULL, *site = NULL, *muser = NULL, *mpass = NULL;
    cfg_get_str("wifi_ssid", &ssid);
    cfg_get_str("wifi_pass", &pass);
    cfg_get_str("site_id", &site);
    cfg_get_str("mqtt_user", &muser);
    cfg_get_str("mqtt_pass", &mpass);

#if M2_DEV_FALLBACK
    if (!ssid) ssid = strdup("KHALIFA_R");      // ya editado
    if (!pass) pass = strdup("vito4303");      // ya editado
    if (!muser) muser = strdup("hub_s3");
    if (!mpass) mpass = strdup("Hub2026Secure!");
#endif






    if (!ssid || !strlen(ssid)) {
        ESP_LOGE(TAG, "Sin wifi_ssid en NVS. Edita DEV_WIFI_SSID o espera M4.");
        abort();
    }
    if (!site) site = strdup("site0");

    uint32_t tid = 0;
    if (nvs_manager_get_tenant_id(&tid) != ESP_OK) tid = 0;
    char tid_str[12];
    snprintf(tid_str, sizeof(tid_str), "%lu", (unsigned long)tid);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* ---- SM_CONN (WiFi STA) antes que ESP-NOW ---- */
    sm_conn_cfg_t scfg = {
        .wifi_ssid = ssid,
        .wifi_pass = pass,
        .channel   = M2_FIXED_CHANNEL,
    };
    ESP_ERROR_CHECK(sm_conn_init(&scfg));
#if M2_FIXED_CHANNEL > 0
    esp_wifi_set_channel(M2_FIXED_CHANNEL, 0);  /* ESP-NOW activo pre-asoc */
#endif

    /* ---- ESP-NOW link ---- */
    espnow_link_cfg_t encfg = {
        .primary_ch = M2_FIXED_CHANNEL,
        .encrypt    = false,
        .on_rx      = espnow_rx_cb,
    };
    ESP_ERROR_CHECK(espnow_link_init(&encfg));

    /* ---- MQTT link (WSS, INFRA-REAL v1.0) ---- */
    mqtt_link_cfg_t mcfg = {
        .broker_uri  = "wss://mqtt.seguimiento360.com:443/mqtt",
        .tenant_id   = tid_str,
        .site_id     = site,
        .hub_mac_str = mac_str,
        .fw_version  = FW_VERSION,
        .mqtt_user   = muser,
        .mqtt_pass   = mpass,
        .on_evt      = mqtt_evt_cb,
        .on_cmd      = mqtt_cmd_cb,
    };
    ESP_ERROR_CHECK(mqtt_link_init(&mcfg));

    ESP_ERROR_CHECK(sm_conn_start());

    /* Tareas M2 */
    xTaskCreatePinnedToCore(t_espnow_rx_task, "t_espnow_rx", 4096, NULL, 24, NULL, 0);
    xTaskCreatePinnedToCore(t_wifi_mqtt_task, "t_wifi_mqtt", 8192, NULL, 15, NULL, 1);

    ESP_LOGI(TAG, "M2 up: ESP-NOW + MQTT WSS (SM_ALARM stub M3)");
}
