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
#include "esp_random.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "espnow_protocol.h"
#include "pairing.h"
#include "ota_node.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

static const char *TAG = "NODE_ULTRASONIC";

// ============================================================================
// 1. CONFIGURACIÓN DE HARDWARE (pines para SuperMini)
// ============================================================================
#define TRIG_PIN                 GPIO_NUM_2
#define ECHO_PIN                 GPIO_NUM_3
#define LED_PIN                  GPIO_NUM_8
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
/* Reintento del PAIR con espera creciente (HUB-NF-005).
 *
 * Antes era un intervalo fijo de 30 s. Con un hub averiado eso son 2880
 * intentos al dia, y si varios nodos pierden su hub a la vez —un corte de
 * luz— todos transmiten en el mismo instante. La espera se dobla hasta un
 * techo de 10 min y lleva jitter para repartir los intentos. */
#define PAIR_RETRY_BASE_SEC 30      // primer reintento tras perder el hub
#define PAIR_RETRY_MAX_SEC 600      // techo: 10 min entre intentos
#define PAIR_RETRY_JITTER_SEC 10    // dispersion para no sincronizar nodos
/* Si el hub vinculado deja de responder, el nodo se desvincula solo y
 * vuelve a buscar. Sin esto, un hub sustituido dejaba al nodo enviando
 * eventos a una MAC que ya no existe, sin forma de recuperarse. */
#define ACK_LOST_MAX        6       // ACKs perdidos seguidos antes de desvincular
#define LINK_CHECK_SEC      10      // cada cuanto se comprueba el enlace
#define UNLINKED_BLINK_MS   2000    // parpadeo del LED sin vincular

/* MAC SEMILLA. No es la MAC definitiva del hub: es solo el destino del
 * primer PAIR cuando el nodo sale de fabrica sin NVS.
 *
 * ESP-NOW no admite broadcast, asi que el nodo necesita saber a quien
 * preguntar. Una vez que el hub contesta, su MAC real se guarda en NVS y
 * esta semilla deja de usarse: si el hub cambia, el nodo lo aprende solo.
 *
 * Para sembrar otra MAC sin recompilar, ver scripts/sembrar-mac-hub.sh. */
/* Semilla del primer PAIR: la MAC STA del hub.
 *
 * Apuntaba a la MAC del SoftAP (...c2:b1), que solo existe cuando el hub
 * levanta su AP de provisioning. Un hub ya configurado va en modo STA y no
 * tiene esa interfaz, asi que el envio fallaba con ESP_ERR_ESPNOW_NOT_FOUND y
 * el nodo no podia emparejarse. La STA existe siempre.
 *
 * Es solo el destino de la primera pregunta: en cuanto el hub contesta, el
 * nodo guarda la MAC real de la respuesta y deja de usar la semilla. */
#define HUB_MAC_SEED_0  0x3c
#define HUB_MAC_SEED_1  0xdc
#define HUB_MAC_SEED_2  0x75
#define HUB_MAC_SEED_3  0x62
#define HUB_MAC_SEED_4  0xc2
#define HUB_MAC_SEED_5  0xb0

