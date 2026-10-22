/*
 * sm_prov.c — FSM de Provisioning HUB-S3 (v3 FIX)
 * [HUB-F-030] [AT-05] [AT-06]
 *
 * FIX v3:
 *   - start_softap() usa SOLO WIFI_MODE_AP (no APSTA)
 *   - NO se registran event handlers en start_softap()
 *   - validate_wifi_credentials() crea EventGroup + handlers SOLO durante validacion
 *   - Handler con guarda defensiva contra NULL
 *   - Cleanup completo tras validacion
 */
#include "sm_prov.h"
#include "nvs_manager.h"
#include "bsp.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "sm_prov";

#define SOFTAP_CHANNEL     1
#define SOFTAP_PASS        "s360setup"
#define PROV_TIMEOUT_MS    (5 * 60 * 1000)
#define PROV_VALIDATE_MS   20000

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static sm_prov_state_t s_state = SM_PROV_CHECK_NVS;
static EventGroupHandle_t s_wifi_evgrp = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static volatile bool s_credentials_received = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static TaskHandle_t s_led_task = NULL;
static esp_event_handler_instance_t s_wifi_handler_inst = NULL;
static esp_event_handler_instance_t s_ip_handler_inst = NULL;

extern esp_err_t web_config_start(void);
extern esp_err_t web_config_stop(void);

/* =========================================================
 * API publica
 * ========================================================= */
const char *sm_prov_state_name(sm_prov_state_t s)
{
    switch (s) {
        case SM_PROV_CHECK_NVS:      return "CHECK_NVS";
        case SM_PROV_SOFTAP_START:   return "SOFTAP_START";
        case SM_PROV_WAIT_CONFIG:    return "WAIT_CONFIG";
        case SM_PROV_VALIDATE:       return "VALIDATE";
        case SM_PROV_SAVE:           return "SAVE";
        case SM_PROV_REBOOT:         return "REBOOT";
        case SM_PROV_IDLE:           return "IDLE";
        case SM_PROV_ERROR:          return "ERROR";
        default:                     return "UNKNOWN";
    }
}

sm_prov_state_t sm_prov_get_state(void) { return s_state; }

bool sm_prov_check_credentials(void)
{
    bool has = nvs_manager_has_credentials();
    ESP_LOGI(TAG, "NVS tiene credenciales: %s", has ? "SI" : "NO");
    return has;
}

void sm_prov_on_credentials_received(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_credentials_received = true;
        xSemaphoreGive(s_mutex);
    } else {
        s_credentials_received = true;
    }
}

/* =========================================================
 * LED parpadeante (BSP de M1)
 * ========================================================= */
static void led_task(void *arg)
{
    bool on = false;
    while (1) {
        on = !on;
        bsp_set_status_led(on);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void led_start(void)
{
    if (!s_led_task)
        xTaskCreate(led_task, "prov_led", 2048, NULL, 2, &s_led_task);
}

static void led_stop(void)
{
    if (s_led_task) {
        vTaskDelete(s_led_task);
        s_led_task = NULL;
        bsp_set_status_led(false);
    }
}

/* =========================================================
 * SSID SoftAP con MAC
 * ========================================================= */
static void build_ap_ssid(char *out, size_t sz)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, sz, "HUB-S3-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* =========================================================
 * Event handler SOLO activo durante validacion
 * ========================================================= */
static void wifi_validate_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    /* Guarda defensiva: si se dispara fuera de validacion, ignorar */
    if (!s_wifi_evgrp) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_evgrp, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_evgrp, WIFI_CONNECTED_BIT);
    }
}

/* =========================================================
 * Arrancar SOLO SoftAP (sin STA, sin event handlers)
 * ========================================================= */
static esp_err_t start_softap(void)
{
    esp_err_t err;
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_IF && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return err;
    }

    char ssid[33];
    build_ap_ssid(ssid, sizeof(ssid));

    wifi_config_t ap_cfg = {0};
    ap_cfg.ap.channel = SOFTAP_CHANNEL;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = true;
    strncpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(ssid);
    strncpy((char *)ap_cfg.ap.password, SOFTAP_PASS, sizeof(ap_cfg.ap.password) - 1);

    /* MODO AP PURO: no arranca STA, no se conecta a nada */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP: SSID=%s PASS=%s CH=%d", ssid, SOFTAP_PASS, SOFTAP_CHANNEL);
    ESP_LOGI(TAG, "Portal: http://192.168.4.1");
    return ESP_OK;
}

