/*
 * main.c — HUB-S3 M4 (Provisioning + ESP-NOW + MQTT WSS + SM_ALARM)
 * Orquesta: NVS -> [Provisioning si no hay creds] -> BSP -> SM_CONN -> MQTT.
 *
 * [HUB-F-030] M4: Provisioning SoftAP al inicio si no hay credenciales.
 * [HUB-F-001] arranque ordenado  [AT-01] boot->online <=10s
 *
 * Colas: xQ_EspnowLogic(10), xQ_MqttOut(10), xQ_MqttCmd(5),
 *        xQ_Tamper(5), xQ_TamperAlarm(5)
 * Tareas:
 *   factory        -       (M4: botón BOOT 10s)
 *   t_sys_mon      Core 0, Pri 1  (WDT, tamper, heartbeat LED)   [M1]
 *   t_espnow_rx    Core 0, Pri 24 (lógica ESP-NOW -> JSON)       [M2]
 *   t_wifi_mqtt    Core 1, Pri 15 (publicador + comandos)        [M2]
 *   t_alarm        Core 0, Pri 5  (SM_ALARM FSM)                 [M3]
 *   sm_conn        Core 1, Pri 10 (FSM WiFi)                     [M2]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "driver/gpio.h"

#include "bsp.h"
#include "nvs_manager.h"
#include "espnow_link.h"
#include "mqtt_link.h"
#include "sm_conn.h"
#include "sm_alarm.h"
#include "sm_prov.h"

static const char *TAG = "MAIN";

#define FW_VERSION "0.4.0"

/* [M4] Botón BOOT para factory reset */
#define BOOT_BTN_GPIO  GPIO_NUM_0
#define FACTORY_MS     10000

/* [M2] Canal fijo: 0 = auto-scan. Producción: canal del AP [R1 A6] */
#define M2_FIXED_CHANNEL 0

/* ---------- Colas ---------- */
static QueueHandle_t xQ_EspnowLogic = NULL;
static QueueHandle_t xQ_MqttOut     = NULL;
static QueueHandle_t xQ_MqttCmd     = NULL;
static QueueHandle_t xQ_Tamper      = NULL;
static QueueHandle_t xQ_TamperAlarm = NULL;

typedef struct {
    uint8_t sub;      /* mqtt_link_sub_t */
    char   *json;     /* heap */
    bool    retain;
} mqtt_out_msg_t;

/* =========================================================
 * [M4] Factory reset: BOOT 10s
 * ========================================================= */