// ============================================================================
// 2. VARIABLES GLOBALES
// ============================================================================
static uint8_t  seq_counter = 0;
static uint8_t  own_mac[6] = {0};
static uint8_t  hub_mac[6] = {0};   // destino actual; se carga de NVS
static esp_now_peer_info_t peerInfo;
static uint32_t last_measurement = 0;
static uint32_t last_heartbeat = 0;
static float    last_distance = 0;
static uint32_t last_pair_attempt = 0;
static uint8_t  ack_lost = 0;   // ACKs consecutivos sin recibir
static uint32_t last_link_check = 0;
/* Intentos de PAIR fallidos seguidos. Se pone a cero en cuanto llega un ACK. */
static uint32_t pair_retry_count = 0;
static bool     peer_registered = false;
/* Se define mas abajo; la respuesta a la prueba la necesita desde on_data_recv. */
static esp_err_t ensure_hub_peer(void);

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

    /* Frame de configuracion (FW-012): el hub manda las credenciales WiFi y
     * la URL del servidor OTA. Es la unica via por la que el nodo puede
     * enterarse de la red de la casa, y sin red no puede descargar nada. */
    if (frame.frame_type == ESPNOW_FT_CMD) {
        /* Prueba de enlace: el hub quiere saber si seguimos ahi. Se contesta
         * antes de la comprobacion de la configuracion porque el payload de
         * la prueba es mucho mas corto. */
        if (frame.payload_len >= 1 && frame.payload[0] == ESPNOW_PING_MAGIC) {
            if (!pairing_is_linked()) {
                ESP_LOGW(TAG, "PING del hub sin vinculo: no se responde.");
                return;
            }
            espnow_payload_v1_t pong = {0};
            memcpy(pong.event_code, "PONG", 4);
            pong.seq           = seq_counter++;
            pong.battery_level = 95;
            pong.distance_cm   = 0;

            espnow_frame_t respuesta;
            if (espnow_build_frame(&respuesta, ESPNOW_FT_DATA_EVENT, pong.seq,
                                   own_mac, hub_mac,
                                   (const uint8_t *)&pong, sizeof(pong)) != ESP_OK) {
                ESP_LOGE(TAG, "No se pudo construir el PONG.");
                return;
            }
            if (ensure_hub_peer() != ESP_OK) {
                return;
            }
            ESP_LOGI(TAG, "PING recibido: respondiendo PONG (seq=%d).", pong.seq);
            esp_now_send(hub_mac, (const uint8_t *)&respuesta,
                         espnow_frame_size(sizeof(pong)));
            return;
        }

        if (frame.payload_len != sizeof(espnow_payload_config_t)) {
            ESP_LOGW(TAG, "Config de %u bytes, se esperaban %u",
                     frame.payload_len, (unsigned)sizeof(espnow_payload_config_t));
            return;
        }
        const espnow_payload_config_t *cfg =
            (const espnow_payload_config_t *)frame.payload;
        if (cfg->magic != ESPNOW_CONFIG_MAGIC) {
            ESP_LOGW(TAG, "Config con magic inesperado (0x%02X)", cfg->magic);
            return;
        }
        ESP_LOGI(TAG, "Config recibida del hub: SSID '%s', URL '%s'",
                 cfg->ssid, cfg->ota_url);
        esp_err_t oerr = ota_node_start_from_config(cfg->ssid, cfg->password,
                                                    cfg->ota_url);
        if (oerr != ESP_OK) {
            ESP_LOGE(TAG, "No se pudo iniciar la actualizacion: %s",
                     esp_err_to_name(oerr));
        }
        return;
    }

    if (frame.frame_type != ESPNOW_FT_ACK) {
        return;
    }

    ESP_LOGI(TAG, "ACK del hub recibido (seq=%u)", frame.seq);
    ack_lost = 0;
    /* El enlace funciona: se olvida el backoff acumulado. */
    pair_retry_count = 0;

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
/* Intervalo hasta el siguiente PAIR, segun los fallos acumulados.
 *
 * Se calcula desde el contador en vez de ir doblando una variable: asi el
 * jitter no se acumula y la espera no deriva por encima del techo. */
