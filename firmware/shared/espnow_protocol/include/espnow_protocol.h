/*
 * espnow_protocol.h — Contrato del frame ESP-NOW v1
 *
 * Fuente de verdad compartida entre el hub y los nodos. Define el formato del
 * frame y el payload, y ofrece las funciones de construcción y validación.
 *
 * Antes de esto, el contrato vivía solo en el componente del hub
 * (hub/components/espnow_link) y el nodo no podía incluirlo. El byte de
 * distancia llegó a llamarse 'distance_cm' en el hub y 'reserved' en el nodo:
 * mismo offset, distinto nombre, sin que el compilador pudiera detectarlo.
 *
 * Contrato: docs/firmware/specs/02-interfaces.md (HUB-IF-001 y HUB-IF-002).
 * Un cambio aquí rompe el enlace con los nodos ya desplegados.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Constantes del frame
 * ------------------------------------------------------------------------ */
#define ESPNOW_FRAME_MAGIC      0xA5
#define ESPNOW_FRAME_VERSION    0x01

/* Cabecera: magic + version + frame_type + seq + src_mac + dst_mac + len */
#define ESPNOW_HEADER_SIZE      19
#define ESPNOW_CRC_SIZE         2
#define ESPNOW_FRAME_OVERHEAD   (ESPNOW_HEADER_SIZE + ESPNOW_CRC_SIZE)

/* Payload máximo: el límite de ESP-NOW son 250 bytes */
#define ESPNOW_MAX_PAYLOAD      (250 - ESPNOW_FRAME_OVERHEAD)

/* ---------------------------------------------------------------------------
 * Tipos de frame
 * ------------------------------------------------------------------------ */
typedef enum {
    ESPNOW_FT_HELLO      = 0x00,
    ESPNOW_FT_DATA_EVENT = 0x01,
    ESPNOW_FT_ACK        = 0x02,
    ESPNOW_FT_CMD        = 0x03,
    ESPNOW_FT_BEACON     = 0x04,
} espnow_frame_type_t;

/* ---------------------------------------------------------------------------
 * Payload v1 por tipo de nodo (HUB-IF-002)
 *
 * Son los 7 bytes que el nodo y el hub ya intercambiaban. Se conservan tal cual
 * para que la lógica de negocio del hub no cambie.
 *
 * El último byte se llama 'distance_cm' en ambos lados. En el nodo se llamaba
 * 'reserved', lo que invitaba a reutilizarlo y romper la telemetría sin aviso.
 * ------------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint8_t event_code[4];   /* "PAIR", "ALRM", "HEAR" */
    uint8_t seq;             /* contador 0-255, anti-replay */
    uint8_t battery_level;   /* 0-100 % */
    uint8_t distance_cm;     /* distancia medida, 0 si no aplica */
} espnow_payload_v1_t;

_Static_assert(sizeof(espnow_payload_v1_t) == 7,
               "El payload v1 debe ser de 7 bytes exactos");

/* ---------------------------------------------------------------------------
 * Frame en memoria (packed)
 * ------------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  frame_type;
    uint16_t seq;
    uint8_t  src_mac[6];
    uint8_t  dst_mac[6];
    uint16_t payload_len;
    uint8_t  payload[ESPNOW_MAX_PAYLOAD];
    uint16_t crc16;
} espnow_frame_t;

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------ */

/* CRC16-CCITT (polinomio 0x1021, semilla 0xFFFF) sobre la cabecera y el
 * payload, excluyendo el propio campo crc16. */
uint16_t espnow_crc16_ccitt(const uint8_t *data, size_t len);

/* Construye un frame listo para enviar. Calcula el CRC y valida longitudes.
 * Devuelve ESP_ERR_INVALID_ARG si payload_len supera ESPNOW_MAX_PAYLOAD. */
esp_err_t espnow_build_frame(espnow_frame_t *out,
                             espnow_frame_type_t type,
                             uint16_t seq,
                             const uint8_t *src_mac,
                             const uint8_t *dst_mac,
                             const uint8_t *payload,
                             uint16_t payload_len);

/* Valida magic, version, longitud declarada y CRC.
 * Devuelve ESP_OK solo si el frame es íntegro y del tipo esperado. */
esp_err_t espnow_validate_frame(const uint8_t *data,
                                size_t len,
                                espnow_frame_t *out);

/* Tamaño total del frame en bytes según su payload_len. */
size_t espnow_frame_size(uint16_t payload_len);

#ifdef __cplusplus
}
#endif
