/*
 * espnow_link.h — Capa ESP-NOW del HUB-S3
 * [HUB-F-006] recepción + ACK <50ms  [AT-02]
 * [HUB-F-007] CRC16-CCITT + anti-replay por MAC
 *
 * CR-PROTO-001: este frame v1 (con MAC+payload) sustituye al stub
 * espnow_frame_v1_t de bsp.h (M1). PROTOCOLO_ESPNOW.md se actualizará
 * en el commit de M2.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Constantes de frame v1 ---------- */
#define ESPNOW_FRAME_MAGIC         0xA5
#define ESPNOW_FRAME_VERSION       0x01
#define ESPNOW_MAX_PAYLOAD         215   /* 250 - 21 (hdr+crc) */
#define ESPNOW_HEADER_SIZE         19    /* magic+ver+type+seq+src+dst+len */
#define ESPNOW_CRC_SIZE            2
#define ESPNOW_FRAME_OVERHEAD      (ESPNOW_HEADER_SIZE + ESPNOW_CRC_SIZE)

/* Tipos de frame */
typedef enum {
    ESPNOW_FT_HELLO      = 0x00,
    ESPNOW_FT_DATA_EVENT = 0x01,
    ESPNOW_FT_ACK        = 0x02,
    ESPNOW_FT_CMD        = 0x03,
    ESPNOW_FT_BEACON     = 0x04,
} espnow_frame_type_t;

/* ---------- Frame en memoria (packed) ---------- */
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

/* ---------- Mensaje encolado hacia lógica superior ---------- */
typedef struct {
    espnow_frame_type_t type;
    uint16_t            seq;
    uint8_t             src_mac[6];
    uint16_t            payload_len;
    uint8_t             payload[ESPNOW_MAX_PAYLOAD];
    int64_t             rx_timestamp_us;  /* para medir AT-02 */
} espnow_rx_msg_t;

/* ---------- Callbacks ---------- */
/* Frame válido y no duplicado. El ACK ya fue enviado inline (AT-02). */
typedef void (*espnow_link_rx_cb_t)(const espnow_rx_msg_t *msg, void *ctx);

/* Confirmación de envío propio */
typedef void (*espnow_link_tx_done_cb_t)(const uint8_t *mac_addr,
                                         esp_now_send_status_t status,
                                         void *ctx);

/* ---------- Configuración ---------- */
typedef struct {
    uint8_t  primary_ch;            /* [R1 A6] canal fijo compartido con WiFi */
    uint8_t  pmk[16];               /* opcional, ceros = sin PMK */
    bool     encrypt;
    espnow_link_rx_cb_t      on_rx;
    void                    *on_rx_ctx;
    espnow_link_tx_done_cb_t on_tx_done;
    void                    *on_tx_done_ctx;
} espnow_link_cfg_t;

/* ---------- API pública ---------- */
esp_err_t espnow_link_init(const espnow_link_cfg_t *cfg);
esp_err_t espnow_link_send_ack(const uint8_t *dst_mac, uint16_t seq);
esp_err_t espnow_link_send_frame(const espnow_frame_t *frame);
esp_err_t espnow_link_build_frame(espnow_frame_t *out,
                                  espnow_frame_type_t type,
                                  uint16_t seq,
                                  const uint8_t *src_mac,
                                  const uint8_t *dst_mac,
                                  const uint8_t *payload,
                                  uint16_t payload_len);
uint16_t  espnow_link_crc16_ccitt(const uint8_t *data, size_t len);
esp_err_t espnow_link_stop(void);

#ifdef __cplusplus
}
#endif
