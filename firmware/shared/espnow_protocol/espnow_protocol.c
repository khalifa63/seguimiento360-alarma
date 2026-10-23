/*
 * espnow_protocol.c — Implementación del frame ESP-NOW v1.
 *
 * Solo lo que ambos lados necesitan: CRC, construcción y validación.
 * El envío, el ACK y el anti-replay viven en el componente del hub
 * (hub/components/espnow_link), porque el nodo no los usa.
 */
#include "espnow_protocol.h"

#include <string.h>
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * CRC16-CCITT
 *
 * Se calcula sobre la cabecera y el payload, sin incluir el propio campo crc16.
 * ------------------------------------------------------------------------ */
uint16_t espnow_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------------------------------------------------------------------------
 * Tamaño del frame
 * ------------------------------------------------------------------------ */
size_t espnow_frame_size(uint16_t payload_len)
{
    return (size_t)ESPNOW_FRAME_OVERHEAD + payload_len;
}

/* ---------------------------------------------------------------------------
 * Construcción
 * ------------------------------------------------------------------------ */
esp_err_t espnow_build_frame(espnow_frame_t *out,
                             espnow_frame_type_t type,
                             uint16_t seq,
                             const uint8_t *src_mac,
                             const uint8_t *dst_mac,
                             const uint8_t *payload,
                             uint16_t payload_len)
{
    if (out == NULL || payload_len > ESPNOW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len > 0 && payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->magic       = ESPNOW_FRAME_MAGIC;
    out->version     = ESPNOW_FRAME_VERSION;
    out->frame_type  = (uint8_t)type;
    out->seq         = seq;
    out->payload_len = payload_len;

    if (src_mac != NULL) {
        memcpy(out->src_mac, src_mac, sizeof(out->src_mac));
    }
    if (dst_mac != NULL) {
        memcpy(out->dst_mac, dst_mac, sizeof(out->dst_mac));
    }
    if (payload_len > 0) {
        memcpy(out->payload, payload, payload_len);
    }

    /* El CRC cubre cabecera + payload, no el propio campo crc16.
     *
     * IMPORTANTE: el CRC va inmediatamente despues del payload real, no en el
     * offset de 'out->crc16'. El struct reserva payload[215], asi que ese
     * campo vive en el byte 234 y solo se transmiten los primeros 28: el CRC
     * se quedaria fuera del frame.
     *
     * Se escribe sobre el buffer y se refleja en el struct para que quien
     * inspeccione 'out' lo vea. */
    const size_t crc_len = (size_t)ESPNOW_HEADER_SIZE + payload_len;
    const uint16_t crc = espnow_crc16_ccitt((const uint8_t *)out, crc_len);
    memcpy((uint8_t *)out + crc_len, &crc, sizeof(crc));

    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Validación
 *
 * Comprueba, en este orden: longitud mínima, magic, versión, longitud
 * declarada coherente y CRC. Un frame que falle cualquier paso se descarta.
 * ------------------------------------------------------------------------ */
esp_err_t espnow_validate_frame(const uint8_t *data,
                                size_t len,
                                espnow_frame_t *out)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len < ESPNOW_FRAME_OVERHEAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* La cabecera es fija; leerla antes de copiar el frame entero permite
     * comprobar payload_len sin asumir que el buffer tiene tamaño completo. */
    const uint8_t *hdr = data;
    if (hdr[0] != ESPNOW_FRAME_MAGIC) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (hdr[1] != ESPNOW_FRAME_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    /* El offset se calcula con offsetof en vez de escribirlo a mano: los
     * literales son fragiles y un desajuste hace que se lea la MAC de destino
     * como si fuera la longitud. */
    uint16_t payload_len;
    memcpy(&payload_len,
           data + offsetof(espnow_frame_t, payload_len),
           sizeof(payload_len));
    if (payload_len > ESPNOW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t esperado = espnow_frame_size(payload_len);
    if (len < esperado) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* CRC sobre cabecera + payload */
    const size_t crc_len = offsetof(espnow_frame_t, payload) + payload_len;
    uint16_t crc_calc = espnow_crc16_ccitt(data, crc_len);
    uint16_t crc_rx;
    memcpy(&crc_rx, data + crc_len, sizeof(crc_rx));
    if (crc_calc != crc_rx) {
        return ESP_ERR_INVALID_CRC;
    }

    if (out != NULL) {
        memcpy(out, data, esperado);
    }
    return ESP_OK;
}
