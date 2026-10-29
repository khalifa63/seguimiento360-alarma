// ============================================================================
// Seguimiento 360 — NODO REED (sensor de apertura, GPIO4 + pull-up interna)
//
// Reed Switch Normalmente Abierto entre GPIO4 y GND. Pull-up interna del
// ESP32 (sin resistencia externa). Imán cerca = contacto cerrado = LOW.
//
// Reporta POR TRANSICIÓN (no por sondeo continuo):
//   - DOOR  value=1 -> Cerrado (imán cerca, pin LOW)
//   - DOOR  value=0 -> Abierto  (imán lejos, pin HIGH por pull-up)
//   - HEAR  value=estado actual, cada 10 min (coincide con
//           NODE_HEARTBEAT_SEC del hub para que node_lost no se adelante).
//
// Vinculación (HUB-IF-011, SPEC-02 v0.3):
//   - Nodo virgen (NVS vacia): modo SIN VINCULAR. PAIR cada 30 s dirigido a
//     la MAC semilla compilada (ESP-NOW no admite broadcast). LED parpadea y
//     no se envian eventos; los cambios quedan pendientes.
//   - El hub solo ADOPTA nodos nuevos con su ventana de emparejamiento
//     abierta (boton BOOT / ventana del portal, 120 s).
//   - El ACK (frame ESPNOW_FT_ACK) lleva la MAC real del hub en src_mac: se
//     guarda en NVS y desde ahi el nodo envia eventos con normalidad.
//   - Vigilancia del enlace: PAIR cada LINK_CHECK_SEC; ACK_LOST_MAX fallos
//     seguidos (60 s) -> pairing_forget_hub_mac() y de nuevo sin vincular.
//
// Contrato: docs/firmware/specs/02-interfaces.md (HUB-IF-001/002/011, v0.3).
// Basado en el nodo semilla firmware/sensor-ultrasonic (frame v1 + pairing).
// ============================================================================

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "espnow_protocol.h"
#include "pairing.h"

static const char *TAG = "NODE_REED";

// ============================ 1. CONFIGURACIÓN ==============================

#define REED_GPIO               GPIO_NUM_4   // Reed entre GPIO4 y GND
#define LED_PIN                 GPIO_NUM_2   // LED integrado del DevKit V1
#define WIFI_CHANNEL            5            // Canal del hub (S360_ESPNOW_CHANNEL)
#define SAMPLE_MS               100          // Sondeo del reed (filtra rebote mecánico)
#define HEARTBEAT_INTERVAL_SEC  (10 * 60)   // = NODE_HEARTBEAT_SEC del hub (600 s)
#define PAIR_RETRY_SEC          30           // PAIR en modo SIN VINCULAR
#define LINK_CHECK_SEC          10           // Chequeo de enlace en modo vinculado
#define ACK_LOST_MAX            6            // 6 fallos = 60 s -> desvincular
#define BATTERY_FIXED           100          // Prototipo alimentado por USB

/*
 * MAC semilla (HUB-IF-011): destino del primer PAIR de un nodo de fabrica.
 * NO es la MAC definitiva: solo a quien se le pregunta primero. La MAC real
 * llega en el src_mac del ACK del hub y se guarda en NVS en ese momento.
 */
static const uint8_t HUB_MAC_SEED[6] = { 0x3c, 0xdc, 0x75, 0x62, 0xc2, 0xb0 };

// ============================ 2. ESTADO GLOBAL ==============================

static uint8_t  seq_counter = 0;
static uint8_t  own_mac[6] = {0};
static uint8_t  hub_mac[6] = {0};        // destino actual: NVS o semilla
static esp_now_peer_info_t peer_info;
static bool     peer_registered = false;

static bool     hub_confirmed = false;   // true = enlazado (NVS de una sesion
                                         // previa o ACK recibido en esta)
static bool     pending_state = false;   // cambio de puerta sin poder enviar
static int      last_reported_level = -1;// ultimo nivel estable (-1 = desconocido)
static int      prev_sample = -1;        // muestra anterior (anti-rebote)
static uint32_t last_pair_sent = 0;      // s desde el arranque
static uint32_t last_heartbeat = 0;
static int      ack_lost = 0;            // chequeos de enlace sin ACK

// ==================== 3. INICIALIZACIÓN DEL REED ============================

