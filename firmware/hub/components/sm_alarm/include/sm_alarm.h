/*
 * sm_alarm.h — FSM de alarma del HUB-S3
 * [A2 v0.2 §3] estados DISARMED/ARMED/TRIGGERED/ALARMING
 * [A6 v0.2 R3] debounce 50ms de tamper
 * [A3 v0.1] persistencia en partición nvs_state
 * [M3/FASE3] Procesamiento de eventos ESP-NOW a JSON estructurado
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SM_ALARM_STATE_DISARMED  = 0,
    SM_ALARM_STATE_ARMED     = 1,
    SM_ALARM_STATE_TRIGGERED = 2,
    SM_ALARM_STATE_ALARMING  = 3,
} sm_alarm_state_t;

typedef enum {
    SM_ALARM_CMD_NONE = 0,
    SM_ALARM_CMD_ARM,
    SM_ALARM_CMD_DISARM,
} sm_alarm_cmd_t;

typedef enum {
    SM_ALARM_EVT_NONE = 0,
    SM_ALARM_EVT_TAMPER,
    SM_ALARM_EVT_ESPNOW_DATA, /* Nuevo para FASE 3 */
} sm_alarm_event_t;

typedef void (*sm_alarm_publish_cb_t)(const char *json_payload);

esp_err_t sm_alarm_init(sm_alarm_publish_cb_t publish_cb);
esp_err_t sm_alarm_process_cmd(sm_alarm_cmd_t cmd);
esp_err_t sm_alarm_notify_event(sm_alarm_event_t evt);
esp_err_t sm_alarm_tick(void);
sm_alarm_state_t sm_alarm_get_state(void);
const char *sm_alarm_state_str(sm_alarm_state_t s);

/* [FASE 3] Procesa datos crudos de ESP-NOW y publica JSON estructurado a MQTT */
esp_err_t sm_alarm_process_espnow_data(const uint8_t *src_mac, uint16_t seq, 
                                       const uint8_t *payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif
