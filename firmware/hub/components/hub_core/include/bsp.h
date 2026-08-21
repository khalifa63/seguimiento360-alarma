/**
 * @file bsp.h
 * @brief Board Support Package para HUB-S3
 *
 * Specs: A1 v0.2 (tareas), A2 v0.2 (FSMs), A3 v0.1 (memoria), A4 v0.2 (seguridad).
 * Contratos §6 definidos como estructuras base para M2+.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Asignación de pines (A1 v0.2)
 * ========================================================================= */
#define BSP_GPIO_STATUS_LED      GPIO_NUM_4    // LED de estado general
#define BSP_GPIO_POWER_EN        GPIO_NUM_5    // Habilitación de sensores externos
#define BSP_GPIO_TAMPER_INPUT    GPIO_NUM_2    // Sensor de apertura de carcasa
#define BSP_GPIO_FACTORY_BTN     GPIO_NUM_3    // Botón factory reset (10s)
#define BSP_ADC_POWER_LOSS_CHAN  0             // ADC1_CH0 (GPIO1) - detección corte

/* =========================================================================
 * [HUB-F-015] Evento de Tamper (ISR -> xQ_Tamper -> t_logic)
 * ========================================================================= */
typedef enum {
    TAMPER_CASE_OPEN    = 0,  // Apertura de carcasa (GPIO)
    TAMPER_BTN_HOLD_10S = 1   // Botón presionado 10s (factory reset)
} tamper_type_t;

typedef struct {
    uint32_t      timestamp_ms;  // Timestamp del evento (uptime)
    tamper_type_t type;
    uint8_t       system_state;  // Estado de SM_ALARM al momento del evento
} tamper_event_t;

/* =========================================================================
 * Contratos §6 - Estructuras base para M2+ (ESP-NOW, MQTT)
 * Definidas aquí como base de la fundación; sin implementación en M1.
 * ========================================================================= */

// [HUB-IF-001] Frame ESP-NOW v1 (docs/hardware/PROTOCOLO_ESPNOW.md)
#define ESPNOW_FRAME_V1_VERSION  0x01
#define ESPNOW_FRAME_V1_MAGIC    0xA5
#define ESPNOW_MAX_PAYLOAD       250

typedef enum {
    ESPNOW_TYPE_EVENT     = 0x01,
    ESPNOW_TYPE_ACK       = 0x02,
    ESPNOW_TYPE_BATTERY   = 0x03,
    ESPNOW_TYPE_TAMPER    = 0x04,
    ESPNOW_TYPE_HEARTBEAT = 0x05
} espnow_frame_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;    // 0x01
    uint8_t  type;       // espnow_frame_type_t
    uint16_t node_id;    // ID lógico del nodo
    uint8_t  zone;       // 1..32
    uint8_t  state;      // 0 ok | 1 trigger | 2 low_batt | 3 tamper
    uint32_t seq;        // Anti-replay monotónico
    uint16_t crc16;      // CRC16-CCITT
} espnow_frame_v1_t;

// [HUB-IF-002] Topics MQTT (coherentes con servidor Fase 3.2.3)
#define MQTT_TOPIC_PREFIX   "seguimiento360"
#define MQTT_TOPIC_HELLO    "hello"
#define MQTT_TOPIC_COMMAND  "command"
#define MQTT_TOPIC_RESPONSE "response"
#define MQTT_TOPIC_EVENT    "event"
#define MQTT_TOPIC_STATUS   "status"

typedef struct {
    char     topic[96];
    uint8_t  payload[512];
    uint16_t payload_len;
    uint8_t  qos;
    bool     retain;
} mqtt_message_t;

/* =========================================================================
 * API pública BSP
 * ========================================================================= */

/**
 * @brief Inicializa GPIOs, ADC y registra la ISR de tamper.
 *
 * @param xQ_Tamper Cola para enviar eventos de tamper a t_logic.
 * @return ESP_OK en éxito.
 */
esp_err_t bsp_init(QueueHandle_t xQ_Tamper);

/**
 * @brief Controla el LED de estado.
 */
esp_err_t bsp_set_status_led(bool state);

/**
 * @brief Parpadea el LED un número de veces.
 */
esp_err_t bsp_blink_led(uint8_t times, uint32_t delay_ms);

/**
 * @brief Lee el ADC de power_loss y retorna true si hay corte inminente.
 *
 * @return true si voltaje < umbral crítico.
 */
bool bsp_is_power_loss_detected(void);

/**
 * @brief Habilita/deshabilita la alimentación de sensores externos.
 */
esp_err_t bsp_set_power_enable(bool enable);

#ifdef __cplusplus
}
#endif