static void reed_init(void)
{
    /* Con inicializadores designados, todo campo no listado queda en 0. */
    const gpio_config_t reed_conf = {
        .pin_bit_mask = (1ULL << REED_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* evita el estado flotante */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,    /* sondeo por tarea */
    };
    ESP_ERROR_CHECK(gpio_config(&reed_conf));
}

/* Nivel actual del reed: 0 = cerrado (imán cerca), 1 = abierto. */
static inline int reed_level(void)
{
    return gpio_get_level(REED_GPIO);
}

/* value del payload: 1 = Cerrado, 0 = Abierto (SPEC-02 v0.3, event DOOR). */
static inline int reed_value(void)
{
    return (reed_level() == 0) ? 1 : 0;
}

static void set_led(int on)
{
    gpio_set_level(LED_PIN, on);
}

// ========================== 4. ENVÍO ESP-NOW ================================

/* Registra (o re-registra) el peer del hub si aún no lo está. */
static esp_err_t ensure_hub_peer(void)
{
    if (peer_registered) {
        return ESP_OK;
    }
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, hub_mac, 6);
    peer_info.channel = WIFI_CHANNEL;
    peer_info.ifidx   = ESP_IF_WIFI_STA;
    peer_info.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST) {
        peer_registered = true;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "No se pudo registrar el peer: %s", esp_err_to_name(err));
    return err;
}

static void send_frame(const char *event_code, uint8_t value)
{
    espnow_payload_v1_t payload = {0};
    memcpy(payload.event_code, event_code, sizeof(payload.event_code));
    payload.seq           = seq_counter++;
    payload.battery_level = BATTERY_FIXED;
    payload.value         = value;

    if (ensure_hub_peer() != ESP_OK) {
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

    ESP_LOGI(TAG, "Enviando: %s | value=%d | Seq=%d", event_code, value, payload.seq);
    esp_now_send(hub_mac, (const uint8_t *)&frame, espnow_frame_size(sizeof(payload)));

    set_led(1);
    vTaskDelay(pdMS_TO_TICKS(60));
    set_led(0);
}

/* PAIR: solicitud de emparejamiento y chequeo de enlace.
 *  - Hub con el nodo ya registrado: responde ACK (ventana cerrada o abierta).
 *  - Hub con nodo nuevo: lo registra SOLO con la ventana abierta y responde ACK. */
static void send_pair_request(void)
{
    espnow_payload_v1_t payload = {0};
    memcpy(payload.event_code, "PAIR", 4);
    payload.seq           = seq_counter++;
    payload.battery_level = BATTERY_FIXED;
    payload.value         = 0;

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
    esp_now_send(hub_mac, (const uint8_t *)&frame, espnow_frame_size(sizeof(payload)));
}

// ============================ 5. CALLBACKS ==================================

static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGE(TAG, "Error al enviar: %d", (int)status);
    }
}

/* El ACK del hub llega como frame v1 de tipo ESPNOW_FT_ACK; su src_mac es la
 * MAC por la que el hub responde (SoftAP) y es lo que se aprende/persiste. */
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
    ack_lost = 0;   /* enlace vivo: reinicia el contador de vigilancia */

    if (pairing_mac_changed(frame.src_mac)) {
        uint8_t vieja[6];
        if (pairing_load_hub_mac(vieja) == ESP_OK) {
            ESP_LOGW(TAG, "El hub ha cambiado. Antes %02x:%02x:%02x:%02x:%02x:%02x",
                     vieja[0], vieja[1], vieja[2], vieja[3], vieja[4], vieja[5]);
            esp_now_del_peer(vieja);
            peer_registered = false;
        }
        if (pairing_save_hub_mac(frame.src_mac) == ESP_OK) {
            memcpy(hub_mac, frame.src_mac, 6);
            ESP_LOGI(TAG, "Nodo VINCULADO con el hub %02x:%02x:%02x:%02x:%02x:%02x",
                     frame.src_mac[0], frame.src_mac[1], frame.src_mac[2],
                     frame.src_mac[3], frame.src_mac[4], frame.src_mac[5]);
        }
    }

    if (!hub_confirmed) {
        hub_confirmed = true;
        ESP_LOGI(TAG, "Enlace confirmado: se inician los eventos de estado.");
    }
    /* El estado actual se reporta al primer ACK de cada sesion (tras el
     * pairing o tras un reinicio), para que el hub conozca la posicion de
     * la puerta aunque no haya habido transicion. */
    if (pending_state) {
        pending_state = false;
        send_frame("DOOR", (uint8_t)reed_value());
    }
}

/* Vigilancia del enlace (HUB-IF-011): LINK_CHECK_SEC sin ACK, ACK_LOST_MAX
 * veces -> olvida el hub y vuelve al modo sin vincular. */
