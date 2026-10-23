// ============================================================================
// Seguimiento 360 — NODO ULTRASÓNICO HC-SR04
//
// Envía eventos al hub con el frame ESP-NOW v1 (cabecera de 19 bytes, magic
// 0xA5 y CRC16), el mismo contrato que usa el hub.
//
// Antes enviaba el payload de 7 bytes en crudo, sin cabecera ni CRC: el hub
// esperaba 19+2 bytes de overhead y descartaba todo lo que llegaba del nodo.
//
// Contrato: docs/firmware/specs/02-interfaces.md (HUB-IF-001, HUB-IF-002).
// ============================================================================

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "espnow_protocol.h"
#include "pairing.h"

static const char *TAG = "NODE_ULTRASONIC";

// ============================================================================
// 1. CONFIGURACIÓN DE HARDWARE (pines para SuperMini)
// ============================================================================
#define TRIG_PIN                 GPIO_NUM_5
#define ECHO_PIN                 GPIO_NUM_4
#define LED_PIN                  GPIO_NUM_2
#define WIFI_CHANNEL             5
#define HEARTBEAT_INTERVAL_SEC   (10 * 60)
#define MEASUREMENT_INTERVAL_SEC 5
#define DISTANCE_THRESHOLD_CM    50
#define MAX_DISTANCE_CM          400

// ============================================================================
// VINCULACIÓN CON EL HUB (FW-008)
//
// La MAC del hub vive en NVS, no en el código. Se siembra al flashear y se
// actualiza sola durante el emparejamiento: el hub la envía en el ACK.
//
// Así un nodo no necesita recompilarse si cambia el hub. Si NVS está vacía,
// el nodo entra en modo sin vincular: parpadea el LED y reintenta PAIR.
//
// ESP-NOW no admite broadcast (reinicia el nodo), de ahí que la primera MAC
// tenga que llegar sembrada. Ver docs/firmware/specs/02-interfaces.md.
// ============================================================================
#define PAIR_RETRY_SEC      30      // reintento de PAIR si no hay ACK
#define UNLINKED_BLINK_MS   2000    // parpadeo del LED sin vincular

/* MAC SEMILLA. No es la MAC definitiva del hub: es solo el destino del
 * primer PAIR cuando el nodo sale de fabrica sin NVS.
 *
 * ESP-NOW no admite broadcast, asi que el nodo necesita saber a quien
 * preguntar. Una vez que el hub contesta, su MAC real se guarda en NVS y
 * esta semilla deja de usarse: si el hub cambia, el nodo lo aprende solo.
 *
 * Para sembrar otra MAC sin recompilar, ver scripts/sembrar-mac-hub.sh. */
#define HUB_MAC_SEED_0  0x3c
#define HUB_MAC_SEED_1  0xdc
#define HUB_MAC_SEED_2  0x75
#define HUB_MAC_SEED_3  0x62
#define HUB_MAC_SEED_4  0xc2
#define HUB_MAC_SEED_5  0xb1

// ============================================================================
// 2. VARIABLES GLOBALES
// ============================================================================
static uint8_t  seq_counter = 0;
static uint8_t  own_mac[6] = {0};
static uint8_t  hub_mac[6] = {0};   // destino actual; se carga de NVS
static esp_now_peer_info_t peerInfo;
static uint32_t last_measurement = 0;
static float    last_distance = 0;
static uint32_t last_pair_attempt = 0;
static bool     peer_registered = false;

// ============================================================================
// 3. SENSOR HC-SR04
// ============================================================================
static float measure_distance(void)
{
    uint64_t start_us, end_us;

    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    uint32_t timeout = 10000;
    while (gpio_get_level(ECHO_PIN) == 0 && timeout > 0) {
        esp_rom_delay_us(1);
        timeout--;
    }
    if (timeout == 0) {
        return 0;
    }

    start_us = esp_timer_get_time();
    timeout = 100000;
    while (gpio_get_level(ECHO_PIN) == 1 && timeout > 0) {
        esp_rom_delay_us(1);
        timeout--;
    }
    end_us = esp_timer_get_time();

    float distance_cm = ((end_us - start_us) * 0.0343f) / 2.0f;
    if (distance_cm > MAX_DISTANCE_CM || distance_cm < 0) {
        return 0;
    }
    return distance_cm;
}

static void init_ultrasonic_sensor(void)
{
    gpio_reset_pin(TRIG_PIN);
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIG_PIN, 0);

    gpio_reset_pin(ECHO_PIN);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
    gpio_pullup_dis(ECHO_PIN);
    gpio_pulldown_en(ECHO_PIN);

    ESP_LOGI(TAG, "Sensor HC-SR04 inicializado (TRIG=%d, ECHO=%d)", TRIG_PIN, ECHO_PIN);
}

