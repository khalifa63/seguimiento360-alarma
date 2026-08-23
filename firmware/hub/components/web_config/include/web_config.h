/*
 * web_config.h
 * Servidor HTTP de configuración + DNS cautivo.
 * [HUB-F-030] [AT-05]
 * Endpoints: GET / (formulario), POST /save, GET /status
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arranca el servidor HTTP en puerto 80 y el DNS cautivo en 53.
 */
esp_err_t web_config_start(void);

/**
 * @brief Detiene ambos servicios.
 */
esp_err_t web_config_stop(void);

/* API interna del DNS cautivo (dns_server.c) */
esp_err_t dns_server_start(void);
void      dns_server_stop(void);

#ifdef __cplusplus
}
#endif
