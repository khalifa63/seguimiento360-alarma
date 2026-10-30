// ============================================================================
// Seguimiento 360 — NODO BUZZER (sirena de alarma, GPIO3 con PWM LEDC)
//
// Buzzer PASIVO entre GPIO3 y GND (conductor directo soportado por el C3 con
// GPIO_DRIVE_CAP_3; si el volumen es bajo, transistor NPN en base con 1 kΩ y
// buzzer a 5 V). El PWM lo genera LEDC a 2700 Hz con duty 90 % (mas volumen
// que 50 %), tal como quedo validado en el prototipo pruebas/buzzer.
//
// Se activa por comando del hub (HUB-IF-015, SPEC-02 v0.4): llega un frame
// ESPNOW_FT_CMD con espnow_payload_buzzer_t (magia 0xE8) y suena `seconds`
// continuos (1..300 s). Al arrancar responde BUZZ (DATA_EVENT, value=1), que
// el hub publica como evento auditable en la tabla events.
//
// Reporta ademas:
//   - HEAR cada 600 s (coincide con NODE_HEARTBEAT_SEC del hub; value = 1 si
//     esta sonando en ese instante).
//   - PONG ante el ping del hub (prueba de enlace del portal).
//
// Vinculacion: identica al nodo REED (HUB-IF-011): barrido de canales 1..13,
// PAIR dirigido a la MAC semilla, MAC real del hub aprendida del ACK y
// guardada en NVS (s360node/hub_mac), desvinculacion tras 60 s sin ACK.
//
// Contrato: docs/firmware/specs/02-interfaces.md (HUB-IF-001/002/015, v0.4).
// Base: firmware/reed-sensor/ (pairing, PONG, HEAR, vigilancia de enlace) y
// prototipo pruebas/buzzer/ (LEDC 2700 Hz, duty 90 %, CAP_3).
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
#include "driver/ledc.h"
#include "esp_timer.h"

#include "espnow_protocol.h"
#include "pairing.h"

static const char *TAG = "NODE_BUZZER";

// ============================ 1. CONFIGURACIÓN ==============================

/* Buzzer pasivo en GPIO3: el PWM de LEDC lo hace vibrar (2-4 kHz audible). */
#define BUZZER_GPIO             GPIO_NUM_3
#define BUZZER_FREQ_HZ          2700        // tono audible tipico del pasivo
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT
#define LEDC_DUTY               921         // 921/1024 = 90 % -> mas volumen
#define LEDC_TIMER_ID           LEDC_TIMER_0
#define LEDC_CHANNEL            LEDC_CHANNEL_0

/* Duracion de la sirena por comando (HUB-IF-015). Fuera de rango se
 * descarta: un seconds=0 o un valor enorme no deben dejar la sirena pegada
 * ni apagarla de golpe. */
#define BUZZER_SECONDS_MIN      1
#define BUZZER_SECONDS_MAX      300

/* LED de estado: en la ESP32-C3 SuperMini el LED azul integrado esta en
 * GPIO8 y es ACTIVO BAJO (se enciende con nivel 0). Cambio documentado
 * respecto al reed (DevKit V1: GPIO2, activo alto). */
#define LED_PIN                 GPIO_NUM_8
#define LED_ACTIVE_LOW          1

#define WIFI_CHANNEL            5           // canal del hub (S360_ESPNOW_CHANNEL)
#define HEARTBEAT_INTERVAL_SEC  (10 * 60)   // = NODE_HEARTBEAT_SEC del hub (600 s)
#define PAIR_SWEEP_SEC          8           // barrido de canales cada 8 s
#define PAIR_CHANNELS           13          // 2.4 GHz: canales 1..13
#define LINK_CHECK_SEC          10          // Chequeo de enlace en modo vinculado
#define ACK_LOST_MAX            6           // 6 fallos = 60 s -> desvincular
#define BATTERY_FIXED           100         // Alimentado por cargador de telefono

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
static bool     pending_state = false;   // estado por reportar al primer ACK
static uint32_t last_pair_sent = 0;      // s desde el arranque
static uint32_t last_heartbeat = 0;
static int      ack_lost = 0;            // chequeos de enlace sin ACK

/* Sirena: s_sounding marca que el PWM esta activo; s_buzz_deadline es el
 * instante (s) en que debe cortar. Un comando nuevo EXTENSIONA la sirena
 * (deadline = max(deadline, ahora + seconds)) para que un trigger repetido
 * no corte una alarma mas larga que ya estaba sonando. */
static bool     s_sounding = false;
static uint32_t s_buzz_deadline = 0;

// ====================== 3. INICIALIZACIÓN DEL BUZZER ========================

static void buzzer_init(void)
{
    /* Timer PWM: 2700 Hz, resolucion de 10 bits (prototipo validado). */
    const ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER_ID,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    const ledc_channel_config_t ledc_channel = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER_ID,
        .duty       = 0,               /* arranca en silencio */
        .hpoint     = 0,
        .flags.output_invert = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    /* Maxima capacidad de corriente del GPIO: empuja el buzzer con mas
     * fuerza (prototipo pruebas/buzzer). */
    ESP_ERROR_CHECK(gpio_set_drive_capability(BUZZER_GPIO, GPIO_DRIVE_CAP_3));
}

static void buzzer_on(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, LEDC_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);
}

static void buzzer_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);
}

/* Arranca (o extensiona) la sirena. Llamado desde el recv callback de
 * ESP-NOW: solo fija el estado, el corte lo hace el bucle principal. */
