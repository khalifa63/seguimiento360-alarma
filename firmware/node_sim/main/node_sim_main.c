/*
 * node_sim_main.c — Nodo simulador ESP-NOW para validar AT-02 del HUB-S3
 * Target: ESP32-C3 Super Mini (RISC-V, 4MB flash)
 *
 * v4: Modo AP puro (SoftAP sin clientes) + WIFI_PS_NONE.
 * Espressif recomienda modo AP para ESP-NOW confiable sin conexion a AP.
 * El radio queda fijo en canal 5 y power-save desactivado.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "node_sim";

/* ==================== Configuracion ==================== */
static const uint8_t HUB_MAC[6] = {0x3c, 0xdc, 0x75, 0x62, 0xc2, 0xb0};
#define WIFI_CHANNEL       5
#define NUM_TEST_FRAMES    100
#define FRAME_INTERVAL_MS  100

/* ==================== Protocolo ESP-NOW v1 ==================== */
#define ESPNOW_FRAME_MAGIC    0xA5
#define ESPNOW_FRAME_VERSION  0x01
#define ESPNOW_MAX_PAYLOAD    215
#define ESPNOW_HEADER_SIZE    19
#define ESPNOW_CRC_SIZE       2
#define ESPNOW_FRAME_OVERHEAD (ESPNOW_HEADER_SIZE + ESPNOW_CRC_SIZE)

enum {
    FT_HELLO      = 0x00,
    FT_DATA_EVENT = 0x01,
    FT_ACK        = 0x02,
    FT_CMD        = 0x03,
    FT_BEACON     = 0x04,
};

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

/* ==================== Estado ==================== */
static struct {
    uint8_t  node_mac[6];
    uint16_t tx_seq;
    int64_t  t_send_us;
    uint16_t last_sent_seq;
    bool     waiting_ack;
    int      frames_sent;
    int      acks_received;
    int64_t  rtt_us[NUM_TEST_FRAMES];
    int      rtt_count;
} s_state;

/* ==================== CRC16-CCITT ==================== */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
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

/* ==================== Construir y enviar frame ==================== */
static esp_err_t build_and_send(uint8_t frame_type, uint16_t seq,
                                 const uint8_t *payload, uint16_t payload_len)
{
    espnow_frame_t f;
    memset(&f, 0, sizeof(f));
    f.magic       = ESPNOW_FRAME_MAGIC;
    f.version     = ESPNOW_FRAME_VERSION;
    f.frame_type  = frame_type;
    f.seq         = seq;
    memcpy(f.src_mac, s_state.node_mac, 6);
    memcpy(f.dst_mac, HUB_MAC, 6);
    f.payload_len = payload_len;
    if (payload_len > 0 && payload != NULL) {
        memcpy(f.payload, payload, payload_len);
    }
    size_t crc_len = ESPNOW_HEADER_SIZE + payload_len;
    f.crc16 = crc16_ccitt((const uint8_t *)&f, crc_len);
    size_t tx_len = ESPNOW_HEADER_SIZE + payload_len + ESPNOW_CRC_SIZE;
    return esp_now_send(HUB_MAC, (const uint8_t *)&f, tx_len);
}

/* ==================== Callback RX (recibe ACKs del HUB) ==================== */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    (void)info;
    if (len < (int)ESPNOW_FRAME_OVERHEAD) return;
    const espnow_frame_t *f = (const espnow_frame_t *)data;
    if (f->magic != ESPNOW_FRAME_MAGIC || f->version != ESPNOW_FRAME_VERSION) return;
    if (f->frame_type != FT_ACK) return;
    if (!s_state.waiting_ack || f->seq != s_state.last_sent_seq) return;

    int64_t rtt = esp_timer_get_time() - s_state.t_send_us;
    s_state.waiting_ack = false;
    s_state.acks_received++;
    if (s_state.rtt_count < NUM_TEST_FRAMES) {
        s_state.rtt_us[s_state.rtt_count] = rtt;
        s_state.rtt_count++;
    }
    ESP_LOGI(TAG, "ACK seq=%u  RTT=%lld us (%.2f ms)", f->seq, rtt, rtt / 1000.0);
}

/* ==================== Callback TX ==================== */
static void on_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "TX OK to " MACSTR, MAC2STR(mac_addr));
    } else {
        ESP_LOGW(TAG, "TX fail to " MACSTR " status=%d", MAC2STR(mac_addr), status);
        s_state.waiting_ack = false;
    }
}

