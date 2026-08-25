/*
 * node_sim_main.c — NODO M2bis: asociado al AP del HUB + protocolo estructurado
 * Mide entrega y RTT de ACKs ESP-NOW en modo producción APSTA
 *
 * Hardware: ESP32 DevKit V1 (D0WD-V3) en /dev/ttyUSB0
 * Target:   esp32 (NO esp32c3)
 * 
 * FIX M2bis: CRC se coloca inmediatamente después del payload útil,
 *            no al final del struct de 215 bytes.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static const char *TAG = "NODE_M2BIS";

/* --- Configuración del AP del HUB (debe coincidir con main.c del HUB) --- */
#define HUB_AP_SSID   "HUB_360_PROD"
#define HUB_AP_PASS   "hub2026secure"
#define NUM_FRAMES    50

/* --- Protocolo ESP-NOW (debe coincidir con espnow_link.h del HUB) --- */
#define ESPNOW_MAGIC         0xA5
#define ESPNOW_VERSION       0x01
#define ESPNOW_MAX_PAYLOAD   215
#define ESPNOW_HEADER_SIZE   19
#define ESPNOW_CRC_SIZE      2
#define ESPNOW_FRAME_OVERHEAD (ESPNOW_HEADER_SIZE + ESPNOW_CRC_SIZE)

typedef enum {
    FT_HELLO = 0, FT_DATA_EVENT = 1, FT_ACK = 2, FT_CMD = 3, FT_BEACON = 4
} frame_type_t;

/* Header del frame (sin payload ni CRC) */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  frame_type;
    uint16_t seq;
    uint8_t  src_mac[6];
    uint8_t  dst_mac[6];
    uint16_t payload_len;
} espnow_header_t;

/* --- Estado global --- */
static uint8_t s_hub_mac[6];
static volatile bool s_connected = false;
static uint16_t s_seq = 0;
static int s_acks = 0;
static int64_t s_last_send_us = 0;
static int64_t s_rtt_sum = 0, s_rtt_min = INT64_MAX, s_rtt_max = 0;

/* --- CRC16-CCITT (debe coincidir con el HUB) --- */
static uint16_t calc_crc16_ccitt(const uint8_t *data, size_t len)
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

/* --- Callback RX: recibe ACK del HUB --- */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)ESPNOW_FRAME_OVERHEAD) return;

    /* Parsear header */
    const espnow_header_t *hdr = (const espnow_header_t *)data;
    if (hdr->magic != ESPNOW_MAGIC || hdr->version != ESPNOW_VERSION) return;
    if (hdr->frame_type != FT_ACK) return;

    /* Validar CRC (está inmediatamente después del payload) */
    size_t crc_offset = ESPNOW_HEADER_SIZE + hdr->payload_len;
    if ((size_t)len < crc_offset + ESPNOW_CRC_SIZE) return;
    
    uint16_t crc_calc = calc_crc16_ccitt(data, crc_offset);
    uint16_t crc_frame = (uint16_t)(data[crc_offset] | (data[crc_offset + 1] << 8));
    if (crc_calc != crc_frame) {
        ESP_LOGW(TAG, "ACK CRC mismatch: calc=0x%04X frame=0x%04X", crc_calc, crc_frame);
        return;
    }

    /* Validar que el ACK corresponda al último frame enviado */
    uint16_t expected_seq = (s_seq == 0) ? 0xFFFF : s_seq - 1;
    if (hdr->seq == expected_seq) {
        int64_t rtt = esp_timer_get_time() - s_last_send_us;
        s_acks++;
        s_rtt_sum += rtt;
        if (rtt < s_rtt_min) s_rtt_min = rtt;
        if (rtt > s_rtt_max) s_rtt_max = rtt;
        ESP_LOGI(TAG, "✓ ACK seq=%u | RTT=%"PRId64" us (%.2f ms) | total=%d/%d",
                 hdr->seq, rtt, rtt / 1000.0, s_acks, (int)(expected_seq + 1));
    } else {
        ESP_LOGW(TAG, "ACK seq=%u no esperado (esperaba %u)", hdr->seq, expected_seq);
    }
    (void)info;
}

/* --- Callback TX: confirmación MAC layer --- */
static void on_sent(const uint8_t *mac, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "TX MAC FAIL to " MACSTR, MAC2STR(mac));
    }
}