static uint32_t pair_retry_interval_sec(void)
{
    uint32_t base = PAIR_RETRY_BASE_SEC;
    for (uint32_t i = 0; i < pair_retry_count && base < PAIR_RETRY_MAX_SEC; i++) {
        base *= 2;
    }
    if (base > PAIR_RETRY_MAX_SEC) {
        base = PAIR_RETRY_MAX_SEC;
    }

    /* Jitter hacia abajo: reparte los intentos de varios nodos sin alargar
     * la espera por encima del techo. */
    uint32_t jitter = 0;
    if (base > PAIR_RETRY_JITTER_SEC) {
        jitter = (uint32_t)(esp_random() % PAIR_RETRY_JITTER_SEC);
    }
    return base - jitter;
}

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

    /* Interfaz de red por defecto de la STA.
     *
     * Faltaba: se inicializaba esp_netif pero no se creaba ninguna interfaz,
     * asi que esp_wifi_connect() no tenia donde asociarse y el OTA fallaba
     * siempre con ESP_ERR_TIMEOUT. Hasta ahora no se notaba porque el nodo
     * solo usaba el radio para ESP-NOW, que no necesita netif. */
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la interfaz STA: el OTA no funcionara.");
    }

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

    /* Confirmacion del rollback (FW-012).
     *
     * Una imagen instalada por OTA arranca en PENDING_VERIFY y el bootloader
     * la revierte si nadie la confirma. Se confirma aqui, cuando el nodo ya
     * ha inicializado sensor, LED y NVS: si algo de eso hubiera fallado, el
     * nodo se habria reiniciado y el bootloader habria vuelto a la version
     * anterior. */
    const esp_partition_t *part = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (part != NULL && esp_ota_get_state_partition(part, &st) == ESP_OK) {
        if (st == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                const esp_app_desc_t *d = esp_app_get_description();
                ESP_LOGW(TAG, "Imagen %s confirmada: rollback cancelado.",
                         d ? d->version : "?");
            } else {
                ESP_LOGE(TAG, "No se pudo confirmar la imagen.");
            }
        }
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
    last_link_check   = last_pair_attempt;
    last_heartbeat    = last_pair_attempt;
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000000;

        /* Modo sin vincular: solo se insiste en el PAIR. Los eventos no se
         * envian porque no hay destino conocido. El LED parpadea para que
         * se vea a simple vista que el nodo esta buscando hub. */
        if (!pairing_is_linked()) {
            uint32_t intervalo = pair_retry_interval_sec();
            if ((now - last_pair_attempt) >= intervalo) {
                send_pair_request();
                last_pair_attempt = now;
                if (pair_retry_count < 32) {
                    pair_retry_count++;
                }
                ESP_LOGI(TAG, "Sin hub: proximo intento en %lu s (fallo %lu).",
                         (unsigned long)pair_retry_interval_sec(),
                         (unsigned long)pair_retry_count);
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

        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_SEC) {
            send_event("HEAR", (uint8_t)last_distance);
            last_heartbeat = now;
        }

        /* Vigilancia del enlace. Va con temporizador propio y no con el del
         * heartbeat, que dura 10 min: de otro modo la desvinculacion
         * tardaria horas. Se envia un PAIR para forzar respuesta del hub;
         * si el que responde es otro, el ACK reensena la MAC. */
        /* La comprobacion se espacia cuando el hub lleva rato sin responder:
         * insistir cada 10 s con un hub averiado son 8640 intentos al dia.
         * Los primeros siguen siendo rapidos, para no tardar en detectar una
         * caida breve. */
        if (now - last_link_check >= (uint32_t)LINK_CHECK_SEC + pair_retry_count * 10) {
            last_link_check = now;
            if (ack_lost > 0 && pair_retry_count < 32) {
                pair_retry_count++;
            }
            if (ack_lost > 0) {
                ESP_LOGI(TAG, "Hub sin responder: proxima comprobacion en %lu s.",
                         (unsigned long)(LINK_CHECK_SEC + pair_retry_count * 10));
            }
            if (++ack_lost >= ACK_LOST_MAX) {
                ESP_LOGW(TAG, "El hub no responde (%u comprobaciones sin ACK). Desvinculando para volver a buscar.", ack_lost);
                pairing_forget_hub_mac();
                ack_lost = 0;
                peer_registered = false;
                /* Se empieza a contar desde el primer fallo real. */
                pair_retry_count = 0;
            } else {
                send_pair_request();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