static void buzzer_start(uint16_t seconds)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t fin = now + seconds;
    if (s_sounding && s_buzz_deadline > fin) {
        fin = s_buzz_deadline;   /* extensiona, no acorta una alarma en curso */
    }
    if (!s_sounding) {
        buzzer_on();
        s_sounding = true;
    }
    s_buzz_deadline = fin;
}

/* Corte por vencimiento, llamado del bucle principal cada 100 ms. */
static void buzzer_tick(uint32_t now)
{
    if (s_sounding && now >= s_buzz_deadline) {
        buzzer_off();
        s_sounding = false;
        ESP_LOGI(TAG, "Sirena detenida tras %u s", (unsigned)s_buzz_deadline);
    }
}

static void set_led(int on)
{
    /* SuperMini C3: LED azul en GPIO8, activo bajo. */
    gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? !on : on);
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
    if (frame.frame_type == ESPNOW_FT_CMD) {
        /* Respuesta al ping del hub (prueba de enlace del portal): el hub
         * manda un frame ESPNOW_FT_CMD con espnow_payload_ping_t (magia
         * 0xE7); el nodo contesta con un PONG (DATA_EVENT) que el hub usa
         * para medir el RTT. Sin esto el portal declara "sin_respuesta"
         * aunque el enlace real funcione (le paso al prototipo REED el
         * 2026-10-30). */
        if (frame.payload_len >= sizeof(espnow_payload_ping_t) &&
            frame.payload[0] == ESPNOW_PING_MAGIC) {
            ESP_LOGI(TAG, "PING del hub recibido (seq=%u): respondiendo PONG",
                     frame.seq);
            send_frame("PONG", frame.seq);
            return;
        }

        /* Activacion de la sirena (HUB-IF-015, SPEC-02 v0.4): frame
         * ESPNOW_FT_CMD con espnow_payload_buzzer_t (magia 0xE8). Se
         * valida el rango de seconds (1..300); fuera de rango se descarta
         * para no dejar la sirena pegada ni apagarla de golpe. */
        if (frame.payload_len >= sizeof(espnow_payload_buzzer_t) &&
            frame.payload[0] == ESPNOW_BUZZER_MAGIC) {
            espnow_payload_buzzer_t cmd;
            memcpy(&cmd, frame.payload, sizeof(cmd));
            if (cmd.seconds < BUZZER_SECONDS_MIN || cmd.seconds > BUZZER_SECONDS_MAX) {
                ESP_LOGW(TAG, "BUZZER invalido: seconds=%u fuera de rango (1..300). Descartado.",
                         (unsigned)cmd.seconds);
                return;
            }
            ESP_LOGI(TAG, "BUZZER recibido (seq=%u): sirena %u s, origen %.4s",
                     frame.seq, (unsigned)cmd.seconds, (const char *)cmd.src_event);
            /* Primero suena, luego confirma: el BUZZ reporta una sirena que
             * ya esta sonando, no una promesa. */
            buzzer_start(cmd.seconds);
            send_frame("BUZZ", 1);
            return;
        }

        /* CMD con magia desconocida: puede ser de una version futura del
         * contrato. Se ignora sin quebrar. */
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
     * pairing o tras un reinicio), para que el hub conozca el estado de la
     * sirena aunque no haya comando reciente. */
    if (pending_state) {
        pending_state = false;
        send_frame("HEAR", s_sounding ? 1 : 0);
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
     * del RF (phy_init) derruba la alimentacion de un puerto USB debil; el
     * nodo va alimentado por cargador de telefono (misma leccion del REED).
     * Para ESP-NOW a corto alcance 10 dBm sobran. */
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

    buzzer_init();
    /* El estado de arranque se reporta al confirmar el enlace. */
    pending_state = true;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  NODO BUZZER v0.1 (GPIO %d, PWM %d Hz, duty 90%%)",
             BUZZER_GPIO, BUZZER_FREQ_HZ);
    ESP_LOGI(TAG, "  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4], own_mac[5]);
    ESP_LOGI(TAG, "  Frame: v1 (magic 0x%02X, cabecera %d B, CRC16)",
             ESPNOW_FRAME_MAGIC, ESPNOW_HEADER_SIZE);
    ESP_LOGI(TAG, "  Sirena: %u..%u s por comando (HUB-IF-015)",
             (unsigned)BUZZER_SECONDS_MIN, (unsigned)BUZZER_SECONDS_MAX);
    ESP_LOGI(TAG, "========================================");

    /* Primer intento de emparejamiento / chequeo de enlace. */
    send_pair_request();
    last_pair_sent = esp_timer_get_time() / 1000000;

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000000;

        /* Corte de la sirena por vencimiento. */
        buzzer_tick(now);

        /* Modo SIN VINCULAR: barrido de canales; sin eventos. */
        if (!pairing_is_linked()) {
            if (now - last_pair_sent >= PAIR_SWEEP_SEC) {
                pair_channel_sweep();
                last_pair_sent = now;
            }
            set_led(now % 2);   /* parpadeo: buscando hub */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Modo vinculado: vigilancia del enlace cada 10 s. */
        link_check(now);

        if (hub_confirmed && (now - last_heartbeat >= HEARTBEAT_INTERVAL_SEC)) {
            ESP_LOGI(TAG, "HEARTBEAT");
            send_frame("HEAR", s_sounding ? 1 : 0);
            last_heartbeat = now;
        }

        /* LED de estado: encendido fijo mientras la sirena suena. */
        if (s_sounding) {
            set_led(1);
        } else if (now % 2) {
            set_led(0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