static void link_check(uint32_t now)
{
    if (now - last_pair_sent < LINK_CHECK_SEC) {
        return;
    }
    send_pair_request();
    last_pair_sent = now;

    if (++ack_lost >= ACK_LOST_MAX) {
        ESP_LOGW(TAG, "El hub no responde (%d chequeos sin ACK). Desvinculando...",
                 ack_lost);
        pairing_forget_hub_mac();
        peer_registered = false;
        hub_confirmed = false;
        ack_lost = 0;
        pending_state = true;   /* al re-vincular se reporta el estado actual */
    }
}

// ============================== 6. APP MAIN =================================

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

    /* Mitigacion de brownout: potencia maxima del radio reducida. El arranque
     * del RF (phy_init) derruba la alimentacion de este puerto USB; para
     * ESP-NOW a corto alcance 10 dBm sobran. Revisar al corregir la
     * alimentacion (ver README del nodo). */
    esp_err_t err_txp = esp_wifi_set_max_tx_power(40);   /* 40 = 10 dBm */
    if (err_txp != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo limitar la potencia TX: %s", esp_err_to_name(err_txp));
    }

    /* La MAC propia va dentro del frame: el hub identifica al nodo por ella. */
    ESP_ERROR_CHECK(esp_read_mac(own_mac, ESP_MAC_WIFI_STA));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    /* Modo inicial segun HUB-IF-011: 2a con MAC en NVS (eventos normales),
     * 2b sin MAC -> SIN VINCULAR, PAIR dirigido a la MAC semilla. */
    ret = pairing_load_hub_mac(hub_mac);
    if (ret == ESP_OK) {
        hub_confirmed = true;   /* sesion previa: el registro del hub persiste */
        ESP_LOGI(TAG, "Hub vinculado en NVS: %02x:%02x:%02x:%02x:%02x:%02x",
                 hub_mac[0], hub_mac[1], hub_mac[2], hub_mac[3], hub_mac[4], hub_mac[5]);
    } else {
        memcpy(hub_mac, HUB_MAC_SEED, 6);
        ESP_LOGW(TAG, "Sin hub en NVS (%s). Modo SIN VINCULAR: PAIR a la semilla.",
                 esp_err_to_name(ret));
    }

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    set_led(0);

    reed_init();
    last_reported_level = reed_level();
    prev_sample = last_reported_level;
    /* El estado de arranque se reporta al confirmar el enlace: el hub debe
     * conocer la posicion de la puerta aunque no haya habido transicion. */
    pending_state = true;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  NODO REED v1.0 (GPIO %d, pull-up interna)", REED_GPIO);
    ESP_LOGI(TAG, "  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4], own_mac[5]);
    ESP_LOGI(TAG, "  Frame: v1 (magic 0x%02X, cabecera %d B, CRC16)",
             ESPNOW_FRAME_MAGIC, ESPNOW_HEADER_SIZE);
    ESP_LOGI(TAG, "  Estado inicial: %s", (last_reported_level == 0) ? "Cerrado" : "Abierto");
    ESP_LOGI(TAG, "========================================");

    /* Primer intento de emparejamiento / chequeo de enlace. */
    send_pair_request();
    last_pair_sent = esp_timer_get_time() / 1000000;

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000000;
        int level = reed_level();

        /* Anti-rebote por muestreo: un cambio exige 2 muestras iguales
         * seguidas (200 ms) distintas del último estado reportado. */
        if (level != prev_sample) {
            prev_sample = level;
        } else if (level != last_reported_level) {
            last_reported_level = level;
            if (hub_confirmed) {
                send_frame("DOOR", (uint8_t)reed_value());
            } else {
                pending_state = true;
                ESP_LOGI(TAG, "Cambio de estado sin enlace: %s (se enviara al vincular)",
                         (level == 0) ? "Cerrado" : "Abierto");
            }
        }

        /* Modo SIN VINCULAR: PAIR cada 30 s a la semilla; sin eventos. */
        if (!pairing_is_linked()) {
            if (now - last_pair_sent >= PAIR_RETRY_SEC) {
                send_pair_request();
                last_pair_sent = now;
            }
            set_led(now % 2);   /* parpadeo: buscando hub */
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
            continue;
        }

        /* Modo vinculado: eventos + vigilancia del enlace cada 10 s. */
        link_check(now);

        if (hub_confirmed && (now - last_heartbeat >= HEARTBEAT_INTERVAL_SEC)) {
            ESP_LOGI(TAG, "HEARTBEAT");
            send_frame("HEAR", (uint8_t)reed_value());
            last_heartbeat = now;
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}
