/**
 * @file nvs_manager.h
 * @brief Gestor de NVS para HUB-S3 (3 particiones segregadas - A3 v0.1)
 *
 * - nvs:        configuración general (WiFi, MQTT, tenant)
 * - nvs_state:  estado de alarma, flags de sistema
 * - nvs_nodes:  contexto de hasta 50 nodos ESP-NOW (LMK, seq)
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Nombres de particiones (deben coincidir con partitions.csv)
 * ========================================================================= */
#define NVS_PART_CONFIG   "nvs"
#define NVS_PART_STATE    "nvs_state"
#define NVS_PART_NODES    "nvs_nodes"

/* =========================================================================
 * Namespaces
 * ========================================================================= */
#define NVS_NS_CONFIG     "hub_cfg"
#define NVS_NS_STATE      "hub_state"
#define NVS_NS_NODES      "hub_nodes"

/* =========================================================================
 * Claves de configuración
 * ========================================================================= */
#define NVS_KEY_DEV_ID      "dev_id"
#define NVS_KEY_TENANT_ID   "tenant_id"
#define NVS_KEY_WIFI_SSID   "wifi_ssid"
#define NVS_KEY_WIFI_PASS   "wifi_pass"

/* Claves de estado */
#define NVS_KEY_ALARM_STATE "alarm_st"

/* =========================================================================
 * API pública
 * ========================================================================= */

/**
 * @brief Inicializa las 3 particiones NVS con auto-reparación.
 *
 * @return ESP_OK en éxito.
 */
esp_err_t nvs_manager_init(void);

/**
 * @brief Guarda el Device ID en la partición de configuración.
 */
esp_err_t nvs_manager_set_device_id(const char *dev_id);

/**
 * @brief Lee el Device ID desde la partición de configuración.
 */
esp_err_t nvs_manager_get_device_id(char *dev_id, size_t max_len);

/**
 * @brief Guarda el Tenant ID (uint32_t).
 */
esp_err_t nvs_manager_set_tenant_id(uint32_t tenant_id);

/**
 * @brief Lee el Tenant ID.
 */
esp_err_t nvs_manager_get_tenant_id(uint32_t *tenant_id);

/**
 * @brief Guarda el estado de alarma (0=disarmed, 1=armed).
 *
 * @note Usa partición nvs_state para wear-leveling segregado (A3 v0.1).
 */
esp_err_t nvs_manager_set_alarm_state(uint8_t state);

/**
 * @brief Lee el estado de alarma.
 */
esp_err_t nvs_manager_get_alarm_state(uint8_t *state);

/**
 * @brief Fuerza commit de las 3 particiones (emergencia power_loss).
 *
 * @note [HUB-NF-003] Se invoca desde t_sys_mon al detectar corte de energía.
 */
esp_err_t nvs_manager_emergency_commit(void);

/**
 * @brief Borra las 3 particiones NVS (factory reset - [HUB-F-020]).
 */
esp_err_t nvs_manager_factory_reset(void);

#ifdef __cplusplus
}
#endif

/* =========================================================
 * M4: Provisioning — credenciales WiFi / Site / MQTT auth
 * [HUB-F-030] [AT-05] [AT-06]
 * ========================================================= */
esp_err_t nvs_manager_set_wifi(const char *ssid, const char *pass);
esp_err_t nvs_manager_get_wifi(char *ssid, size_t ssid_len,
                               char *pass, size_t pass_len);
esp_err_t nvs_manager_set_site(const char *tenant_id, const char *site_id);
esp_err_t nvs_manager_get_site(char *tid, size_t tid_len,
                               char *sid, size_t sid_len);
esp_err_t nvs_manager_set_mqtt_auth(const char *user, const char *pass);
esp_err_t nvs_manager_get_mqtt_auth(char *user, size_t u_len,
                                    char *pass, size_t p_len);
bool      nvs_manager_has_credentials(void);
esp_err_t nvs_manager_factory_reset_credentials(void);  /* borra solo hub_cfg */