/* --- Handler eventos WiFi --- */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *e = (wifi_event_sta_connected_t *)data;
            ESP_LOGI(TAG, ">>> Conectado al AP del HUB: '%s' canal=%d",
                     e->ssid, e->channel);
            s_connected = true;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "Desconectado del AP, reason=%d, reintentando...",
                     d ? d->reason : -1);
            s_connected = false;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== NODO M2bis: Conectando al AP del HUB ===");
    ESP_LOGI(TAG, "Target SSID: '%s'", HUB_AP_SSID);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, HUB_AP_SSID, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, HUB_AP_PASS, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(78);
    esp_wifi_connect();

    ESP_LOGI(TAG, "Esperando conexión al AP del HUB...");
    int t = 0;
    while (!s_connected && t < 300) {
        vTaskDelay(pdMS_TO_TICKS(100));
        t++;
    }
    if (!s_connected) {
        ESP_LOGE(TAG, "❌ No se conectó al AP del HUB en 30s. Abortando.");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ESP_LOGE(TAG, "❌ Fallo al obtener info del AP");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    memcpy(s_hub_mac, ap_info.bssid, 6);
    ESP_LOGI(TAG, "📡 AP del HUB:");
    ESP_LOGI(TAG, "   BSSID:  " MACSTR, MAC2STR(s_hub_mac));
    ESP_LOGI(TAG, "   Canal:  %d", ap_info.primary);
    ESP_LOGI(TAG, "   RSSI:   %d dBm", ap_info.rssi);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_hub_mac, 6);
    peer.channel = ap_info.primary;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ esp_now_add_peer: %s", esp_err_to_name(err));
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "✓ Peer HUB registrado en canal %d", ap_info.primary);

    uint8_t my_mac[6];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "📟 MAC propia: " MACSTR, MAC2STR(my_mac));

    ESP_LOGI(TAG, ">>> Iniciando envío de %d frames (1 por segundo) <<<", NUM_FRAMES);

    /* Buffer de transmisión: header + payload + CRC */
    uint8_t tx_buf[ESPNOW_HEADER_SIZE + ESPNOW_MAX_PAYLOAD + ESPNOW_CRC_SIZE];

    for (int i = 0; i < NUM_FRAMES; i++) {
        if (!s_connected) {
            ESP_LOGW(TAG, "Conexión perdida, reintentando...");
            esp_wifi_connect();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* Construir header */
        espnow_header_t hdr = {0};
        hdr.magic = ESPNOW_MAGIC;
        hdr.version = ESPNOW_VERSION;
        hdr.frame_type = FT_DATA_EVENT;
        hdr.seq = s_seq++;
        memcpy(hdr.src_mac, my_mac, 6);
        memcpy(hdr.dst_mac, s_hub_mac, 6);
        hdr.payload_len = 8;  /* "ALRM" + 3 dígitos + null */

        /* Copiar header al buffer */
        memcpy(tx_buf, &hdr, ESPNOW_HEADER_SIZE);

        /* Copiar payload inmediatamente después del header */
        int payload_written = snprintf((char *)&tx_buf[ESPNOW_HEADER_SIZE], 
                                       hdr.payload_len, "ALRM%03d", i);
        if (payload_written < 0) payload_written = 0;

        /* Calcular CRC sobre header + payload */
        size_t crc_offset = ESPNOW_HEADER_SIZE + hdr.payload_len;
        uint16_t crc = calc_crc16_ccitt(tx_buf, crc_offset);

        /* Colocar CRC inmediatamente después del payload */
        tx_buf[crc_offset] = (uint8_t)(crc & 0xFF);
        tx_buf[crc_offset + 1] = (uint8_t)((crc >> 8) & 0xFF);

        /* Enviar solo los bytes necesarios: header + payload + CRC */
        size_t tx_len = crc_offset + ESPNOW_CRC_SIZE;
        s_last_send_us = esp_timer_get_time();
        err = esp_now_send(s_hub_mac, tx_buf, tx_len);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "❌ esp_now_send: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "→ Frame %d enviado (seq=%u, len=%zu, crc=0x%04X)",
                     i, hdr.seq, tx_len, crc);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  RESULTADOS M2bis (NODO → HUB APSTA)  ");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Frames enviados:    %d", NUM_FRAMES);
    ESP_LOGI(TAG, " ACKs recibidos:     %d (%.1f%%)",
             s_acks, NUM_FRAMES ? (s_acks * 100.0) / NUM_FRAMES : 0.0);
    if (s_acks > 0) {
        ESP_LOGI(TAG, " RTT min:            %.2f ms", s_rtt_min / 1000.0);
        ESP_LOGI(TAG, " RTT avg:            %.2f ms", (s_rtt_sum / s_acks) / 1000.0);
        ESP_LOGI(TAG, " RTT max:            %.2f ms", s_rtt_max / 1000.0);
        ESP_LOGI(TAG, " Criterio AT-02:     %s (<50ms)",
                 ((s_rtt_sum / s_acks) / 1000.0 < 50.0) ? "✅ PASS" : "❌ FAIL");
    }
    ESP_LOGI(TAG, "========================================");

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
