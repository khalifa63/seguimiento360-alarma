/*
 * pairing.h — Vinculación del nodo con el hub (FW-008)
 *
 * El nodo aprende y conserva la MAC del hub en NVS, en vez de tenerla
 * compilada. Asi un nodo no necesita recompilarse si cambia el hub: la
 * actualiza durante el emparejamiento.
 *
 * La MAC es la del SoftAP del hub, que es la interfaz por la que el hub
 * registra los peers (hub/main.c: ifidx = ESP_IF_WIFI_AP).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lee la MAC del hub de NVS.
 * Devuelve ESP_OK y copia 6 bytes si existe; ESP_ERR_NOT_FOUND si el nodo
 * todavia no esta vinculado. */
esp_err_t pairing_load_hub_mac(uint8_t out_mac[6]);

/* Guarda la MAC del hub en NVS. Solo escribe si cambio, para no desgastar
 * la flash en cada ACK. */
esp_err_t pairing_save_hub_mac(const uint8_t mac[6]);

/* true si el nodo ya tiene hub asignado. */
bool pairing_is_linked(void);

/* Marca que la MAC recibida es distinta de la guardada. */
bool pairing_mac_changed(const uint8_t mac[6]);

/* Olvida el hub guardado y deja el nodo como recien salido de fabrica.
 * Lo usa la vigilancia del enlace cuando el hub deja de responder: sin
 * esto un hub sustituido dejaba al nodo enviando a una MAC inexistente. */
esp_err_t pairing_forget_hub_mac(void);

#ifdef __cplusplus
}
#endif
