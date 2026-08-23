/*
 * sm_prov.h
 * FSM de Provisioning del HUB-S3.
 * [HUB-F-030] [AT-05] [AT-06]
 *
 * Flujo: CHECK_NVS -> SOFTAP_START -> WAIT_CONFIG -> VALIDATE -> SAVE -> REBOOT
 * Si hay credenciales en NVS, salta directamente a IDLE.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SM_PROV_CHECK_NVS = 0,     // Leer credenciales de NVS 'hub_cfg'
    SM_PROV_SOFTAP_START,      // Arrancar SoftAP si no hay credenciales
    SM_PROV_WAIT_CONFIG,       // Web server activo esperando formulario
    SM_PROV_VALIDATE,          // Probar conexión WiFi STA con creds recibidas
    SM_PROV_SAVE,              // Persistir en NVS si validación OK
    SM_PROV_REBOOT,            // Reiniciar para arrancar en modo normal
    SM_PROV_IDLE,              // Estado terminal: no se requiere provisioning
    SM_PROV_ERROR              // Error irrecuperable
} sm_prov_state_t;

/**
 * @brief Comprueba si hay credenciales válidas en NVS.
 * @return true si hay credenciales (saltar provisioning).
 */
bool sm_prov_check_credentials(void);

/**
 * @brief Arranca la FSM de provisioning (SoftAP + web server + DNS).
 *        Bloquea hasta que se complete o expire el timeout (5 min).
 *        Si tiene éxito, hace esp_restart() internamente.
 */
esp_err_t sm_prov_start(void);

/**
 * @brief Devuelve el estado actual de la FSM (para /status).
 */
sm_prov_state_t sm_prov_get_state(void);

/**
 * @brief Devuelve el estado como string legible.
 */
const char *sm_prov_state_name(sm_prov_state_t s);

/**
 * @brief Callback desde web_config al recibir POST /save.
 *        Thread-safe (usa mutex interno).
 */
void sm_prov_on_credentials_received(void);

#ifdef __cplusplus
}
#endif
