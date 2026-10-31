// ============================================================================
// Seguimiento 360 — NODO BOTÓN DE PÁNICO (pulsador NA a GND, GPIO4 pull-up)
//
// El boton de panico es un peer ESP-NOW del hub, mismo modelo que el REED y
// el buzzer (firmware/reed-sensor es su base). Cada OPRESION envia un evento
// PANIC (DATA_EVENT, value=1) por ESP-NOW al hub, que lo publica por MQTT y
// Node-RED alterna el estado armado/desarmado POR CONTEXTO (el nodo NO lleva
// toggle: cada oprimida es exactamente un PANIC value=1, SPEC-02 v0.6):
//
//   1a oprimida -> Node-RED despacha buzzer seconds=0xFFFF (bucle continuo)
//   2a oprimida -> Node-RED despacha buzzer seconds=0 (silencio)
//
// Ruta propia (HUB-IF-015 v0.6): NO pasa por el cooldown anti-retrigger de
// 60 s de los DOOR. Protecciones propias del nodo:
//   - anti-rebote por software de 50 ms (nivel estable, patron del prototipo
//     pruebas/pulzador: GPIO4, pull-up interna, flanco 1->0 = oprimida).
//   - rate-limit propio: maximo un PANIC cada 3 s.
//   - reenvio SOLO si el ACK no llega (hasta 3 intentos, 300 ms entre
//     intentos, misma seq): un PANIC duplicado alternaria el toggle dos
//     veces; Node-RED ademas descarta seq repetidas.
//
// Reporta ademas:
//   - HEAR cada 600 s (coincide con NODE_HEARTBEAT_SEC del hub; value=0: el
//     boton no tiene estado de sensor que reportar).
//   - PONG ante el ping del hub (prueba de enlace del portal, HUB-IF-006).
//
// Vinculacion: identica al nodo REED (HUB-IF-011): barrido de canales 1..13,
// PAIR dirigido a la MAC semilla, MAC real del hub aprendida del ACK y
// guardada en NVS (s360node/hub_mac), desvinculacion tras 60 s sin ACK.
//
// Decision documentada: un PANIC oprimido SIN ENLACE no se encola. El panico
// es una orden en tiempo real; encolarlo dispararia la sirena cuando el
// enlace vuelva, minutos despues de la emergencia.
//
// Contrato: docs/firmware/specs/02-interfaces.md (HUB-IF-001/002/005/011/015, v0.6).
// Base: firmware/reed-sensor/ (pairing, PONG, HEAR, vigilancia de enlace) y
// prototipo pruebas/pulzador/ (GPIO4, pull-up, debounce 50 ms, esp32s3).
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

static const char *TAG = "NODE_PANICO";

// ============================ 1. CONFIGURACIÓN ==============================

/* Pulsador NA entre GPIO4 y GND, pull-up interna (prototipo
 * pruebas/pulzador, ESP32-S3): sin oprimir el pin lee 1; oprimirlo lo
 * lleva a GND (0). La oprimida es el flanco 1->0 estable. */
#define BUTTON_GPIO             GPIO_NUM_4
#define LED_PIN                 GPIO_NUM_2   // LED integrado del DevKit (como el reed)
#define WIFI_CHANNEL            5            // Canal del hub (S360_ESPNOW_CHANNEL)
#define SAMPLE_MS               10           // Sondeo del boton
#define DEBOUNCE_MS             50           // 50 ms de nivel estable = oprimida valida
#define HEARTBEAT_INTERVAL_SEC  (10 * 60)   // = NODE_HEARTBEAT_SEC del hub (600 s)
#define PAIR_SWEEP_SEC          8            // barrido de canales cada 8 s
#define PAIR_CHANNELS           13           // 2.4 GHz: canales 1..13
#define LINK_CHECK_SEC          10           // Chequeo de enlace en modo vinculado
#define ACK_LOST_MAX            6            // 6 fallos = 60 s -> desvincular
#define BATTERY_FIXED           100          // Alimentado por cargador de telefono

