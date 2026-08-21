/*
 * sm_conn.c — FSM WiFi STA + arranque de MQTT
 * [R1 A6] canal fijo  [R2 A6] backoff exponencial con jitter ±20%
 * [HUB-F-020] transiciones de estado
 */
#include "sm_conn.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"

#include "mqtt_link.h"

static const char *TAG = "sm_conn";

#define BIT_CONNECTED   (1 << 0)
#define BIT_FAIL        (1 << 1)

/* Backoff [R2 A6]: 1,2,4,8,16,32,60 (tope) */
static const uint16_t BACKOFF_TABLE_S[] = { 1, 2, 4, 8, 16, 32, 60 };
#define BACKOFF_STEPS (sizeof(BACKOFF_TABLE_S) / sizeof(BACKOFF_TABLE_S[0]))

static struct {
    bool               initialized;
    sm_conn_cfg_t      cfg;
    sm_conn_state_t    state;
    uint8_t            retry_idx;
    EventGroupHandle_t evgrp;
    esp_netif_t       *sta_netif;
    TaskHandle_t       task;
    bool               mqtt_started;
} s_sm;

static void set_state(sm_conn_state_t st)
{
    if (s_sm.state == st) return;
    s_sm.state = st;
    ESP_LOGI(TAG, "state -> %d", (int)st);
    if (s_sm.cfg.on_state) s_sm.cfg.on_state(st, s_sm.cfg.on_state_ctx);
}

/* ---------- Eventos WiFi / IP ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi associated");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc =
                (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "WiFi disconnected reason=%d",
                     disc ? disc->reason : -1);
            set_state(SM_CONN_STATE_DISCONNECTED);
            if (s_sm.evgrp) xEventGroupSetBits(s_sm.evgrp, BIT_FAIL);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&evt->ip_info.ip));
        if (s_sm.evgrp) xEventGroupSetBits(s_sm.evgrp, BIT_CONNECTED);
    }
}

/* ---------- Jitter ±20% [R2 A6] ---------- */
static uint32_t backoff_with_jitter_ms(uint16_t base_s)
{
    float jitter = ((float)(esp_random() % 401) - 200.0f) / 1000.0f;
    float secs = (float)base_s * (1.0f + jitter);
    if (secs < 0.5f) secs = 0.5f;
    return (uint32_t)(secs * 1000.0f);
}

/* ---------- Tarea FSM ---------- */
static void sm_conn_task(void *arg)
{
    (void)arg;

    while (1) {
        set_state(SM_CONN_STATE_CONNECTING);
        esp_wifi_connect();

        EventBits_t bits = xEventGroupWaitBits(
            s_sm.evgrp,
            BIT_CONNECTED | BIT_FAIL,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(15000));

        if (bits & BIT_CONNECTED) {
            /* Arrancar MQTT una sola vez [AT-07] */
            if (!s_sm.mqtt_started) {
                mqtt_link_start();
                s_sm.mqtt_started = true;
            }
            set_state(SM_CONN_STATE_CONNECTED);
            s_sm.retry_idx = 0;

            /* Esperar caída de conexión */
            xEventGroupWaitBits(s_sm.evgrp, BIT_FAIL, pdTRUE, pdFALSE,
                                portMAX_DELAY);
        } else {
            /* Fallo de asociación / DHCP (timeout 15s) */
            esp_wifi_disconnect();
        }

        /* Backoff con jitter [R2 A6] */
        set_state(SM_CONN_STATE_ERROR_BACKOFF);
        uint16_t base = BACKOFF_TABLE_S[
            s_sm.retry_idx < BACKOFF_STEPS ? s_sm.retry_idx : BACKOFF_STEPS - 1];
        uint32_t ms = backoff_with_jitter_ms(base);
        ESP_LOGI(TAG, "backoff %u ms (base=%us)", (unsigned)ms, base);
        if (s_sm.retry_idx < BACKOFF_STEPS) s_sm.retry_idx++;
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

/* ---------- API ---------- */
esp_err_t sm_conn_init(const sm_conn_cfg_t *cfg)
{
    if (!cfg || !cfg->wifi_ssid) return ESP_ERR_INVALID_ARG;
    if (s_sm.initialized) return ESP_ERR_INVALID_STATE;

    s_sm.cfg = *cfg;
    s_sm.evgrp = xEventGroupCreate();
    if (!s_sm.evgrp) return ESP_ERR_NO_MEM;

    s_sm.sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sm.sta_netif) return ESP_FAIL;

    esp_event_handler_instance_t w, i;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &w);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &i);

    wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&c);
    if (err != ESP_OK) return err;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);   /* credenciales desde NVS propio */
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, cfg->wifi_ssid, sizeof(wc.sta.ssid) - 1);
    if (cfg->wifi_pass && strlen(cfg->wifi_pass)) {
        strncpy((char *)wc.sta.password, cfg->wifi_pass,
                sizeof(wc.sta.password) - 1);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wc.sta.channel = cfg->channel;   /* [R1 A6] canal fijo */

    err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    s_sm.initialized = true;
    set_state(SM_CONN_STATE_DISCONNECTED);
    return ESP_OK;
}

esp_err_t sm_conn_start(void)
{
    if (!s_sm.initialized) return ESP_ERR_INVALID_STATE;
    if (s_sm.task) return ESP_OK;
    /* Pri 10, Core 1 (red) según A1 v0.2 */
    if (xTaskCreatePinnedToCore(sm_conn_task, "sm_conn", 4096, NULL, 10,
                                &s_sm.task, 1) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sm_conn_stop(void)
{
    if (!s_sm.initialized) return ESP_ERR_INVALID_STATE;
    if (s_sm.task) {
        vTaskDelete(s_sm.task);
        s_sm.task = NULL;
    }
    esp_wifi_stop();
    return ESP_OK;
}

esp_err_t sm_conn_force_disconnect(void)
{
    return esp_wifi_disconnect();
}

sm_conn_state_t sm_conn_get_state(void)
{
    return s_sm.state;
}