/* ==================== Comparacion para qsort ==================== */
static int cmp_int64(const void *a, const void *b)
{
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* ==================== Reporte de estadisticas ==================== */
static void report_stats(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " RESULTADOS AT-02 — ACK ESP-NOW < 50 ms");
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " Frames enviados:   %d", s_state.frames_sent);
    ESP_LOGI(TAG, " ACKs recibidos:    %d", s_state.acks_received);
    ESP_LOGI(TAG, " ACKs perdidos:     %d",
             s_state.frames_sent - s_state.acks_received);
    ESP_LOGI(TAG, "----------------------------------------------");

    if (s_state.rtt_count == 0) {
        ESP_LOGE(TAG, " NO SE RECIBIERON ACKs — AT-02 FALLO");
        ESP_LOGI(TAG, "==============================================");
        return;
    }

    int64_t sorted[NUM_TEST_FRAMES];
    memcpy(sorted, s_state.rtt_us, s_state.rtt_count * sizeof(int64_t));
    qsort(sorted, s_state.rtt_count, sizeof(int64_t), cmp_int64);

    int64_t min_rtt = sorted[0];
    int64_t max_rtt = sorted[s_state.rtt_count - 1];
    int64_t sum = 0;
    for (int i = 0; i < s_state.rtt_count; i++) sum += sorted[i];
    int64_t avg_rtt = sum / s_state.rtt_count;

    int p95_idx = (int)(s_state.rtt_count * 0.95);
    if (p95_idx >= s_state.rtt_count) p95_idx = s_state.rtt_count - 1;
    int64_t p95_rtt = sorted[p95_idx];

    ESP_LOGI(TAG, " RTT min:  %7lld us  (%6.2f ms)", min_rtt, min_rtt / 1000.0);
    ESP_LOGI(TAG, " RTT avg:  %7lld us  (%6.2f ms)", avg_rtt, avg_rtt / 1000.0);
    ESP_LOGI(TAG, " RTT max:  %7lld us  (%6.2f ms)", max_rtt, max_rtt / 1000.0);
    ESP_LOGI(TAG, " RTT p95:  %7lld us  (%6.2f ms)", p95_rtt, p95_rtt / 1000.0);
    ESP_LOGI(TAG, "----------------------------------------------");

    if (p95_rtt < 50000) {
        ESP_LOGI(TAG, " AT-02: PASS  (p95 = %.2f ms < 50 ms)", p95_rtt / 1000.0);
    } else {
        ESP_LOGE(TAG, " AT-02: FALLO  (p95 = %.2f ms >= 50 ms)", p95_rtt / 1000.0);
    }
    ESP_LOGI(TAG, "==============================================");
}

/* ==================== app_main ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Node Simulator ESP-NOW — AT-02 (v4 AP-mode) ===");

    /* NVS (requerido por WiFi) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_read_mac(s_state.node_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Node MAC (STA): " MACSTR, MAC2STR(s_state.node_mac));

    uint8_t ap_mac[6];
    esp_read_mac(ap_mac, ESP_MAC_WIFI_SOFTAP);
    ESP_LOGI(TAG, "Node MAC (AP):  " MACSTR, MAC2STR(ap_mac));
    ESP_LOGI(TAG, "Hub  MAC:       " MACSTR, MAC2STR(HUB_MAC));
    ESP_LOGI(TAG, "Canal fijo:     %d", WIFI_CHANNEL);

    /* WiFi init */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    /* CRITICO: desactivar power-save para ESP-NOW confiable */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* Configurar SoftAP minimalista (sin clientes, solo para fijar canal) */
    char ap_ssid[20];
    snprintf(ap_ssid, sizeof(ap_ssid), "NODE-%02X%02X%02X",
             s_state.node_mac[3], s_state.node_mac[4], s_state.node_mac[5]);

    wifi_config_t ap_cfg = {0};
    ap_cfg.ap.channel = WIFI_CHANNEL;
    ap_cfg.ap.max_connection = 0;            /* no aceptar clientes */
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    ap_cfg.ap.ssid_len = strlen(ap_ssid);
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Confirmar canal activo */
    uint8_t primary_ch;
    wifi_second_chan_t second_ch;
    esp_wifi_get_channel(&primary_ch, &second_ch);
    ESP_LOGI(TAG, "SoftAP '%s' activo en canal %u (PS=NONE)", ap_ssid, primary_ch);

    /* ESP-NOW */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    /* Registrar peer: el HUB */
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, HUB_MAC, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_AP;          /* enviar por la interfaz AP */
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_LOGI(TAG, "Peer HUB registrado. Esperando 2 segundos...");

    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Bucle de envio */
    ESP_LOGI(TAG, "Enviando %d frames DATA_EVENT (intervalo %d ms)...",
             NUM_TEST_FRAMES, FRAME_INTERVAL_MS);

    for (int i = 0; i < NUM_TEST_FRAMES; i++) {
        s_state.tx_seq++;
        s_state.last_sent_seq = s_state.tx_seq;

        uint8_t payload[8];
        int64_t ts = esp_timer_get_time();
        memcpy(payload, &ts, sizeof(ts));

        s_state.t_send_us = esp_timer_get_time();
        s_state.waiting_ack = true;

        esp_err_t send_err = build_and_send(FT_DATA_EVENT, s_state.tx_seq,
                                             payload, sizeof(payload));
        if (send_err == ESP_OK) {
            s_state.frames_sent++;
        } else {
            ESP_LOGW(TAG, "Frame seq=%u send fail: %s",
                     s_state.tx_seq, esp_err_to_name(send_err));
            s_state.waiting_ack = false;
        }

        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    report_stats();

    ESP_LOGI(TAG, "Test completado. Reiniciando en 10 segundos...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}
