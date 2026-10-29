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
 * Payload v1 por tipo de nodo (HUB-IF-002, v0.3)
 *
 * Son los 7 bytes que el nodo y el hub ya intercambiaban. Se conservan tal cual
 * para que la lógica de negocio del hub no cambie.
 *
 * El último byte es genérico: 'value'. Su significado lo define el event_code
 * (SPEC-02 v0.3): DOOR -> estado de la puerta (0 = Abierto, 1 = Cerrado);
 * ALRM/HEAR del nodo ultrasónico -> distancia en cm (legado); PAIR -> 0.
 * Hasta v0.2 se llamaba 'distance_cm', un nombre que no encajaba con los
 * nuevos tipos de nodo que llegan (apertura, humo, pánico, etc.).
 * ------------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint8_t event_code[4];   /* "PAIR", "DOOR", "HEAR", "ALRM", ... */
    uint8_t seq;             /* contador 0-255, anti-replay */
    uint8_t battery_level;   /* 0-100 % */
    uint8_t value;           /* dato del evento; semántica según event_code */
} espnow_payload_v1_t;

_Static_assert(sizeof(espnow_payload_v1_t) == 7,
               "El payload v1 debe ser de 7 bytes exactos");

/* ---------------------------------------------------------------------------
 * Payload de configuracion (HUB-IF-012)
 *
 * El hub manda al nodo los datos que este necesita para descargar su propio
 * firmware: credenciales WiFi y la URL base del servidor OTA. Va en un frame
 * de tipo ESPNOW_FT_CMD, que admite hasta 229 bytes.
 *
 * El nodo no tiene forma de conocer la WiFi de la casa por si mismo, y sin
 * red no puede descargar una actualizacion: hoy solo se actualiza por cable.
 * ------------------------------------------------------------------------ */
#define ESPNOW_WIFI_SSID_MAX   33   /* 32 + terminador, limite de la IEEE 802.11 */
#define ESPNOW_WIFI_PASS_MAX   65   /* 64 + terminador */
#define ESPNOW_OTA_URL_MAX     96
#define ESPNOW_CONFIG_MAGIC    0xC0

typedef struct __attribute__((packed)) {
    uint8_t  magic;                            /* ESPNOW_CONFIG_MAGIC */
    char     ssid[ESPNOW_WIFI_SSID_MAX];
    char     password[ESPNOW_WIFI_PASS_MAX];
    char     ota_url[ESPNOW_OTA_URL_MAX];
} espnow_payload_config_t;

/* ---------------------------------------------------------------------------
 * Prueba de enlace en tiempo real (HUB-IF-014)
 *
 * La app pregunta "este nodo escucha de verdad?". No basta con que el hub
 * tenga la MAC en su lista: puede estar emparejado pero apagado, fuera de
 * alcance o en otro canal. El hub manda este CMD y el nodo responde un
 * DATA_EVENT con codigo "PONG". Va en el mismo tipo de frame que la
 * configuracion, y el primer byte distingue una cosa de la otra.
 * ------------------------------------------------------------------------ */
#define ESPNOW_PING_MAGIC      0xE7

typedef struct __attribute__((packed)) {
    uint8_t magic;                    /* ESPNOW_PING_MAGIC */
    uint8_t nonce;                    /* se devuelve tal cual en el PONG */
    char    tid[12];                  /* identificador de la prueba, opaco */
} espnow_payload_ping_t;

_Static_assert(sizeof(espnow_payload_ping_t) <= ESPNOW_MAX_PAYLOAD,
               "El payload de prueba no cabe en un frame ESP-NOW");

_Static_assert(sizeof(espnow_payload_config_t) <= ESPNOW_MAX_PAYLOAD,
               "El payload de configuracion no cabe en un frame ESP-NOW");

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
