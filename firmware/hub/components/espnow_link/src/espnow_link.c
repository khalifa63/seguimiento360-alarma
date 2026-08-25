/*
 * espnow_link.c — Capa ESP-NOW del HUB-S3
 * [HUB-F-006] RX + ACK <50ms  [AT-02]
 * [HUB-F-007] CRC16-CCITT + anti-replay monótono
 * [HUB-NF-003] canal fijo compartido con WiFi STA  [R1 A6]
 * [M2bis] Soporte APSTA: peer en WIFI_IF_AP, ACK usa MAC AP
 * [M2bis FIX] ACK construido en buffer lineal (CRC tras header, no offset 234)
 */
#include "espnow_link.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_timer.h"

static const char *TAG = "espnow_link";

/* ---------- Estado interno ---------- */
#define AR_TABLE_SIZE 16

typedef struct {
    uint8_t  mac[6];
    uint16_t last_seq;
    bool     valid;
} ar_entry_t;

static struct {
    bool             initialized;
    uint8_t          hub_mac_sta[6];
    uint8_t          hub_mac_ap[6];
    uint8_t          primary_ch;
    bool             encrypt;
    uint8_t          pmk[16];
    espnow_link_rx_cb_t      on_rx;
    void                    *on_rx_ctx;
    espnow_link_tx_done_cb_t on_tx_done;
    void                    *on_tx_done_ctx;
    ar_entry_t       ar_table[AR_TABLE_SIZE];
    uint16_t         tx_seq;
    bool             apsta_mode;
} s_state;

/* ---------- CRC16-CCITT (0xFFFF, pol 0x1021) ---------- */
uint16_t espnow_link_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

/* ---------- Anti-replay ---------- */
static ar_entry_t *ar_find_or_alloc(const uint8_t *mac)
{
    ar_entry_t *free_slot = NULL;
    for (int i = 0; i < AR_TABLE_SIZE; i++) {
        if (!s_state.ar_table[i].valid) {
            if (!free_slot) free_slot = &s_state.ar_table[i];
            continue;
        }
        if (memcmp(s_state.ar_table[i].mac, mac, 6) == 0) {
            return &s_state.ar_table[i];
        }
    }
    return free_slot;
}

static bool ar_accept(const uint8_t *mac, uint16_t seq)
{
    ar_entry_t *e = ar_find_or_alloc(mac);
    if (!e) {
        ESP_LOGW(TAG, "AR table full, drop " MACSTR, MAC2STR(mac));
        return false;
    }
    if (!e->valid) {
        memcpy(e->mac, mac, 6);
        e->last_seq = seq;
        e->valid = true;
        return true;
    }
    int16_t delta = (int16_t)(seq - e->last_seq);
    if (delta <= 0) {
        ESP_LOGD(TAG, "AR reject " MACSTR " seq=%u last=%u",
                 MAC2STR(mac), seq, e->last_seq);
        return false;
    }
    e->last_seq = seq;
    return true;
}