/* Protecciones propias del PANIC (ruta de toggle, sin cooldown de Node-RED):
 * rate-limit entre oprimidas y reintentos ACK-gateados. */
#define PANIC_MIN_INTERVAL_S    3            // maximo 1 PANIC cada 3 s
#define PANIC_REINTENTOS_MAX    3            // reenvios si el ACK no llega
#define PANIC_REINTENTO_MS      300          // espera entre reintentos

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
static bool     pending_state = false;   // HEAR pendiente al primer ACK
static uint32_t last_pair_sent = 0;      // s desde el arranque
static uint32_t last_heartbeat = 0;
static int      ack_lost = 0;            // chequeos de enlace sin ACK

/* Anti-rebote del pulsador (patron del prototipo pruebas/pulzador): un
 * cambio exige DEBOUNCE_MS de nivel estable distinto del ultimo reportado. */
static int       button_estable = 1;     // 1 = sin oprimir (pull-up)
static int       candidato = 1;          // muestra anterior
static int64_t   desde_cambio_ms = 0;    // instante del ultimo cambio de muestra

/* PANIC en vuelo: reenvio solo si el ACK no llega (misma seq). */
static bool      panic_pendiente = false;
static uint8_t   panic_seq = 0;
static int       panic_intentos = 0;
static int64_t   panic_enviado_ms = 0;
static uint32_t  last_panic_s = 0;       // ultima oprimida enviada (rate-limit)

// ========================== 3. INICIALIZACIÓN DEL BOTON =====================

static void boton_init(void)
{
    /* NA entre GPIO4 y GND: pull-up interna, sin resistencia externa
     * (prototipo pruebas/pulzador). Sin interrupciones: sondeo por tarea,
     * igual que el reed. */
    const gpio_config_t boton_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boton_conf));
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
    if (peer_info.channel == 0) peer_info.channel = WIFI_CHANNEL;
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

/* PANIC: la oprimida del boton. value=1 SIEMPRE (el toggle armado/desarmado
 * lo lleva Node-RED por contexto, no el nodo). Registra el seq para el
 * reenvio ACK-gateado: esp_now_send es fire-and-forget, sin confirmacion de
 * entrega, y un PANIC perdido dejaria el toggle desincronizado. */
static void send_panic(void)
{
    espnow_payload_v1_t payload = {0};
    memcpy(payload.event_code, "PANIC", 4);
    payload.seq           = seq_counter++;
    payload.battery_level = BATTERY_FIXED;
    payload.value         = 1;

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
        ESP_LOGE(TAG, "No se pudo construir el PANIC: %s", esp_err_to_name(err));
        return;
    }

    panic_pendiente = true;
    panic_seq       = payload.seq;
    panic_intentos  = 1;
    panic_enviado_ms = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "PANIC enviado (seq=%d, intento 1/%d)", payload.seq,
             PANIC_REINTENTOS_MAX);
    esp_now_send(hub_mac, (const uint8_t *)&frame, espnow_frame_size(sizeof(payload)));

    set_led(1);
    vTaskDelay(pdMS_TO_TICKS(60));
    set_led(0);
}

/* Reenvio del PANIC con la MISMA seq si el ACK no llega. Con seq nueva, un
 * PANIC duplicado alternaria el toggle dos veces (armar+desarmar); con la
 * misma seq, Node-RED descarta el repetido (HUB-IF-015 v0.6). */