static void stop_wifi(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
}

/* =========================================================
 * Validacion STA (EventGroup + handlers SOLO aqui)
 * ========================================================= */
static bool validate_wifi_credentials(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    if (nvs_manager_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudieron leer credenciales para validar");
        return false;
    }

    ESP_LOGI(TAG, "Validando WiFi STA SSID='%s'...", ssid);

    /* Crear EventGroup ANTES de conectar */
    s_wifi_evgrp = xEventGroupCreate();
    if (!s_wifi_evgrp) {
        ESP_LOGE(TAG, "No se pudo crear EventGroup");
        return false;
    }

    /* Registrar handlers SOLO durante validacion */
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                        &wifi_validate_handler, NULL,
                                        &s_wifi_handler_inst);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_validate_handler, NULL,
                                        &s_ip_handler_inst);

    /* Cambiar a APSTA temporalmente (AP sigue activo para el portal) */
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    /* Configurar STA */
    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);

    /* Conectar */
    esp_wifi_connect();

    /* Esperar resultado */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_evgrp,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(PROV_VALIDATE_MS));

    bool ok = (bits & WIFI_CONNECTED_BIT) != 0;
    ESP_LOGI(TAG, "Validacion WiFi: %s", ok ? "OK" : "FALLO");

    /* Limpiar: desconectar, desregistrar, destruir */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    if (s_wifi_handler_inst) {
        esp_event_handler_instance_unregister(WIFI_EVENT,
            WIFI_EVENT_STA_DISCONNECTED, s_wifi_handler_inst);
        s_wifi_handler_inst = NULL;
    }
    if (s_ip_handler_inst) {
        esp_event_handler_instance_unregister(IP_EVENT,
            IP_EVENT_STA_GOT_IP, s_ip_handler_inst);
        s_ip_handler_inst = NULL;
    }

    /* Volver a modo AP puro */
    esp_wifi_set_mode(WIFI_MODE_AP);

    /* IMPORTANTE: poner a NULL ANTES de destruir */
    EventGroupHandle_t tmp = s_wifi_evgrp;
    s_wifi_evgrp = NULL;
    if (tmp) vEventGroupDelete(tmp);

    return ok;
}

/* =========================================================
 * FSM principal
 * ========================================================= */
esp_err_t sm_prov_start(void)
{
    ESP_LOGI(TAG, "=== Iniciando Provisioning (M4) ===");

    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();

    led_start();

    s_state = SM_PROV_SOFTAP_START;
    if (start_softap() != ESP_OK) {
        s_state = SM_PROV_ERROR;
        led_stop();
        return ESP_FAIL;
    }

    web_config_start();

    /* Bucle: WAIT_CONFIG -> VALIDATE -> (retry o SAVE) */
    while (1) {
        s_state = SM_PROV_WAIT_CONFIG;

        /* Esperar credenciales con timeout */
        TickType_t start = xTaskGetTickCount();
        bool received = false;
        while (!received) {
            vTaskDelay(pdMS_TO_TICKS(500));
            uint32_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            if (elapsed >= PROV_TIMEOUT_MS) {
                ESP_LOGW(TAG, "Timeout provisioning (%d s). Reiniciando SoftAP.",
                         PROV_TIMEOUT_MS / 1000);
                web_config_stop();
                stop_wifi();
                vTaskDelay(pdMS_TO_TICKS(500));
                start_softap();
                web_config_start();
                start = xTaskGetTickCount();
                continue;
            }
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            received = s_credentials_received;
            s_credentials_received = false;
            xSemaphoreGive(s_mutex);
        }

        /* VALIDATE */
        s_state = SM_PROV_VALIDATE;
        bool valid = validate_wifi_credentials();

        if (valid) {
            s_state = SM_PROV_SAVE;
            ESP_LOGI(TAG, "Credenciales validadas y persistidas en NVS.");
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;
        }

        /* Invalido: volver a WAIT_CONFIG */
        ESP_LOGW(TAG, "Credenciales WiFi invalidas. Volviendo a formulario.");
        s_state = SM_PROV_WAIT_CONFIG;
    }

    /* REBOOT */
    s_state = SM_PROV_REBOOT;
    web_config_stop();
    led_stop();
    stop_wifi();
    ESP_LOGI(TAG, "Provisioning completado. Reiniciando en modo normal...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}