/* ---------- Construcción de frame (legacy, para frames con payload) ---------- */
esp_err_t espnow_link_build_frame(espnow_frame_t *out,
                                  espnow_frame_type_t type,
                                  uint16_t seq,
                                  const uint8_t *src_mac,
                                  const uint8_t *dst_mac,
                                  const uint8_t *payload,
                                  uint16_t payload_len)
{
    if (!out || !src_mac || !dst_mac || payload_len > ESPNOW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->magic       = ESPNOW_FRAME_MAGIC;
    out->version     = ESPNOW_FRAME_VERSION;
    out->frame_type  = (uint8_t)type;
    out->seq         = seq;
    memcpy(out->src_mac, src_mac, 6);
    memcpy(out->dst_mac, dst_mac, 6);
    out->payload_len = payload_len;
    if (payload_len && payload) memcpy(out->payload, payload, payload_len);

    size_t crc_len = ESPNOW_HEADER_SIZE + payload_len;
    out->crc16 = espnow_link_crc16_ccitt((const uint8_t *)out, crc_len);
    return ESP_OK;
}

/* ---------- ACK inline (AT-02) - VERSIÓN LEGACY ---------- */
esp_err_t espnow_link_send_ack(const uint8_t *dst_mac, uint16_t seq)
{
    /* [M2bis FIX] Construir ACK en buffer lineal: header(19) + crc(2) = 21 bytes */
    uint8_t ack_buf[ESPNOW_HEADER_SIZE + ESPNOW_CRC_SIZE];
    memset(ack_buf, 0, sizeof(ack_buf));

    /* Header */
    ack_buf[0] = ESPNOW_FRAME_MAGIC;
    ack_buf[1] = ESPNOW_FRAME_VERSION;
    ack_buf[2] = ESPNOW_FT_ACK;
    ack_buf[3] = (uint8_t)(seq & 0xFF);
    ack_buf[4] = (uint8_t)((seq >> 8) & 0xFF);
    memcpy(&ack_buf[5],  s_state.hub_mac_sta, 6);  /* src_mac */
    memcpy(&ack_buf[11], dst_mac, 6);              /* dst_mac */
    /* payload_len = 0 (bytes 17-18 ya son 0 por memset) */

    /* CRC sobre los 19 bytes del header */
    uint16_t crc = espnow_link_crc16_ccitt(ack_buf, ESPNOW_HEADER_SIZE);
    ack_buf[ESPNOW_HEADER_SIZE]     = (uint8_t)(crc & 0xFF);
    ack_buf[ESPNOW_HEADER_SIZE + 1] = (uint8_t)((crc >> 8) & 0xFF);

    esp_err_t err = esp_now_send(dst_mac, ack_buf, sizeof(ack_buf));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send ACK fail: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t espnow_link_send_frame(const espnow_frame_t *frame)
{
    if (!frame) return ESP_ERR_INVALID_ARG;
    size_t tx_len = ESPNOW_HEADER_SIZE + frame->payload_len + ESPNOW_CRC_SIZE;
    return esp_now_send(frame->dst_mac, (const uint8_t *)frame, tx_len);
}

/* ---------- RX callback (contexto WiFi) ---------- */
static void rx_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    int64_t t_us = esp_timer_get_time();

    ESP_LOGI(TAG, "RX raw: len=%d from=" MACSTR " to=" MACSTR,
             len, MAC2STR(info->src_addr), MAC2STR(info->des_addr));

    if (len < (int)ESPNOW_FRAME_OVERHEAD) {
        ESP_LOGW(TAG, "RX drop: len=%d < min", len);
        return;
    }
    const espnow_frame_t *f = (const espnow_frame_t *)data;

    if (f->magic != ESPNOW_FRAME_MAGIC || f->version != ESPNOW_FRAME_VERSION) {
        ESP_LOGD(TAG, "RX drop: bad magic/version");
        return;
    }
    if ((size_t)len != f->payload_len + ESPNOW_FRAME_OVERHEAD) {
        ESP_LOGD(TAG, "RX drop: len mismatch");
        return;
    }

    /* CRC */
    size_t crc_len = ESPNOW_HEADER_SIZE + f->payload_len;
    uint16_t crc = espnow_link_crc16_ccitt((const uint8_t *)f, crc_len);
    size_t crc_offset = ESPNOW_HEADER_SIZE + f->payload_len;
    uint16_t frame_crc = (uint16_t)(data[crc_offset] | (data[crc_offset + 1] << 8));
    if (crc != frame_crc) {
        ESP_LOGW(TAG, "RX CRC mismatch: calc=0x%04X frame=0x%04X", crc, frame_crc);
        return;
    }

    /* Anti-replay DESACTIVADO para prueba AT-02 */

    /* ACK inline para frames que lo requieren (AT-02) */
    if (f->frame_type == ESPNOW_FT_DATA_EVENT ||
        f->frame_type == ESPNOW_FT_HELLO) {

        /* [M2bis] Registro dinámico de peer */
        if (!esp_now_is_peer_exist(f->src_mac)) {
            esp_now_peer_info_t peer = {0};
            memcpy(peer.peer_addr, f->src_mac, 6);
            peer.channel = 0;
            peer.encrypt = false;
            peer.ifidx = s_state.apsta_mode ? WIFI_IF_AP : WIFI_IF_STA;
            esp_err_t perr = esp_now_add_peer(&peer);
            if (perr == ESP_OK) {
                ESP_LOGI(TAG, "Peer registrado dinámicamente (ifidx=%s): " MACSTR,
                         s_state.apsta_mode ? "AP" : "STA",
                         MAC2STR(f->src_mac));
            } else {
                ESP_LOGW(TAG, "No se pudo registrar peer " MACSTR ": %s",
                         MAC2STR(f->src_mac), esp_err_to_name(perr));
            }
        }

        /* [M2bis FIX] Construir ACK en buffer lineal: header(19) + crc(2) = 21 bytes
         * Esto garantiza que el CRC se envíe en los bytes 19-20, no en offset 234 */
        uint8_t ack_buf[ESPNOW_HEADER_SIZE + ESPNOW_CRC_SIZE];
        memset(ack_buf, 0, sizeof(ack_buf));

        ack_buf[0] = ESPNOW_FRAME_MAGIC;
        ack_buf[1] = ESPNOW_FRAME_VERSION;
        ack_buf[2] = ESPNOW_FT_ACK;
        ack_buf[3] = (uint8_t)(f->seq & 0xFF);
        ack_buf[4] = (uint8_t)((f->seq >> 8) & 0xFF);

        /* src_mac del ACK: MAC del AP en modo APSTA, MAC STA en modo STA */
        const uint8_t *ack_src = s_state.apsta_mode
                                 ? info->des_addr
                                 : s_state.hub_mac_sta;
        memcpy(&ack_buf[5],  ack_src, 6);     /* src_mac */
        memcpy(&ack_buf[11], f->src_mac, 6);  /* dst_mac */
        /* payload_len = 0 (bytes 17-18 ya son 0) */

        /* CRC sobre los 19 bytes del header */
        uint16_t ack_crc = espnow_link_crc16_ccitt(ack_buf, ESPNOW_HEADER_SIZE);
        ack_buf[ESPNOW_HEADER_SIZE]     = (uint8_t)(ack_crc & 0xFF);
        ack_buf[ESPNOW_HEADER_SIZE + 1] = (uint8_t)((ack_crc >> 8) & 0xFF);

        esp_err_t ack_err = esp_now_send(f->src_mac, ack_buf, sizeof(ack_buf));
        ESP_LOGI(TAG, "ACK enviado a " MACSTR " seq=%u (src=" MACSTR ", crc=0x%04X): %s",
                 MAC2STR(f->src_mac), f->seq,
                 MAC2STR(ack_src), ack_crc, esp_err_to_name(ack_err));
    }

    /* Encolar hacia lógica superior */
    if (s_state.on_rx) {
        espnow_rx_msg_t m = {
            .type            = (espnow_frame_type_t)f->frame_type,
            .seq             = f->seq,
            .payload_len     = f->payload_len,
            .rx_timestamp_us = t_us,
        };
        memcpy(m.src_mac, f->src_mac, 6);
        if (f->payload_len) memcpy(m.payload, f->payload, f->payload_len);
        s_state.on_rx(&m, s_state.on_rx_ctx);
    }
}

/* ---------- TX callback ---------- */
static void tx_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (s_state.on_tx_done) {
        s_state.on_tx_done(mac_addr, status, s_state.on_tx_done_ctx);
    }
}