static void panic_reintento(void)
{
    espnow_payload_v1_t payload = {0};
    memcpy(payload.event_code, "PANIC", 4);
    payload.seq           = panic_seq;
    payload.battery_level = BATTERY_FIXED;
    payload.value         = 1;

    espnow_frame_t frame;
    if (espnow_build_frame(&frame, ESPNOW_FT_DATA_EVENT, payload.seq,
                           own_mac, hub_mac,
                           (const uint8_t *)&payload,
                           sizeof(payload)) != ESP_OK) {
        return;
    }

    panic_intentos++;
    panic_enviado_ms = esp_timer_get_time() / 1000;
    ESP_LOGW(TAG, "PANIC sin ACK: reintento %d/%d (seq=%d)",
             panic_intentos, PANIC_REINTENTOS_MAX, panic_seq);
    esp_now_send(hub_mac, (const uint8_t *)&frame, espnow_frame_size(sizeof(payload)));
}

/* Barrido de canales para el PAIR (ampliacion de HUB-IF-011).
 *
 * El hub es estacion de su router: su radio queda en el canal del router,
 * que el usuario no controla y que puede cambiar solo. Fijar el nodo en un
 * canal unico rompe el enlace sin aviso (le paso al prototipo REED el
 * 2026-10-30). El barrido manda el PAIR por los 13 canales con una
 * habitacion corta en cada uno: en cuanto el hub contesta, el canal actual
 * ya es el del hub y ahi se queda. Si el router cambia de canal mas
 * adelante, los chequeos de enlace fallan, el nodo se desvincula y el
 * barrido vuelve a encontrar el canal nuevo. */
static void send_pair_request(void);