// ============================================================================
// 4. CALLBACKS ESP-NOW
// ============================================================================
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Mensaje enviado");
        gpio_set_level(LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, "Error al enviar: %d", (int)status);
    }
}

/* El ACK del hub llega como frame v1 de tipo ESPNOW_FT_ACK.
 *
 * Su campo src_mac contiene la MAC del hub: ese es el dato que el nodo
 * aprende durante el emparejamiento (FW-008). */
static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    espnow_frame_t frame;
    if (espnow_validate_frame(data, (size_t)len, &frame) != ESP_OK) {
        ESP_LOGW(TAG, "Frame descartado (CRC o formato invalido), %d bytes", len);
        return;
    }

    if (frame.frame_type != ESPNOW_FT_ACK) {
        return;
    }

    ESP_LOGI(TAG, "ACK del hub recibido (seq=%u)", frame.seq);

    /* La MAC de destino que usa el nodo es la del SoftAP del hub, pero el
     * ACK viaja desde la interfaz por la que el hub responde. Si el hub
     * cambio, se aprende la nueva aqui. */
    if (pairing_mac_changed(frame.src_mac)) {
        uint8_t vieja[6];
        bool tenia = (pairing_load_hub_mac(vieja) == ESP_OK);
        if (tenia) {
            ESP_LOGW(TAG, "El hub ha cambiado. Antes %02x:%02x:%02x:%02x:%02x:%02x",
                     vieja[0], vieja[1], vieja[2], vieja[3], vieja[4], vieja[5]);
            /* Se reemplaza el peer: la MAC vieja ya no sirve. */
            esp_now_del_peer(vieja);
            peer_registered = false;
        }
        if (pairing_save_hub_mac(frame.src_mac) == ESP_OK) {
            ESP_LOGI(TAG, "Nodo VINCULADO con el hub %02x:%02x:%02x:%02x:%02x:%02x",
                     frame.src_mac[0], frame.src_mac[1], frame.src_mac[2],
                     frame.src_mac[3], frame.src_mac[4], frame.src_mac[5]);
        }
    }
}

// ============================================================================
// 5. ENVÍO DE EVENTOS CON FRAME v1
// ============================================================================
/* Registra (o re-registra) el peer del hub si aún no lo está. */
static esp_err_t ensure_hub_peer(void)
{
    if (peer_registered) {
        return ESP_OK;
    }

    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, hub_mac, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.ifidx   = ESP_IF_WIFI_STA;
    peerInfo.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peerInfo);
    if (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST) {
        peer_registered = true;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "No se pudo registrar el peer: %s", esp_err_to_name(err));
    return err;
}

static void send_event(const char *event_code, uint8_t distance_cm)
{
    espnow_payload_v1_t payload = {0};
    memcpy(payload.event_code, event_code, sizeof(payload.event_code));
    payload.seq           = seq_counter++;
    payload.battery_level = 95;
    payload.distance_cm   = distance_cm;

    /* Sin hub vinculado no se envian eventos: solo el PAIR, que es lo que
     * permite descubrirlo. Ver el bucle de app_main. */
    if (!pairing_is_linked()) {
        ESP_LOGW(TAG, "Sin vincular: evento %s descartado", event_code);
        return;
    }

    espnow_frame_t frame;
    esp_err_t err = espnow_build_frame(&frame,
                                       ESPNOW_FT_DATA_EVENT,
                                       payload.seq,
                                       own_mac,
                                       hub_mac,
                                       (const uint8_t *)&payload,
                                       sizeof(payload));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo construir el frame: %s", esp_err_to_name(err));
        return;
    }

    if (ensure_hub_peer() != ESP_OK) {
        return;
    }

    ESP_LOGI(TAG, "Enviando: %s | Dist: %dcm | Seq: %d", event_code, distance_cm, payload.seq);
    gpio_set_level(LED_PIN, 1);

    /* Se envía el tamaño real: cabecera + payload + CRC, no sizeof(frame). */
    esp_now_send(hub_mac, (const uint8_t *)&frame,
                 espnow_frame_size(sizeof(payload)));

    vTaskDelay(pdMS_TO_TICKS(200));
}

/* Envia una solicitud de emparejamiento. Se usa al arrancar sin vincular y
 * como reintento periodico. */
