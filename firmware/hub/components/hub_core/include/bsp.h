/**
 * @file bsp.h
 * @brief Board Support Package para HUB-S3
 *
 * Specs: A1 v0.2, A2 v0.2, A3 v0.1, A4 v0.2.
 *
 * CR-PROTO-001 (M2): los contratos §6 (frame ESP-NOW v1, topics MQTT)
 * viven ahora en espnow_link.h y mqtt_link.h. Este header solo cubre BSP.
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
#define BSP_GPIO_POWER_EN        GPIO_NUM_5    // Alimentación sensores externos
#define BSP_GPIO_TAMPER_INPUT    GPIO_NUM_2    // Apertura de carcasa
#define BSP_GPIO_FACTORY_BTN     GPIO_NUM_3    // Botón factory reset (10s)
#define BSP_ADC_POWER_LOSS_CHAN  0             // ADC1_CH0 (GPIO1)

/* =========================================================================
 * [HUB-F-015] Evento de Tamper (ISR -> xQ_Tamper -> t_sys_mon)
 * ========================================================================= */
typedef enum {
    TAMPER_CASE_OPEN    = 0,  // Apertura de carcasa (GPIO)
    TAMPER_BTN_HOLD_10S = 1   // Botón presionado 10s (factory reset)
} tamper_type_t;

typedef struct {
    uint32_t      timestamp_ms;  // Uptime del evento
    tamper_type_t type;
    uint8_t       system_state;  // Estado SM_ALARM al momento del evento
} tamper_event_t;

/* =========================================================================
 * API pública BSP
 * ========================================================================= */
esp_err_t bsp_init(QueueHandle_t xQ_Tamper);
esp_err_t bsp_set_status_led(bool state);
esp_err_t bsp_blink_led(uint8_t times, uint32_t delay_ms);
bool bsp_is_power_loss_detected(void);
esp_err_t bsp_set_power_enable(bool enable);

#ifdef __cplusplus
}
#endif