static void factory_reset_task(void *arg)
{
    gpio_reset_pin(BOOT_BTN_GPIO);
    gpio_set_direction(BOOT_BTN_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BTN_GPIO, GPIO_PULLUP_ONLY);

    TickType_t pressed_since = 0;
    bool pressing = false;
    while (1) {
        int level = gpio_get_level(BOOT_BTN_GPIO);
        if (level == 0) {  /* BOOT pulsado (pull-up) */
            if (!pressing) {
                pressing = true;
                pressed_since = xTaskGetTickCount();
            } else {
                uint32_t ms = (xTaskGetTickCount() - pressed_since)
                              * portTICK_PERIOD_MS;
                if (ms >= FACTORY_MS) {
                    ESP_LOGW(TAG, "[M4] Factory reset activado (BOOT 10s)");
                    nvs_manager_factory_reset_credentials();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                }
            }
        } else {
            pressing = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* =========================================================
 * Callbacks ESP-NOW y MQTT (M2)
 * ========================================================= */
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
        mqtt_link_publish(MQTT_LINK_SUB_STATUS, js, true);
        free(js);
    }
}

static void mqtt_evt_cb(mqtt_link_evt_t evt, void *ctx)
{
    (void)ctx;
    switch (evt) {
    case MQTT_LINK_EVT_CONNECTED:
        ESP_LOGI(TAG, "[AT-01] MQTT online");
        publish_status();
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

/* =========================================================
 * [M3] Callback SM_ALARM -> MQTT
 * ========================================================= */
static void alarm_publish_cb(const char *json_payload)
{
    mqtt_link_publish(MQTT_LINK_SUB_EVENT, json_payload, false);
}

/* =========================================================
 * t_sys_mon (M1): WDT + tamper + heartbeat
 * ========================================================= */
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
            /* [CR-M3-002] Reenviar tamper a t_alarm */
            if (xQueueSend(xQ_TamperAlarm, &evt, 0) != pdTRUE) {
                ESP_LOGW(TAG, "xQ_TamperAlarm full");
            }
            ESP_LOGI(TAG, "TAMPER type=%d ts=%lu -> t_alarm",
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

/* =========================================================
 * t_espnow_rx (M2): frame -> JSON -> xQ_MqttOut
 * ========================================================= */
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

/* =========================================================
 * t_wifi_mqtt (M2): publicador + comandos -> SM_ALARM (M3)
 * ========================================================= */
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
            ESP_LOGI(TAG, "CMD RX: %s", cmd);
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

/* =========================================================
 * [M3] t_alarm: Core 0, Pri 5, 4096
 * ========================================================= */
static void t_alarm_task(void *arg)
{
    (void)arg;
    tamper_event_t te;
    char *cmd = NULL;

    ESP_LOGI(TAG, "[M3] t_alarm iniciada (estado=%s)",
             sm_alarm_state_str(sm_alarm_get_state()));

    while (1) {
        /* Comandos MQTT desde xQ_MqttCmd (M2 encola char* heap) */
        if (xQueueReceive(xQ_MqttCmd, &cmd, 0) == pdTRUE) {
            cJSON *j = cJSON_Parse(cmd);
            if (j) {
                cJSON *jc = cJSON_GetObjectItem(j, "cmd");
                if (jc && cJSON_IsString(jc)) {
                    if (strcmp(jc->valuestring, "arm") == 0) {
                        ESP_LOGI(TAG, "CMD arm");
                        sm_alarm_process_cmd(SM_ALARM_CMD_ARM);
                    } else if (strcmp(jc->valuestring, "disarm") == 0) {
                        ESP_LOGI(TAG, "CMD disarm");
                        sm_alarm_process_cmd(SM_ALARM_CMD_DISARM);
                    }
                }
                cJSON_Delete(j);
            }
            free(cmd);
        }

        /* Eventos tamper desde xQ_TamperAlarm */
        if (xQueueReceive(xQ_TamperAlarm, &te, 0) == pdTRUE) {
            sm_alarm_notify_event(SM_ALARM_EVT_TAMPER);
        }

        sm_alarm_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* =========================================================
 * app_main
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== HUB-S3 M4 boot === fw=%s", FW_VERSION);

    /* NVS 3 particiones (M1) */
    ESP_ERROR_CHECK(nvs_manager_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* [M4] Factory reset task (botón BOOT) */
    xTaskCreate(factory_reset_task, "factory", 3072, NULL, 3, NULL);

    /* Crear cola de tamper y inicializar BSP (necesario para LED en ambos caminos) */
    xQ_Tamper = xQueueCreate(5, sizeof(tamper_event_t));
    ESP_ERROR_CHECK(bsp_init(xQ_Tamper));

    /* =========================================================
     * [M4] ¿Hay credenciales? → provisioning o flujo normal
     * ========================================================= */
    if (!sm_prov_check_credentials()) {
        ESP_LOGI(TAG, "[M4] Sin credenciales. Entrando en modo provisioning...");
        esp_err_t err = sm_prov_start();   /* bloqueante, hace esp_restart() */
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[M4] Provisioning falló. Reiniciando...");
            esp_restart();
        }
        return;  /* no se alcanza */
    }

    ESP_LOGI(TAG, "[M4] Credenciales presentes. Arrancando flujo M2/M3...");

    /* Resto de colas (M2 + M3) */
    xQ_EspnowLogic = xQueueCreate(10, sizeof(espnow_rx_msg_t));
    xQ_MqttOut     = xQueueCreate(10, sizeof(mqtt_out_msg_t));
    xQ_MqttCmd     = xQueueCreate(5, sizeof(char *));
    xQ_TamperAlarm = xQueueCreate(5, sizeof(tamper_event_t));
    if (!xQ_EspnowLogic || !xQ_MqttOut || !xQ_MqttCmd || !xQ_TamperAlarm) {
        ESP_LOGE(TAG, "FATAL: colas");
        abort();
    }

    /* t_sys_mon (WDT + heartbeat) */
    xTaskCreatePinnedToCore(t_sys_mon_task, "t_sys_mon", 4096, NULL, 1, NULL, 0);

    /* =========================================================
     * [M4] Config desde NVS vía nvs_manager (sin fallback dev)
     * ========================================================= */
    static char ssid[33] = {0}, pass[65] = {0};
    static char tid_str[33] = {0}, sid[33] = {0};
    static char muser[65] = {0}, mpass[65] = {0};

    ESP_ERROR_CHECK(nvs_manager_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)));
    nvs_manager_get_site(tid_str, sizeof(tid_str), sid, sizeof(sid));
    nvs_manager_get_mqtt_auth(muser, sizeof(muser), mpass, sizeof(mpass));

    /* Defaults si no están configurados */
    if (tid_str[0] == '\0') strcpy(tid_str, "0");
    if (sid[0] == '\0') strcpy(sid, "site0");

    ESP_LOGI(TAG, "[M4] Config: SSID='%s' tenant='%s' site='%s' mqtt_user='%s'",
             ssid, tid_str, sid, muser);

    /* MAC del HUB como string */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* ---- SM_CONN (WiFi STA) con credenciales de NVS ---- */
    sm_conn_cfg_t scfg = {
        .wifi_ssid = ssid,
        .wifi_pass = pass,
        .channel   = M2_FIXED_CHANNEL,
    };
    ESP_ERROR_CHECK(sm_conn_init(&scfg));
#if M2_FIXED_CHANNEL > 0
    esp_wifi_set_channel(M2_FIXED_CHANNEL, 0);
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
        .site_id     = sid,
        .hub_mac_str = mac_str,
        .fw_version  = FW_VERSION,
        .mqtt_user   = muser,
        .mqtt_pass   = mpass,
        .on_evt      = mqtt_evt_cb,
        .on_cmd      = mqtt_cmd_cb,
    };
    ESP_ERROR_CHECK(mqtt_link_init(&mcfg));

    /* ---- [M3] SM_ALARM con callback de publicación ---- */
    ESP_ERROR_CHECK(sm_alarm_init(alarm_publish_cb));

    ESP_ERROR_CHECK(sm_conn_start());

    /* Tareas M2 + M3 */
    xTaskCreatePinnedToCore(t_espnow_rx_task, "t_espnow_rx", 4096, NULL, 24, NULL, 0);
    xTaskCreatePinnedToCore(t_wifi_mqtt_task, "t_wifi_mqtt", 8192, NULL, 15, NULL, 1);
    xTaskCreatePinnedToCore(t_alarm_task, "t_alarm", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "[M4] HUB-S3 operativo. ESP-NOW + MQTT + SM_ALARM + Provisioning.");
}
