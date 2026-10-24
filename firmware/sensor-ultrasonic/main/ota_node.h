/*
 * ota_node.h — Actualizacion del nodo por el aire (FW-012)
 *
 * El nodo no tiene pila de red propia: solo ESP-NOW. Para poder descargar su
 * firmware recibe del hub las credenciales WiFi y la URL del servidor OTA en
 * un frame de configuracion, se asocia a la red y descarga por HTTPS.
 *
 * La imagen debe estar firmada con la misma clave RSA-3072 que la del hub.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Guarda las credenciales y la URL recibidas del hub, y lanza la descarga.
 * Devuelve ESP_ERR_INVALID_ARG si el payload no trae el magic esperado.
 * La descarga corre en segundo plano; la funcion no bloquea. */
esp_err_t ota_node_start_from_config(const char *ssid,
                                     const char *password,
                                     const char *url);

/* true si hay una actualizacion en curso. Mientras tanto el nodo no debe
 * reiniciarse ni cambiar de estado. */
bool ota_node_in_progress(void);

#ifdef __cplusplus
}
#endif