static void send_pair_request(void)
{
    espnow_payload_v1_t payload = {0};
    memcpy(payload.event_code, "PAIR", 4);
    payload.seq           = seq_counter++;
    payload.battery_level = 95;
    payload.distance_cm   = 0;

    if (ensure_hub_peer() != ESP_OK) {
        return;
    }

    espnow_frame_t frame;
    if (espnow_build_frame(&frame, ESPNOW_FT_DATA_EVENT, payload.seq,
                           own_mac, hub_mac,
                           (const uint8_t *)&payload,
                           sizeof(payload)) != ESP_OK) {
        return;
    }

    ESP_LOGI(TAG, "Solicitando emparejamiento (seq=%d)", payload.seq);
    esp_now_send(hub_mac, (const uint8_t *)&frame,
                 espnow_frame_size(sizeof(payload)));
}

// ============================================================================
// 6. APP MAIN
// ============================================================================
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t err_channel = esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err_channel != ESP_OK) {
        ESP_LOGW(TAG, "Aviso canal: %s", esp_err_to_name(err_channel));
    }

    /* La MAC propia va dentro del frame: el hub la usa para identificar el nodo. */
    ESP_ERROR_CHECK(esp_read_mac(own_mac, ESP_MAC_WIFI_STA));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    /* La MAC del hub se lee de NVS. Si no hay, el nodo arranca sin vincular
     * y solo envia solicitudes de PAIR hasta que el hub conteste. */
    ret = pairing_load_hub_mac(hub_mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Hub vinculado en NVS: %02x:%02x:%02x:%02x:%02x:%02x",
                 hub_mac[0], hub_mac[1], hub_mac[2],
                 hub_mac[3], hub_mac[4], hub_mac[5]);
        peer_registered = false;   /* ensure_hub_peer lo registra al enviar */
    } else {
        /* Sin NVS: se usa la semilla solo para preguntar. El nodo sigue
         * "sin vincular" hasta que el hub conteste con su MAC real. */
        const uint8_t semilla[6] = {
            HUB_MAC_SEED_0, HUB_MAC_SEED_1, HUB_MAC_SEED_2,
            HUB_MAC_SEED_3, HUB_MAC_SEED_4, HUB_MAC_SEED_5
        };
        memcpy(hub_mac, semilla, sizeof(hub_mac));
        peer_registered = false;
        ESP_LOGW(TAG, "Sin hub en NVS (%s). Se usara la semilla "
                      "%02x:%02x:%02x:%02x:%02x:%02x para pedir el pairing.",
                 esp_err_to_name(ret),
                 hub_mac[0], hub_mac[1], hub_mac[2],
                 hub_mac[3], hub_mac[4], hub_mac[5]);
    }

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    init_ultrasonic_sensor();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  SENSOR ULTRASONICO HC-SR04 v2.1");
    ESP_LOGI(TAG, "  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4], own_mac[5]);
    ESP_LOGI(TAG, "  Frame: v1 (magic 0x%02X, cabecera %d B, CRC16)",
             ESPNOW_FRAME_MAGIC, ESPNOW_HEADER_SIZE);
    ESP_LOGI(TAG, "  Umbral: %d cm | Medicion: %d seg",
             DISTANCE_THRESHOLD_CM, MEASUREMENT_INTERVAL_SEC);
    ESP_LOGI(TAG, "========================================");

    /* Primer intento de emparejamiento. Si ya habia hub en NVS se confirma;
     * si no, es lo que arranca el descubrimiento. */
    send_pair_request();
    last_pair_attempt = esp_timer_get_time() / 1000000;
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000000;

        /* Modo sin vincular: solo se insiste en el PAIR. Los eventos no se
         * envian porque no hay destino conocido. El LED parpadea para que
         * se vea a simple vista que el nodo esta buscando hub. */
        if (!pairing_is_linked()) {
            if ((now - last_pair_attempt) >= PAIR_RETRY_SEC) {
                send_pair_request();
                last_pair_attempt = now;
            }
            gpio_set_level(LED_PIN, (now % (UNLINKED_BLINK_MS / 1000)) < 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (now - last_measurement >= MEASUREMENT_INTERVAL_SEC) {
            float distance = measure_distance();
            last_distance = distance;
            last_measurement = now;

            ESP_LOGI(TAG, "Distancia: %.1f cm", distance);

            if (distance > 0 && distance < DISTANCE_THRESHOLD_CM) {
                ESP_LOGW(TAG, "OBJETO DETECTADO: %.1f cm", distance);
                send_event("ALRM", (uint8_t)distance);
                for (int i = 0; i < 5; i++) {
                    gpio_set_level(LED_PIN, i % 2);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                gpio_set_level(LED_PIN, 0);
            }
        }

        if (now - last_measurement >= HEARTBEAT_INTERVAL_SEC) {
            send_event("HEAR", (uint8_t)last_distance);
            last_measurement = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