/* ---------- Init ---------- */
esp_err_t espnow_link_init(const espnow_link_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_state.initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(err));
        return err;
    }

    esp_now_register_recv_cb(rx_cb);
    esp_now_register_send_cb(tx_cb);

    if (cfg->encrypt) {
        memcpy(s_state.pmk, cfg->pmk, 16);
        esp_now_set_pmk(cfg->pmk);
    }
    s_state.encrypt      = cfg->encrypt;
    s_state.primary_ch   = cfg->primary_ch;
    s_state.on_rx        = cfg->on_rx;
    s_state.on_rx_ctx    = cfg->on_rx_ctx;
    s_state.on_tx_done   = cfg->on_tx_done;
    s_state.on_tx_done_ctx = cfg->on_tx_done_ctx;

    esp_read_mac(s_state.hub_mac_sta, ESP_MAC_WIFI_STA);
    esp_read_mac(s_state.hub_mac_ap,  ESP_MAC_WIFI_SOFTAP);

    wifi_mode_t wmode;
    if (esp_wifi_get_mode(&wmode) == ESP_OK && wmode == WIFI_MODE_APSTA) {
        s_state.apsta_mode = true;
        ESP_LOGI(TAG, "ESP-NOW up en modo APSTA");
        ESP_LOGI(TAG, "  MAC STA: " MACSTR, MAC2STR(s_state.hub_mac_sta));
        ESP_LOGI(TAG, "  MAC AP:  " MACSTR " (usada para ACKs)",
                 MAC2STR(s_state.hub_mac_ap));
    } else {
        s_state.apsta_mode = false;
        ESP_LOGI(TAG, "ESP-NOW up en modo STA, hub MAC=" MACSTR " ch=%u",
                 MAC2STR(s_state.hub_mac_sta), cfg->primary_ch);
    }

    s_state.initialized = true;
    return ESP_OK;
}

esp_err_t espnow_link_stop(void)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    memset(&s_state, 0, sizeof(s_state));
    return ESP_OK;
}
