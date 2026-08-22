/*
 * sm_alarm.c — FSM de alarma con debounce y persistencia NVS segregada
 * [HUB-F-005] [AT-03] [AT-04] [AT-08]
 * CR-M3-001: usa nvs_manager de M1 (partición nvs_state, namespace hub_state)
 */
#include "sm_alarm.h"
#include "nvs_manager.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "sm_alarm";

/* [A6 v0.2 R3] debounce 50ms */
#define TAMPER_DEBOUNCE_MS 50

static sm_alarm_state_t     s_state       = SM_ALARM_STATE_DISARMED;
static sm_alarm_publish_cb_t s_publish_cb  = NULL;
static bool                  s_tamper_pend = false;
static int64_t               s_tamper_ts_us = 0;
static SemaphoreHandle_t     s_mutex       = NULL;

static const char *state_names[] = {
    "disarmed", "armed", "triggered", "alarming"
};

const char *sm_alarm_state_str(sm_alarm_state_t s)
{
    if (s > SM_ALARM_STATE_ALARMING) return "unknown";
    return state_names[s];
}

/* [CR-M3-001] Persistencia vía nvs_manager (usa partición nvs_state) */
static void persist_state(void)
{
    esp_err_t err = nvs_manager_set_alarm_state((uint8_t)s_state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo persistir estado: %s", esp_err_to_name(err));
    }
}

/* Publica evento JSON vía callback (t_alarm -> mqtt_link_publish) */
static void publish_event(const char *type, const char *state_str, int zone)
{
    if (!s_publish_cb) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(root, "state", state_str);
    if (zone >= 0) {
        cJSON_AddNumberToObject(root, "zone", zone);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return;

    s_publish_cb(payload);
    free(payload);
}

/* Transición atómica con persistencia y publicación */
static void transition_to(sm_alarm_state_t new_state)
{
    sm_alarm_state_t old = s_state;
    if (old == new_state) return;

    s_state = new_state;
    ESP_LOGI(TAG, "FSM: %s -> %s", state_names[old], state_names[new_state]);

    /* [AT-03][AT-08] persistir en partición nvs_state */
    persist_state();

    /* [AT-04] eventos de entrada a TRIGGERED y ALARMING */
    if (new_state == SM_ALARM_STATE_TRIGGERED) {
        publish_event("tamper", "triggered", -1);
        /* TRIGGERED -> ALARMING inmediato según A2 v0.2 §3 */
        s_state = SM_ALARM_STATE_ALARMING;
        persist_state();
        publish_event("alarm", "alarming", 1);
        ESP_LOGI(TAG, "FSM: triggered -> alarming (auto)");
    }
}

/* API pública */
esp_err_t sm_alarm_init(sm_alarm_publish_cb_t cb)
{
    s_publish_cb = cb;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* [CR-M3-001] Restaurar desde partición nvs_state vía nvs_manager */
    uint8_t persisted = 0xFF;
    esp_err_t err = nvs_manager_get_alarm_state(&persisted);
    if (err == ESP_OK && persisted <= SM_ALARM_STATE_ALARMING) {
        s_state = (sm_alarm_state_t)persisted;
        ESP_LOGI(TAG, "Estado restaurado desde NVS: %s", state_names[s_state]);
    } else {
        s_state = SM_ALARM_STATE_DISARMED;
        persist_state();  /* primera ejecución: escribir default */
    }
    return ESP_OK;
}

esp_err_t sm_alarm_process_cmd(sm_alarm_cmd_t cmd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (cmd == SM_ALARM_CMD_ARM && s_state == SM_ALARM_STATE_DISARMED) {
        /* [AT-03] DISARMED -> ARMED */
        transition_to(SM_ALARM_STATE_ARMED);
    } else if (cmd == SM_ALARM_CMD_DISARM && s_state != SM_ALARM_STATE_DISARMED) {
        /* [AT-08] cualquier estado -> DISARMED */
        s_tamper_pend = false;  /* cancelar debounce si había */
        transition_to(SM_ALARM_STATE_DISARMED);
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t sm_alarm_notify_event(sm_alarm_event_t evt)
{
    if (evt != SM_ALARM_EVT_TAMPER) return ESP_OK;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_tamper_pend) {
        s_tamper_pend    = true;
        s_tamper_ts_us   = esp_timer_get_time();
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t sm_alarm_tick(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* [A6 v0.2 R3] debounce 50ms */
    if (s_tamper_pend) {
        int64_t now = esp_timer_get_time();
        if ((now - s_tamper_ts_us) >= (TAMPER_DEBOUNCE_MS * 1000LL)) {
            s_tamper_pend = false;
            if (s_state == SM_ALARM_STATE_ARMED) {
                /* ARMED -> TRIGGERED (y automáticamente a ALARMING dentro de transition_to) */
                transition_to(SM_ALARM_STATE_TRIGGERED);
            } else {
                ESP_LOGD(TAG, "Tamper descartado (estado=%s)", state_names[s_state]);
            }
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

sm_alarm_state_t sm_alarm_get_state(void)
{
    return s_state;
}