static void pair_channel_sweep(void)
{
    for (int ch = 1; ch <= PAIR_CHANNELS; ch++) {
        if (pairing_is_linked()) return;
        if (esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
        vTaskDelay(pdMS_TO_TICKS(20));       /* asentar el canal */
        peer_info.channel = (uint8_t)ch;
        if (peer_registered) esp_now_mod_peer(&peer_info);
        send_pair_request();                 /* registra el peer si faltaba */
        vTaskDelay(pdMS_TO_TICKS(180));      /* habitacion para el ACK */
    }
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
    /* Respuesta al ping del hub (prueba de enlace del portal): el hub manda
     * un frame ESPNOW_FT_CMD con espnow_payload_ping_t (magia 0xE7); el nodo
     * contesta con un PONG (DATA_EVENT) que el hub usa para medir el RTT.
     * Leccion del REED (2026-10-30): sin esta respuesta el portal declara
     * "sin_respuesta" aunque el enlace real funcione. */
    if (frame.frame_type == ESPNOW_FT_CMD) {
        if (frame.payload_len >= sizeof(espnow_payload_ping_t) &&
            frame.payload[0] == ESPNOW_PING_MAGIC) {
            ESP_LOGI(TAG, "PING del hub recibido (seq=%u): respondiendo PONG",
                     frame.seq);
            send_frame("PONG", frame.seq);
        }
        return;
    }

    if (frame.frame_type != ESPNOW_FT_ACK) {
        return;
    }

    ESP_LOGI(TAG, "ACK del hub recibido (seq=%u)", frame.seq);
    ack_lost = 0;   /* enlace vivo: reinicia el contador de vigilancia */

    /* ACK del PANIC en vuelo: entrega confirmada, sin reintentos. */
    if (panic_pendiente && frame.seq == panic_seq) {
        panic_pendiente = false;
        ESP_LOGI(TAG, "PANIC (seq=%d) confirmado por el hub tras %d intento(s).",
                 panic_seq, panic_intentos);
    }

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
    /* El boton no tiene estado de sensor: al primer ACK de cada sesion
     * reporta un HEAR (value=0) para que el hub lo marque visto. */
    if (pending_state) {
        pending_state = false;
        send_frame("HEAR", 0);
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
        pending_state = true;   /* al re-vincular se reporta el estado */
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
     * del RF (phy_init) derruba la alimentacion de un puerto USB debil; el
     * nodo va alimentado por cargador de telefono (misma leccion del REED y
     * del buzzer). Para ESP-NOW a corto alcance 10 dBm sobran. */
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

    boton_init();
    /* El estado se reporta al confirmar el enlace (HEAR value=0: el boton
     * no mide nada). */
    pending_state = true;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  NODO BOTON DE PANICO v0.1 (GPIO %d, pull-up interna)",
             BUTTON_GPIO);
    ESP_LOGI(TAG, "  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4], own_mac[5]);
    ESP_LOGI(TAG, "  Frame: v1 (magic 0x%02X, cabecera %d B, CRC16)",
             ESPNOW_FRAME_MAGIC, ESPNOW_HEADER_SIZE);
    ESP_LOGI(TAG, "  PANIC: value=1 por oprimida, debounce %d ms, rate-limit %d s",
             DEBOUNCE_MS, PANIC_MIN_INTERVAL_S);
    ESP_LOGI(TAG, "========================================");

    /* Primer intento de emparejamiento / chequeo de enlace. */
    send_pair_request();
    last_pair_sent = esp_timer_get_time() / 1000000;

    while (1) {
        int64_t ahora_ms = esp_timer_get_time() / 1000;
        uint32_t now = ahora_ms / 1000;

        /* --- Anti-rebote (prototipo pruebas/pulzador): un cambio de nivel
         * solo se acepta tras DEBOUNCE_MS estable. La oprimida es el flanco
         * 1->0 (NA a GND con pull-up). --- */
        const int lectura = gpio_get_level(BUTTON_GPIO);
        if (lectura != candidato) {
            candidato = lectura;
            desde_cambio_ms = ahora_ms;
        } else if (candidato != button_estable &&
                   (ahora_ms - desde_cambio_ms) >= DEBOUNCE_MS) {
            const int anterior = button_estable;
            button_estable = candidato;

            if (anterior == 1 && button_estable == 0) {
                /* OPRESION (flanco descendente). Un PANIC por oprimida:
                 * el toggle lo lleva Node-RED. */
                if (!pairing_is_linked()) {
                    /* Sin enlace NO se encola (decision documentada): una
                     * oprimida sin hub no debe sonar cuando el enlace
                     * vuelva mas tarde. */
                    ESP_LOGW(TAG, "Oprimida sin enlace: PANIC no enviado "
                             "(no se encola).");
                } else if ((now - last_panic_s) < PANIC_MIN_INTERVAL_S &&
                           last_panic_s != 0) {
                    /* Rate-limit propio de la ruta de toggle (el cooldown
                     * de 60 s de Node-RED es de los DOOR, no de aqui). */
                    ESP_LOGW(TAG, "Oprimida ignorada por rate-limit "
                             "(< %d s desde la anterior).", PANIC_MIN_INTERVAL_S);
                } else {
                    last_panic_s = now;
                    send_panic();
                }
            }
        }

        /* Reenvio del PANIC mientras el ACK no llegue (misma seq). */
        if (panic_pendiente && pairing_is_linked()) {
            if ((esp_timer_get_time() / 1000 - panic_enviado_ms) >= PANIC_REINTENTO_MS) {
                if (panic_intentos < PANIC_REINTENTOS_MAX) {
                    panic_reintento();
                } else {
                    panic_pendiente = false;
                    ESP_LOGE(TAG, "PANIC (seq=%d) sin ACK tras %d intentos. "
                             "Pulsar de nuevo.", panic_seq, PANIC_REINTENTOS_MAX);
                }
            }
        }

        /* Modo SIN VINCULAR: barrido de canales; sin eventos. */
        if (!pairing_is_linked()) {
            if (now - last_pair_sent >= PAIR_SWEEP_SEC) {
                pair_channel_sweep();
                last_pair_sent = now;
            }
            set_led(now % 2);   /* parpadeo: buscando hub */
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
            continue;
        }

        /* Modo vinculado: vigilancia del enlace cada 10 s. */
        link_check(now);

        if (hub_confirmed && (now - last_heartbeat >= HEARTBEAT_INTERVAL_SEC)) {
            ESP_LOGI(TAG, "HEARTBEAT");
            send_frame("HEAR", 0);
            last_heartbeat = now;
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}
