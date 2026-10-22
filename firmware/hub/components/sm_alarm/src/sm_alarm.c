/*
 * sm_alarm.c — FSM de alarma del HUB-S3
 * [FASE 3] Integración con JSON estructurado para InfluxDB/Node-RED
 */
#include "sm_alarm.h"
#include "nvs_manager.h" // Para leer estado si es necesario
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sm_alarm";

static sm_alarm_publish_cb_t s_publish_cb = NULL;
static sm_alarm_state_t s_state = SM_ALARM_STATE_DISARMED;
static uint8_t s_hub_mac[6];

const char *sm_alarm_state_str(sm_alarm_state_t s) {
    switch (s) {
        case SM_ALARM_STATE_DISARMED: return "disarmed";
        case SM_ALARM_STATE_ARMED: return "armed";
        case SM_ALARM_STATE_TRIGGERED: return "triggered";
        case SM_ALARM_STATE_ALARMING: return "alarming";
        default: return "unknown";
    }
}

sm_alarm_state_t sm_alarm_get_state(void) {
    return s_state;
}

esp_err_t sm_alarm_init(sm_alarm_publish_cb_t publish_cb) {
    if (!publish_cb) return ESP_ERR_INVALID_ARG;
    s_publish_cb = publish_cb;
    
    // Obtener MAC del HUB para incluir en el JSON
    esp_read_mac(s_hub_mac, ESP_MAC_WIFI_STA);
    
    // TODO: Restaurar estado desde NVS (nvs_manager_get_alarm_state)
    s_state = SM_ALARM_STATE_DISARMED; 
    
    ESP_LOGI(TAG, "Iniciado. Estado inicial: %s", sm_alarm_state_str(s_state));
    return ESP_OK;
}

esp_err_t sm_alarm_process_cmd(sm_alarm_cmd_t cmd) {
    if (cmd == SM_ALARM_CMD_ARM) {
        s_state = SM_ALARM_STATE_ARMED;
        ESP_LOGI(TAG, "Comando: ARMED");
    } else if (cmd == SM_ALARM_CMD_DISARM) {
        s_state = SM_ALARM_STATE_DISARMED;
        ESP_LOGI(TAG, "Comando: DISARMED");
    }
    // TODO: Persistir en NVS aquí
    return ESP_OK;
}

esp_err_t sm_alarm_notify_event(sm_alarm_event_t evt) {
    if (evt == SM_ALARM_EVT_TAMPER) {
        ESP_LOGW(TAG, "Evento TAMPER detectado!");
        // Aquí se podría cambiar a TRIGGERED y publicar
    }
    return ESP_OK;
}

esp_err_t sm_alarm_tick(void) {
    // Lógica de debounce o timeouts si se requiere en el futuro
    return ESP_OK;
}

/* =========================================================================
 * [FASE 3] Generación de JSON estructurado para ESP-NOW
 * ========================================================================= */
esp_err_t sm_alarm_process_espnow_data(const uint8_t *src_mac, uint16_t seq, 
                                       const uint8_t *payload, uint16_t payload_len) 
{
    if (!s_publish_cb) return ESP_ERR_INVALID_STATE;

    // 1. Formatear MACs
    char node_mac_str[13], hub_mac_str[13];
    snprintf(node_mac_str, sizeof(node_mac_str), "%02X%02X%02X%02X%02X%02X",
             src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
    snprintf(hub_mac_str, sizeof(hub_mac_str), "%02X%02X%02X%02X%02X%02X",
             s_hub_mac[0], s_hub_mac[1], s_hub_mac[2], s_hub_mac[3], s_hub_mac[4], s_hub_mac[5]);

    // 2. Formatear payload (ASCII si es imprimible, sino Hex)
    char payload_str[128] = {0};
    bool is_printable = true;
    uint16_t len_to_copy = (payload_len < sizeof(payload_str) - 1) ? payload_len : sizeof(payload_str) - 1;
    
    for (int i = 0; i < len_to_copy; i++) {
        if (payload[i] < 32 || payload[i] > 126) {
            is_printable = false;
            break;
        }
    }

    if (is_printable && len_to_copy > 0) {
        memcpy(payload_str, payload, len_to_copy);
    } else {
        for (int i = 0; i < len_to_copy; i++) {
            sprintf(payload_str + (i * 2), "%02X", payload[i]);
        }
    }

    // 3. Construir JSON compatible con Node-RED -> InfluxDB
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "measurement", "alarm_event");
    
    cJSON *tags = cJSON_AddObjectToObject(root, "tags");
    cJSON_AddStringToObject(tags, "device_id", node_mac_str);
    cJSON_AddStringToObject(tags, "device_type", "esp32_node");
    cJSON_AddStringToObject(tags, "hub_mac", hub_mac_str);
    
    cJSON *fields = cJSON_AddObjectToObject(root, "fields");
    cJSON_AddNumberToObject(fields, "seq", seq);
    cJSON_AddStringToObject(fields, "event_code", len_to_copy >= 4 ? (const char*)payload : "UNK");
    cJSON_AddStringToObject(fields, "message", payload_str);
    cJSON_AddStringToObject(fields, "system_state", sm_alarm_state_str(s_state));
    
    cJSON_AddNumberToObject(root, "timestamp", (double)(esp_timer_get_time() / 1000)); // ms

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        // El callback ya apunta a mqtt_link_publish(MQTT_LINK_SUB_EVENT, json_str, false)
        s_publish_cb(json_str);
        ESP_LOGI(TAG, "Evento ESP-NOW publicado a MQTT (seq: %u, node: %s)", seq, node_mac_str);
        free(json_str);
    } else {
        ESP_LOGE(TAG, "Fallo al asignar memoria para JSON de alarma");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
