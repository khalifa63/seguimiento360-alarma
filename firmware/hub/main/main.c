// ============================================================================
// Seguimiento 360 - HUB ESP32-S3
// Fase 4: Multi-tenant + Portal Cautivo v2 + MQTT + API REST
// Versión: 6.0 (Producción Final - Compatible con node_sim_main.c)
// ============================================================================

#include <stdio.h>

// Archivos del portal cautivo incrustados (COMPONENT_EMBED_TXTFILES)

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "espnow_protocol.h"
#include "sm_alarm.h"
#include "nvs_manager.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include <time.h>
#include <ctype.h>

/* Credenciales locales. secrets.h no se versiona (ver secrets.h.example). */
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "secrets.h no encontrado: usando valores de ejemplo. Ver secrets.h.example"
#define S360_MQTT_USERNAME   "s360_hub"
#define S360_MQTT_PASSWORD   "CAMBIAR_EN_PRODUCCION"
#endif

// ============================================================================
// ARCHIVOS DEL PORTAL CAUTIVO INCRUSTADOS (COMPONENT_EMBED_TXTFILES)
// ============================================================================


static const char *TAG = "HUB_S3_MAIN";

// ============================================================================
// CONFIGURACIÓN Y DEFINICIONES
// ============================================================================
#define MAX_NODES 20
#define PORTAL_INACTIVITY_TIMEOUT_MS 600000  // 10 minutos
#define INDICATOR_LED_PIN 48                  // GPIO 48 del ESP32-S3

/* Vigilancia de nodos (HUB-F-008, AT-004).
 * Un nodo se considera perdido tras NODE_LOST_MISSED heartbeats sin dar
 * señales. Cualquier evento del nodo (ALRM o HEAR) cuenta como actividad. */
#define NODE_HEARTBEAT_SEC   (10 * 60)        // debe coincidir con el nodo
#define NODE_LOST_MISSED     3                // heartbeats perdidos
#define NODE_LOST_TIMEOUT_S  (NODE_HEARTBEAT_SEC * NODE_LOST_MISSED)
#define NODE_WATCHDOG_PERIOD_S 30             // cada cuanto se revisa

// URI del Broker MQTT (Cloudflare Tunnel)
#define MQTT_BROKER_URI "wss://mqtt.seguimiento360.com:443"

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

// Estructura antigua (para migración transparente desde versiones previas)
typedef struct {
    char ssid[33];
    char password[65];
} wifi_credentials_t;

// Estructura de configuración del HUB (Multi-tenant)
typedef struct {
    uint8_t version;
    char ssid[33];
    char password[65];
    char tenant_id[16];
    char site_id[32];
    char hub_name[32];        // "Hub Entrada Principal"
    /* Claim code con el que se vinculo el hub (HUB-F-016). Va al final a
     * proposito: al anadirlo al final, una configuracion ya guardada en NVS
     * se sigue leyendo bien y este campo queda vacio, que significa "sin
     * codigo". Los campos anteriores no se desplazan. */
    char claim_code[16];
} hub_config_t;

// Estructura de información de cada nodo (versión 3 - con jerarquía)
typedef struct {
    uint8_t  mac[6];
    char     type[16];             // "ULTRASONIC", "PIR", etc.
    char     name[24];             // "Sensor Puerta Garaje"
    char     location[24];         // "Casa Principal"
    char     room[24];             // "Garaje"
    uint8_t  distance_threshold;   // Umbral configurable (cm)
    uint32_t link_date;            // Fecha de vinculación (UNIX timestamp)
    uint32_t last_ping;            // Último evento recibido (UNIX timestamp)
} node_info_t;

// Registro unificado de nodos
typedef struct {
    uint8_t count;
    node_info_t nodes[MAX_NODES];
} node_registry_t;

// ⚠️ CRÍTICO: Estructura EXACTA del Nodo (7 bytes - debe coincidir con node_sim_main.c)
typedef struct {
    uint8_t event_code[4];    // "PAIR", "ALRM", "HEAR"
    uint8_t seq;              // Contador de secuencia (0-255)
    uint8_t battery_level;    // Nivel de batería (0-100%)
    uint8_t value;            // Dato del evento; semántica según event_code (SPEC-02 v0.3)
} __attribute__((packed)) security_payload_t;

// Verificación en tiempo de compilación
_Static_assert(sizeof(security_payload_t) == 7, 
               "¡ERROR: El tamaño del payload no coincide con el nodo (debe ser 7 bytes)!");

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
static nvs_handle_t nvs_handle_pairing;
static bool wifi_configured = false;
/* true solo si el SoftAP de provisioning esta levantado. Lo usa el arranque
 * del portal para no abrirlo en un hub ya configurado. */
static bool s_portal_habilitado = false;
/* Momento en que se abrio la ventana de vinculacion, en segundos. 0 = cerrada.
 * Se guarda como instante y no como booleano: asi la ventana caduca sola sin
 * necesidad de una tarea que la cierre. */
static uint32_t s_pair_window_started = 0;
static httpd_handle_t server = NULL;
/* Canal compartido con los nodos. Debe coincidir con WIFI_CHANNEL del
 * firmware del nodo y con el canal en que queda el AP del hub.
 * Estaba a 0 y nunca se asignaba: los peers se registraban en el canal 0
 * mientras los nodos transmiten en el 5, asi que no se escuchaban. */
/* Unico origen desde el que se acepta firmware (HUB-F-012).
 * Debe coincidir con el host del broker: si el certificado y el dominio ya
 * estan validados ahi, no tiene sentido admitir otro origen para OTA. */
#define S360_OTA_HOST   "seguimiento360.com"

/* Reintento de conexion con espera creciente (HUB-NF-005).
 *
 * Sin esto, una caida de WiFi o de MQTT dejaba el hub desconectado hasta el
 * siguiente reinicio: el handler de desconexion solo ponia un bit que nadie
 * leia, y esp_wifi no reintenta por su cuenta.
 *
 * Se dobla la espera en cada fallo, de 1 s a 60 s, y se anade jitter: si
 * varios hubs pierden el enlace a la vez (corte de luz, router reiniciado),
 * sin jitter todos reintentarian en el mismo instante y saturarian el AP. */
#define S360_BACKOFF_MIN_MS      1000
#define S360_BACKOFF_MAX_MS      60000
#define S360_BACKOFF_JITTER_MS   1000

/* Presupuestos del enlace, en milisegundos: los valores no funcionales que
 * fija SPEC-01. Se usan para clasificar cada medida como dentro o fuera. */
#define S360_BUDGET_ACK_MS      50
#define S360_BUDGET_PUBLISH_MS  250

/* Ventana de vinculacion (opcion 2 del analisis de varios hubs).
 *
 * El hub solo adopta nodos DESCONOCIDOS mientras la ventana esta abierta.
 * Cerrada —el estado normal— ignora cualquier PAIR que no sea de un nodo ya
 * vinculado. Asi un hub no roba el nodo de otra planta: no lo oye, o lo oye y
 * lo rechaza.
 *
 * Se abre por dos vias, porque el instalador puede estar en dos situaciones:
 *   - MQTT: esta en la casa con el movil y el hub ya tiene red.
 *   - Boton fisico (GPIO 0, el BOOT de la placa): el hub aun no tiene red.
 *
 * NO cierra el caso de un vecino que este vinculando a la vez. Reduce el
 * problema de "siempre" a "solo durante la ventana". Cerrarlo del todo exige
 * un token compartido en el PAIR, que es otro cambio. */
#define PAIR_WINDOW_SEC        120      // duracion de la ventana
#define PAIR_BUTTON_PIN        GPIO_NUM_0
#define PAIR_BUTTON_DEBOUNCE_MS 200

#define S360_ESPNOW_CHANNEL  5
static uint8_t router_channel = S360_ESPNOW_CHANNEL;
static char hub_mac_str[18];
static uint8_t hub_mac[6];   /* MAC STA; el frame v1 la lleva como src_mac */

/* --- Prueba de enlace ESP-NOW pedida desde la app -----------------------
 * Solo puede haber una prueba en vuelo: la app lanza una por nodo y espera
 * su resultado, y tener dos simultaneas obligaria a casar respuestas por
 * identificador. El tid se devuelve tal cual para que el portal sepa a que
 * pulsacion corresponde lo que llego. */
typedef struct {
    uint8_t mac[6];
    char    tid[16];
    int     intentos;
    int64_t enviado_us;
    bool    pendiente;
    bool    fallo_radio;
} ping_test_t;
static ping_test_t s_ping;
static esp_timer_handle_t s_ping_timer = NULL;

/* Tres intentos separados 3 s: suficiente para un nodo con el enlace justo,
 * y lo bastante corto para que la app no tenga que esperar un minuto. */
#define PING_INTENTOS_MAX 3
#define PING_ESPERA_US    (3 * 1000 * 1000)

/* Van a continuacion, pero el handler de MQTT las necesita antes. */
static void publish_ping_result(const uint8_t *mac, bool ok, const char *razon,
                                int rtt_ms, int intentos, int seq);
static void hub_enviar_ping(void);
static void on_espnow_sent(const uint8_t *mac_addr, esp_now_send_status_t status);
/* Eventos que no llegaron a publicarse. Se reporta en cada publicacion
 * fallida para que una perdida no pase inadvertida. */
static uint32_t s_events_dropped = 0;
/* MAC del SoftAP. El nodo dirige el PAIR a esta interfaz, y el hub debe
 * reconocerla como propia: sin esto el filtro de destinatario rechazaria
 * los PAIR legitimos. */
static uint8_t hub_ap_mac[6];

/* true si la MAC dada es de este hub (STA o SoftAP). */
static bool mac_es_de_este_hub(const uint8_t *mac)
{
    return memcmp(mac, hub_mac, 6) == 0 || memcmp(mac, hub_ap_mac, 6) == 0;
}

/* Payload del testamento (LWT). Debe sobrevivir mientras viva el cliente
 * MQTT: ESP-IDF no copia el string, solo guarda el puntero. */
static char s_lwt_payload[128];

typedef enum {
    PORTAL_STATE_CONFIGURING,
    PORTAL_STATE_TESTING,
    PORTAL_STATE_CONNECTED,
    PORTAL_STATE_FAILED
} portal_state_t;

static portal_state_t current_portal_state = PORTAL_STATE_CONFIGURING;
static hub_config_t active_config = {0};
static node_registry_t node_registry = {0};
static uint64_t last_portal_activity = 0;
static bool mqtt_is_connected = false;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool sntp_synced = false;

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// ============================================================================
// PROTOTIPOS DE FUNCIONES
// ============================================================================
static void start_mqtt_client(const char *broker_uri);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data);
esp_err_t hub_send_ota_config(const uint8_t *node_mac, const char *ota_url);
static void reconnect_request(void);

/* Ventana de vinculacion: se usan en el handler de MQTT y en el de ESP-NOW,
 * que estan antes de sus definiciones. */
static bool pair_window_open(void);
static void pair_window_open_now(const char *origen);
static uint32_t pair_window_remaining(void);
static void publish_event_to_mqtt(const uint8_t *sender_mac, security_payload_t *payload,
                                  int64_t rx_us);
static void format_mac(const uint8_t *mac, char *out, size_t out_size);
static void get_mqtt_topic(char *out_buffer, size_t buffer_size, const char *suffix);

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

static void url_decode(char *dst, const char *src, size_t dst_size) {
    char a, b;
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++, j++) {
        if (src[i] == '+') dst[j] = ' ';
        else if (src[i] == '%' && src[i+1] && src[i+2]) {
            a = src[i+1]; b = src[i+2];
            dst[j] = ((a <= '9' ? a - '0' : (a & 0x7) + 9) << 4) | 
                     (b <= '9' ? b - '0' : (b & 0x7) + 9);
            i += 2;
        } else {
            dst[j] = src[i];
        }
    }
    dst[j] = '\0';
}

static void get_form_param(const char *buf, const char *name, char *out, size_t out_size) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", name);
    const char *start = strstr(buf, search);
    if (start) {
        start += strlen(search);
        const char *end = strchr(start, '&');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (len >= out_size) len = out_size - 1;
        
        char raw[256];
        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
        strncpy(raw, start, len);
        raw[len] = '\0';
        url_decode(out, raw, out_size);
    }
}

static void format_mac(const uint8_t *mac, char *out, size_t out_size) {
    snprintf(out, out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void get_mqtt_topic(char *out_buffer, size_t buffer_size, const char *suffix) {
    snprintf(out_buffer, buffer_size, "seguimiento360/tenant/%s/site/%s/hub/%s/%s", 
             active_config.tenant_id, active_config.site_id, hub_mac_str, suffix);
}


// ============================================================================
// SNTP: SINCRONIZACIÓN DE HORA
// ============================================================================
static void sntp_time_sync_notification(struct timeval *tv) {
    ESP_LOGI(TAG, "🕐 Hora sincronizada vía SNTP");
    sntp_synced = true;
}

static void init_sntp(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_time_sync_notification);
    esp_sntp_init();
    setenv("TZ", "UTC0", 1);
    tzset();
    ESP_LOGI(TAG, "SNTP iniciado (pool.ntp.org, zona UTC)");
}

// Devuelve timestamp UNIX actual, o 0 si aún no hay hora sincronizada
static uint32_t get_unix_now(void) {
    time_t now = time(NULL);
    if (now < 1700000000) return 0;  // Antes de 2023 = no sincronizado
    return (uint32_t)now;
}

// Busca el índice de un nodo por MAC. Devuelve -1 si no existe
static int find_node_index(const uint8_t *mac) {
    for (int i = 0; i < node_registry.count; i++) {
        if (memcmp(node_registry.nodes[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// ============================================================================
// FUNCIONES NVS - CONFIGURACIÓN DEL HUB
// ============================================================================

static esp_err_t load_config(void) {
    esp_err_t err = nvs_open("pairing", NVS_READWRITE, &nvs_handle_pairing);
    if (err != ESP_OK) return err;

    size_t required_size = sizeof(hub_config_t);
    err = nvs_get_blob(nvs_handle_pairing, "hub_config", &active_config, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Intentar migrar desde estructura antigua "wifi_creds"
        wifi_credentials_t old_creds;
        size_t old_size = sizeof(wifi_credentials_t);
        if (nvs_get_blob(nvs_handle_pairing, "wifi_creds", &old_creds, &old_size) == ESP_OK) {
            ESP_LOGI(TAG, "Migrando configuración legacy a multi-tenant...");
            active_config.version = 3;
            strncpy(active_config.ssid, old_creds.ssid, sizeof(active_config.ssid) - 1);
            active_config.ssid[sizeof(active_config.ssid) - 1] = '\0';
            strncpy(active_config.password, old_creds.password, sizeof(active_config.password) - 1);
            active_config.password[sizeof(active_config.password) - 1] = '\0';
            strcpy(active_config.tenant_id, "0");
            strcpy(active_config.site_id, "site0");
            nvs_set_blob(nvs_handle_pairing, "hub_config", &active_config, sizeof(hub_config_t));
            nvs_commit(nvs_handle_pairing);
            nvs_erase_key(nvs_handle_pairing, "wifi_creds");
            wifi_configured = true;
        } else {
            memset(&active_config, 0, sizeof(active_config));
            active_config.version = 3;
            strcpy(active_config.tenant_id, "0");
            strcpy(active_config.site_id, "site0");
        }
    } else if (err == ESP_OK) {
        if (active_config.version < 2) {
            strcpy(active_config.tenant_id, "0");
            strcpy(active_config.site_id, "site0");
            active_config.version = 3;
            nvs_set_blob(nvs_handle_pairing, "hub_config", &active_config, sizeof(hub_config_t));
            nvs_commit(nvs_handle_pairing);
        }
        if (strlen(active_config.ssid) > 0) {
            wifi_configured = true;
        }
    }

    /* hub_name nunca se asignaba en ningun sitio, asi que HUB_ONLINE y
     * HUB_OFFLINE lo publicaban vacio. Si no hay nombre configurado se
     * deriva uno de la MAC: asi el hub es identificable en el broker sin
     * exigir configuracion adicional. */
    if (strlen(active_config.hub_name) == 0) {
        uint8_t m[6];
        esp_read_mac(m, ESP_MAC_WIFI_STA);
        snprintf(active_config.hub_name, sizeof(active_config.hub_name),
                 "Hub-%02X%02X%02X", m[3], m[4], m[5]);
        ESP_LOGI(TAG, "hub_name derivado de la MAC: %s", active_config.hub_name);
    }
    return ESP_OK;
}

static esp_err_t save_config(void) {
    active_config.version = 3;
    esp_err_t err = nvs_set_blob(nvs_handle_pairing, "hub_config", &active_config, sizeof(hub_config_t));
    if (err == ESP_OK) err = nvs_commit(nvs_handle_pairing);
    return err;
}

// ============================================================================
// FUNCIONES NVS - REGISTRO DE NODOS
// ============================================================================

static void load_node_registry(void) {
    size_t required_size = sizeof(node_registry_t);
    esp_err_t err = nvs_get_blob(nvs_handle_pairing, "node_registry", &node_registry, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&node_registry, 0, sizeof(node_registry_t));
        ESP_LOGI(TAG, "Registro de nodos inicializado (vacío).");
    } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        // Estructura antigua detectada → limpieza y reinicio del registro
        ESP_LOGW(TAG, "⚠️ Registro con estructura antigua detectado. Limpiando y reiniciando.");
        nvs_erase_key(nvs_handle_pairing, "node_registry");
        nvs_commit(nvs_handle_pairing);
        memset(&node_registry, 0, sizeof(node_registry_t));
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "Registro de nodos cargado: %d nodos vinculados.", node_registry.count);

        /* last_ping se conserva tal como quedo en NVS a proposito.
         *
         * Refrescarlo al arrancar parecia comodo, pero rompe el proposito de
         * la vigilancia: si el hub estuvo caido 6 horas y el nodo lleva todo
         * ese tiempo en silencio, el backend debe enterarse. Un nodo callado
         * sigue siendo un nodo perdido aunque el hub acabe de arrancar.
         *
         * Efecto buscado: si el hub vuelve tras una ausencia mayor que el
         * umbral, avisa de los nodos que siguen sin dar señales. El aviso es
         * unico por nodo: last_ping se pone a 0 al publicarlo. */
    } else {
        ESP_LOGE(TAG, "Error al cargar registro de nodos: %s", esp_err_to_name(err));
        memset(&node_registry, 0, sizeof(node_registry_t));
    }
}

static void save_node_registry(void) {
    nvs_set_blob(nvs_handle_pairing, "node_registry", &node_registry, sizeof(node_registry_t));
    nvs_commit(nvs_handle_pairing);
}

static bool is_mac_registered(const uint8_t *mac) {
    for (int i = 0; i < node_registry.count; i++) {
        if (memcmp(node_registry.nodes[i].mac, mac, 6) == 0) return true;
    }
    return false;
}

/* El LED es simple, NO direccionable.
 *
 * Se comprobo en el banco: un WS2812 no se enciende con gpio_set_level, y
 * este si. La documentacion de hardware dice "LED RGB WS2812" en tres sitios,
 * pero la medicion dice lo contrario: es un LED de un solo color. Conviene
 * corregir esa documentacion.
 *
 * Con un LED asi, la unica forma de transmitir varios estados es usar
 * PATRONES distintos. Antes todos los eventos hacian "N destellos iguales",
 * y por eso no se distinguia si el hub estaba emparejando, armado o avisando
 * de un error.
 *
 *   fijo             alarma armada (es un estado, no un aviso)
 *   parpadeo lento   ventana de emparejamiento abierta
 *   parpadeo rapido  problema: limite alcanzado
 *   dos destellos    arranque correcto */
#define LED_EMPAREJA_MS  700    /* lento: "estoy escuchando" */
#define LED_ERROR_MS     120    /* rapido: algo va mal */

static void blink_indicator(int times, int delay_ms) {
    for (int i = 0; i < times; i++) {
        gpio_set_level(INDICATOR_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(INDICATOR_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// ============================================================================
// MQTT: CONFIGURACIÓN, EVENTOS Y PUBLICACIÓN
// ============================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ MQTT_EVENT_CONNECTED");
    // Publicar estado HUB_ONLINE al servidor central
    char mac_str[18];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    /* La version viaja en HUB_ONLINE para que el backend sepa que firmware
     * corre cada hub sin necesitar acceso a su red local. Es la base para
     * decidir a cuales mandar un ota_cmd. */
    const esp_app_desc_t *app = esp_app_get_description();
    const char *running_ver = (app != NULL) ? app->version : "desconocida";
    const esp_partition_t *part = esp_ota_get_running_partition();
    const char *part_label = (part != NULL) ? part->label : "?";

    /* Uptime y motivo del ultimo reinicio (HUB-NF-003).
     *
     * El requisito pide 30 dias sin reset manual, y eso no se puede medir en
     * una sesion. Lo que si se puede es dejar el dato: con el uptime, quien
     * consulte el broker sabe cuanto lleva en pie este hub, y con el motivo
     * distingue un reinicio previsto de uno por fallo. Un motivo distinto de
     * SW_REST o POWERON apunta a watchdog, pila o pánico. */
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    esp_reset_reason_t rr = esp_reset_reason();
    const char *rr_str = "OTRO";
    switch (rr) {
        case ESP_RST_POWERON:  rr_str = "POWERON";  break;
        case ESP_RST_SW:       rr_str = "SW_REST";  break;
        case ESP_RST_PANIC:    rr_str = "PANIC";    break;
        case ESP_RST_INT_WDT:  rr_str = "INT_WDT";  break;
        case ESP_RST_TASK_WDT: rr_str = "TASK_WDT"; break;
        case ESP_RST_WDT:      rr_str = "WDT";      break;
        case ESP_RST_BROWNOUT: rr_str = "BROWNOUT"; break;
        case ESP_RST_DEEPSLEEP:rr_str = "DEEPSLEEP";break;
        case ESP_RST_EXT:      rr_str = "EXT";      break;
        /* El hub va por USB nativo: un reinicio lanzado con DTR (idf.py
         * monitor, esptool) sale como USB, no como SW. Sin este caso el
         * motivo mas frecuente en desarrollo aparecia como "OTRO". */
        case ESP_RST_USB:      rr_str = "USB";      break;
        case ESP_RST_JTAG:     rr_str = "JTAG";     break;
        case ESP_RST_SDIO:     rr_str = "SDIO";     break;
        case ESP_RST_PWR_GLITCH: rr_str = "PWR_GLITCH"; break;
        case ESP_RST_EFUSE:    rr_str = "EFUSE";    break;
        case ESP_RST_UNKNOWN:  rr_str = "UNKNOWN";  break;
        default: break;
    }

    char online_payload[420];
    snprintf(online_payload, sizeof(online_payload),
        "{\"event\":\"HUB_ONLINE\",\"mac\":\"%s\",\"hub_name\":\"%s\","
        "\"version\":\"%s\",\"partition\":\"%s\","
        "\"uptime_s\":%lu,\"reset_reason\":\"%s\"}",
        mac_str, active_config.hub_name, running_ver, part_label,
        (unsigned long)uptime_s, rr_str);
    
    /* Retenido, igual que el last will: asi el broker guarda la ultima verdad
     * del hub y el que se suscribe despues (Node-RED al arrancar) no hereda un
     * OFFLINE fantasma. */
    esp_mqtt_client_publish(mqtt_client, "seguimiento360/hub/status", online_payload, 0, 1, 1);
    ESP_LOGI(TAG, "📡 Publicado HUB_ONLINE al servidor central");

            mqtt_is_connected = true;
        
        // --- NUEVO: Suscribirse a comandos OTA ---
        char ota_cmd_topic[128];
        get_mqtt_topic(ota_cmd_topic, sizeof(ota_cmd_topic), "ota_cmd");
        esp_mqtt_client_subscribe(mqtt_client, ota_cmd_topic, 1);
        ESP_LOGI(TAG, "Suscrito a comandos OTA: %s", ota_cmd_topic);

        /* Cancelacion del rollback (HUB-F-012).
         *
         * Con CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, una imagen recien
         * flasheada arranca en estado PENDING_VERIFY y el bootloader la
         * revierte si nadie la confirma. Sin esta llamada toda actualizacion
         * OTA se desharia sola a los 30 s.
         *
         * Se confirma aqui, al conectar MQTT, y no al final de app_main: asi
         * el rollback protege tambien el caso en que la imagen nueva arranca
         * pero es incapaz de alcanzar el broker. */
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                esp_err_t merr = esp_ota_mark_app_valid_cancel_rollback();
                if (merr == ESP_OK) {
                    const esp_app_desc_t *d = esp_app_get_description();
                    ESP_LOGW(TAG, "Imagen %s confirmada: rollback cancelado.", d->version);
                } else {
                    ESP_LOGE(TAG, "No se pudo confirmar la imagen: %s", esp_err_to_name(merr));
                }
            }
        }

        // Suscribirse a configuración de nodos
        char node_cfg_topic[128];
        get_mqtt_topic(node_cfg_topic, sizeof(node_cfg_topic), "node_cfg");
        esp_mqtt_client_subscribe(mqtt_client, node_cfg_topic, 1);
        ESP_LOGI(TAG, "📡 Suscrito a configuración de nodos: %s", node_cfg_topic);

        /* Comandos de armado/desarmado (HUB-F-004/005, AT-002) */
        char cmd_topic[128];
        get_mqtt_topic(cmd_topic, sizeof(cmd_topic), "command");
        esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
        ESP_LOGI(TAG, "🛡️  Suscrito a comandos de alarma: %s", cmd_topic);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "❌ MQTT_EVENT_DISCONNECTED");
            mqtt_is_connected = false;
            /* El cliente MQTT reintenta por su cuenta, pero si la causa es el
             * enlace, quien tiene que actuar es la reconexion de WiFi. */
            reconnect_request();
            mqtt_is_connected = false;
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA en tópico: %.*s", event->topic_len, event->topic);

            // --- Lógica para disparar OTA ---
            if (strstr(event->topic, "ota_cmd") != NULL) {
                ESP_LOGI(TAG, "Comando OTA recibido. Longitud: %d", event->data_len);
                
                char *url_buffer = calloc(1, event->data_len + 2);
                if (url_buffer != NULL) {
                    memcpy(url_buffer, event->data, event->data_len);
                    url_buffer[event->data_len] = '\0';
                    
                    // 🧹 LIMPIEZA ROBUSTA: Buscar el inicio de "http" o "https" e ignorar cualquier basura anterior
                    char *start = strstr(url_buffer, "http");
                    if (start == NULL) {
                        ESP_LOGE(TAG, "❌ No se encontró 'http' en el payload. Volcado HEX del payload recibido:");
                        ESP_LOG_BUFFER_HEX(TAG, url_buffer, event->data_len);
                        free(url_buffer);
                    } else {
                        // Encontrar el final de la URL (detenerse en el primer caracter no imprimible, espacio o comilla)
                        char *end = start;
                        while (*end != '\0' && isprint((unsigned char)*end) && *end != ' ' && *end != '\n' && *end != '\r' && *end != '"') {
                            end++;
                        }
                        
                        size_t clean_len = end - start;
                        memmove(url_buffer, start, clean_len);
                        url_buffer[clean_len] = '\0'; // Asegurar null-termination

                        /* La URL llega por MQTT y decide que firmware se
                         * instala, asi que no se acepta cualquiera: solo
                         * HTTPS y solo el origen propio. Antes bastaba con
                         * que el payload contuviera "http" en algun sitio,
                         * de modo que un ota_cmd podia apuntar a un servidor
                         * ajeno y el hub lo instalaba. */
                        if (strncmp(url_buffer, "https://", 8) != 0) {
                            ESP_LOGE(TAG, "OTA rechazada: la URL no es HTTPS.");
                        } else if (strncmp(url_buffer + 8, S360_OTA_HOST, strlen(S360_OTA_HOST)) != 0) {
                            ESP_LOGE(TAG, "OTA rechazada: origen distinto de %s.", S360_OTA_HOST);
                        } else {
                            ESP_LOGI(TAG, "URL de OTA validada: '%s'", url_buffer);
                            esp_err_t ret = ota_manager_start(url_buffer);
                            if (ret != ESP_OK) {
                                ESP_LOGE(TAG, "Error al iniciar tarea OTA: %s", esp_err_to_name(ret));
                            }
                        }
                        
                    }
                }
            }
            // --- MANEJO DE CONFIGURACIÓN DE NODOS ---
            else if (strstr(event->topic, "command") != NULL) {
                /* --- COMANDOS DE ALARMA (AT-002) --- */
                char *cmd_data = calloc(1, event->data_len + 1);
                if (cmd_data != NULL) {
                    memcpy(cmd_data, event->data, event->data_len);
                    ESP_LOGI(TAG, "Comando de alarma recibido: %s", cmd_data);

                    /* Vinculacion por MQTT: la app abre la ventana para que el
                     * hub acepte un nodo nuevo. Es la via comoda cuando el hub
                     * ya tiene red y el instalador esta con el movil. */
                    if (strstr(cmd_data, "pairing") != NULL) {
                        pair_window_open_now("MQTT");
                        free(cmd_data);
                        return;
                    }

                    /* Olvidar un nodo: se usa al sustituir un sensor averiado,
                     * para que el hub acepte el nuevo en su lugar. */
                    if (strstr(cmd_data, "unpair") != NULL) {
                        cJSON *raiz = cJSON_Parse(cmd_data);
                        cJSON *mac_json = raiz ? cJSON_GetObjectItem(raiz, "mac") : NULL;
                        uint8_t a_borrar[6];
                        if (cJSON_IsString(mac_json) &&
                            sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                                   &a_borrar[0], &a_borrar[1], &a_borrar[2],
                                   &a_borrar[3], &a_borrar[4], &a_borrar[5]) == 6) {
                            int idx = find_node_index(a_borrar);
                            if (idx >= 0) {
                                for (int i = idx; i < node_registry.count - 1; i++) {
                                    node_registry.nodes[i] = node_registry.nodes[i + 1];
                                }
                                node_registry.count--;
                                save_node_registry();
                                esp_now_del_peer(a_borrar);
                                ESP_LOGW(TAG, "Nodo %s olvidado. Quedan %d/%d.",
                                         mac_json->valuestring,
                                         node_registry.count, MAX_NODES);
                            } else {
                                ESP_LOGW(TAG, "unpair: el nodo %s no estaba registrado.",
                                         mac_json->valuestring);
                            }
                        } else {
                            ESP_LOGW(TAG, "unpair: falta 'mac' en el comando.");
                        }
                        if (raiz) cJSON_Delete(raiz);
                        free(cmd_data);
                        return;
                    }

                    /* Prueba de enlace en vivo pedida desde la pantalla de
                     * nodos. Responde por /diag, no por aqui. */
                    if (strstr(cmd_data, "ping") != NULL) {
                        cJSON *raiz = cJSON_Parse(cmd_data);
                        cJSON *mac_json = raiz ? cJSON_GetObjectItem(raiz, "mac") : NULL;
                        cJSON *tid_json = raiz ? cJSON_GetObjectItem(raiz, "tid") : NULL;
                        uint8_t objetivo[6];

                        if (cJSON_IsString(mac_json) &&
                            sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                                   &objetivo[0], &objetivo[1], &objetivo[2],
                                   &objetivo[3], &objetivo[4], &objetivo[5]) == 6) {
                            memset(&s_ping, 0, sizeof(s_ping));
                            memcpy(s_ping.mac, objetivo, 6);
                            if (cJSON_IsString(tid_json) && tid_json->valuestring) {
                                strncpy(s_ping.tid, tid_json->valuestring,
                                        sizeof(s_ping.tid) - 1);
                            }

                            if (find_node_index(objetivo) < 0) {
                                /* El hub no lo tiene: la app se entera de que
                                 * el problema no es de radio sino de vinculo. */
                                s_ping.pendiente = false;
                                publish_ping_result(objetivo, false, "no_vinculado",
                                                    0, 0, -1);
                            } else {
                                s_ping.pendiente = true;
                                s_ping.intentos = 1;
                                hub_enviar_ping();
                                esp_timer_start_once(s_ping_timer, PING_ESPERA_US);
                            }
                        } else {
                            ESP_LOGW(TAG, "ping: falta 'mac' en el comando.");
                        }
                        if (raiz) cJSON_Delete(raiz);
                        free(cmd_data);
                        return;
                    }

                    sm_alarm_cmd_t cmd = SM_ALARM_CMD_NONE;
                    if (strstr(cmd_data, "arm") != NULL && strstr(cmd_data, "disarm") == NULL) {
                        cmd = SM_ALARM_CMD_ARM;
                    } else if (strstr(cmd_data, "disarm") != NULL) {
                        cmd = SM_ALARM_CMD_DISARM;
                    }

                    if (cmd != SM_ALARM_CMD_NONE) {
                        esp_err_t aerr = sm_alarm_process_cmd(cmd);
                        /* El LED queda fijo encendido si esta armado. */
                        gpio_set_level(INDICATOR_LED_PIN,
                                       sm_alarm_get_state() == SM_ALARM_STATE_ARMED ? 1 : 0);
                        ESP_LOGI(TAG, "Estado de alarma: %s (%s)",
                                 sm_alarm_state_str(sm_alarm_get_state()),
                                 aerr == ESP_OK ? "ok" : esp_err_to_name(aerr));
                    } else {
                        ESP_LOGW(TAG, "Comando no reconocido: %s", cmd_data);
                    }
                    free(cmd_data);
                }
            }
            else if (strstr(event->topic, "node_cfg") != NULL) {
                ESP_LOGI(TAG, "📥 Configuración de nodo recibida en %.*s", event->topic_len, event->topic);
                
                char *data = calloc(1, event->data_len + 1);
                if (data != NULL) {
                    memcpy(data, event->data, event->data_len);
                    
                    cJSON *root = cJSON_Parse(data);
                    if (root) {
                        cJSON *mac_json = cJSON_GetObjectItem(root, "node_mac");
                        cJSON *name = cJSON_GetObjectItem(root, "node_name");
                        cJSON *location = cJSON_GetObjectItem(root, "location");
                        cJSON *room = cJSON_GetObjectItem(root, "room");
                        cJSON *threshold = cJSON_GetObjectItem(root, "distance_threshold");

                        if (cJSON_IsString(mac_json) && cJSON_IsString(name) && cJSON_IsString(location) && 
                            cJSON_IsString(room) && cJSON_IsNumber(threshold)) {
                            
                            uint8_t parsed_mac[6];
                            if (sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                                       &parsed_mac[0], &parsed_mac[1], &parsed_mac[2], 
                                       &parsed_mac[3], &parsed_mac[4], &parsed_mac[5]) == 6) {
                                
                                int idx = find_node_index(parsed_mac);
                                if (idx >= 0) {
                                    strncpy(node_registry.nodes[idx].name, name->valuestring, sizeof(node_registry.nodes[idx].name) - 1);
                                    strncpy(node_registry.nodes[idx].location, location->valuestring, sizeof(node_registry.nodes[idx].location) - 1);
                                    strncpy(node_registry.nodes[idx].room, room->valuestring, sizeof(node_registry.nodes[idx].room) - 1);
                                    node_registry.nodes[idx].distance_threshold = (uint8_t)threshold->valueint;
                                    
                                    save_node_registry();
                                    ESP_LOGI(TAG, "✅ Nodo %s actualizado y guardado en NVS", name->valuestring);
                                    
                                    char espnow_cmd[128];
                                    snprintf(espnow_cmd, sizeof(espnow_cmd), "CFG_FULL:%s|%s|%s|%d", 
                                             node_registry.nodes[idx].name, 
                                             node_registry.nodes[idx].location, 
                                             node_registry.nodes[idx].room, 
                                             node_registry.nodes[idx].distance_threshold);
                                    
                                    esp_now_peer_info_t peer = {0};
                                    memcpy(peer.peer_addr, node_registry.nodes[idx].mac, 6);
                                    peer.channel = S360_ESPNOW_CHANNEL;
                                    peer.encrypt = false;
                                    esp_now_add_peer(&peer);
                                    
                                    esp_err_t err = esp_now_send(node_registry.nodes[idx].mac, (uint8_t *)espnow_cmd, strlen(espnow_cmd));
                                    if (err == ESP_OK) {
                                        ESP_LOGI(TAG, "📤 Configuración enviada al nodo vía ESP-NOW");
                                    } else {
                                        ESP_LOGE(TAG, "❌ Error al enviar ESP-NOW: %d", err);
                                    }
                                } else {
                                    ESP_LOGW(TAG, "⚠️ Nodo con MAC %s no encontrado en el registro", mac_json->valuestring);
                                }
                            }

                        }

                        /* Actualizacion del nodo (FW-012). Va aqui, fuera del
                         * bloque que exige nombre, ubicacion y umbral: un
                         * comando de actualizacion solo necesita la MAC y la
                         * URL, y con la validacion estricta nunca llegaba. */
                        cJSON *ota_url = cJSON_GetObjectItem(root, "ota_url");
                        cJSON *mac_only = cJSON_GetObjectItem(root, "node_mac");
                        if (cJSON_IsString(ota_url) && cJSON_IsString(mac_only)) {
                            uint8_t ota_mac[6];
                            if (sscanf(mac_only->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                                       &ota_mac[0], &ota_mac[1], &ota_mac[2],
                                       &ota_mac[3], &ota_mac[4], &ota_mac[5]) == 6) {
                                if (find_node_index(ota_mac) < 0) {
                                    ESP_LOGW(TAG, "OTA: el nodo %s no esta vinculado.",
                                             mac_only->valuestring);
                                } else {
                                    ESP_LOGI(TAG, "OTA solicitada para %s: %s",
                                             mac_only->valuestring, ota_url->valuestring);
                                    hub_send_ota_config(ota_mac, ota_url->valuestring);
                                }
                            }
                        }

                        cJSON_Delete(root);
                    } else {
                        ESP_LOGE(TAG, "❌ Error al parsear JSON de configuración de nodo");
                    }
                    free(data);
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last errno: %s", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}

static void start_mqtt_client(const char *broker_uri) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char client_id_str[32];
    snprintf(client_id_str, sizeof(client_id_str), "hub_%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

   /* Testamento: si el hub se cae o pierde la conexion, el broker publica
    * este mensaje en su nombre. Es lo unico que permite al backend distinguir
    * un hub caido de uno simplemente en silencio. Retenido, para que lo vea
    * quien se suscriba despues del suceso. */
   snprintf(s_lwt_payload, sizeof(s_lwt_payload),
            "{\"event\":\"HUB_OFFLINE\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\"}",
            hub_mac[0], hub_mac[1], hub_mac[2], hub_mac[3], hub_mac[4], hub_mac[5]);

   esp_mqtt_client_config_t mqtt_cfg = {
       .broker.address.uri = broker_uri,
       /* Cola de salida. Con el valor por defecto se llenaba en rafaga: la
        * publicacion bloqueaba hasta 10 s (medido en AT-004) y los eventos
        * siguientes se perdian. 4 KB dan holgura para rafagas de alarma. */
       .buffer.out_size = 4096,
       .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
       .credentials.client_id = client_id_str,
       // ✅ NUEVAS LÍNEAS DE SEGURIDAD:
       .credentials.username = "s360_hub",
       .credentials.authentication.password = S360_MQTT_PASSWORD,
       // Testamento (HUB-F-009, AT-005)
       .session.last_will = {
           .topic   = "seguimiento360/hub/status",
           .msg     = s_lwt_payload,
           .msg_len = 0,   /* 0 = usar strlen */
           .qos     = 1,
           .retain  = 1,
       },
   };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client) {
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_err_t ret = esp_mqtt_client_start(mqtt_client);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Cliente MQTT iniciado. URI: %s, ClientID: %s", broker_uri, client_id_str);
        } else {
            ESP_LOGE(TAG, "Error al iniciar cliente MQTT: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Error al inicializar el cliente MQTT");
    }
}

static void publish_event_to_mqtt(const uint8_t *sender_mac, security_payload_t *payload,
                                  int64_t rx_us) {
    if (mqtt_client == NULL || !mqtt_is_connected) {
        ESP_LOGW(TAG, "MQTT no está conectado. Evento descartado.");
        s_events_dropped++;
        return;
    }

    char mac_str[18];
    format_mac(sender_mac, mac_str, sizeof(mac_str));

    char event_str[5] = {0};
    memcpy(event_str, payload->event_code, 4);

    // Construir tópico dinámico multi-tenant
    char mqtt_topic[128];
    get_mqtt_topic(mqtt_topic, sizeof(mqtt_topic), "event");

    // Buscar información del nodo para enriquecer el payload
    int node_idx = find_node_index(sender_mac);
    const char *node_name = "Nodo Desconocido";
    const char *location = "Sin Ubicación";
    const char *room = "Sin Habitación";
    uint8_t value = payload->value;   // dato del evento (semántica por event_code)

    if (node_idx >= 0) {
        node_name = node_registry.nodes[node_idx].name;
        location = node_registry.nodes[node_idx].location;
        room = node_registry.nodes[node_idx].room;
    }

    // Construir payload JSON enriquecido para Node-RED (aumentado a 512 bytes)
    char json_payload[512];
    snprintf(json_payload, sizeof(json_payload),
        "{"
        "\"mac\":\"%s\","
        "\"node_name\":\"%s\","
        "\"location\":\"%s\","
        "\"room\":\"%s\","
        "\"event\":\"%s\","
        "\"seq\":%d,"
        "\"battery\":%d,"
        "\"value\":%d,"
        "\"tenant\":\"%s\","
        "\"site\":\"%s\""
        "}",
        mac_str, node_name, location, room, event_str, 
        payload->seq, payload->battery_level, value,
        active_config.tenant_id, active_config.site_id
    );

    ESP_LOGI(TAG, "📤 Publicando en MQTT -> Topic: %s", mqtt_topic);
    ESP_LOGI(TAG, "📦 Payload: %s", json_payload);

    // Publicar con QoS 1 (al menos una vez)
    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic, json_payload, 0, 1, 0);
    if (msg_id > 0) {
        ESP_LOGI(TAG, "✅ Evento publicado exitosamente (msg_id=%d)", msg_id);
    } else {
        /* msg_id <= 0 significa que el cliente no acepto el mensaje: la cola
         * esta llena o no hay conexion. Antes se registraba y ya; ahora se
         * cuenta, porque un evento perdido no debe ser invisible. */
        s_events_dropped++;
        ESP_LOGE(TAG, "❌ Evento MQTT descartado (msg_id=%d). Perdidos desde el arranque: %lu",
                 msg_id, (unsigned long)s_events_dropped);
    }

    /* Latencia RX->publish. Se mide hasta la entrega al cliente MQTT, no
     * hasta la confirmacion del broker: esp_mqtt_client_publish es
     * asincrono y esperar el PUBACK mediria otra cosa. */
    if (rx_us > 0) {
        uint32_t ms = (uint32_t)((esp_timer_get_time() - rx_us) / 1000);
        if (ms > S360_BUDGET_PUBLISH_MS) {
            ESP_LOGW(TAG, "RX->publish en %lu ms (presupuesto %d ms SUPERADO)",
                     (unsigned long)ms, S360_BUDGET_PUBLISH_MS);
        } else {
            ESP_LOGI(TAG, "RX->publish en %lu ms (presupuesto %d ms)",
                     (unsigned long)ms, S360_BUDGET_PUBLISH_MS);
        }
    }
}

// ============================================================================
// ESP-NOW: RECEPCIÓN DE DATOS Y VINCULACIÓN
// ============================================================================

/* Envia el ACK al nodo y reporta la latencia RX->ACK.
 *
 * `rx_us` es el instante de recepcion, tomado lo antes posible en
 * on_data_recv(). La medida se toma tras esp_now_send() porque el coste
 * relevante es el que percibe el nodo: el que espera su confirmacion.
 * Un ACK fuera de presupuesto se registra como advertencia, no como log
 * normal, para que destaque en la consola. */
static void send_ack_v1(const uint8_t *dst_mac, uint16_t seq, int64_t rx_us) {
    espnow_frame_t ack;
    if (espnow_build_frame(&ack, ESPNOW_FT_ACK, seq,
                           hub_mac, dst_mac, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo construir el ACK");
        return;
    }
    esp_err_t err = esp_now_send(dst_mac, (const uint8_t *)&ack,
                                 espnow_frame_size(0));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ACK no enviado: %s", esp_err_to_name(err));
        return;
    }

    if (rx_us > 0) {
        uint32_t ms = (uint32_t)((esp_timer_get_time() - rx_us) / 1000);
        if (ms > S360_BUDGET_ACK_MS) {
            ESP_LOGW(TAG, "ACK seq=%u en %lu ms (presupuesto %d ms SUPERADO)",
                     seq, (unsigned long)ms, S360_BUDGET_ACK_MS);
        } else {
            ESP_LOGI(TAG, "ACK seq=%u en %lu ms (presupuesto %d ms)",
                     seq, (unsigned long)ms, S360_BUDGET_ACK_MS);
        }
    }
}

/* Manda al nodo las credenciales WiFi de la casa y la URL del servidor OTA
 * (FW-012), en un frame ESPNOW_FT_CMD.
 *
 * El nodo no tiene pila de red ni forma de conocer la WiFi por si mismo: sin
 * esto no puede descargar su firmware y sigue dependiendo del cable.
 * Devuelve ESP_ERR_INVALID_STATE si el hub no tiene credenciales guardadas. */
esp_err_t hub_send_ota_config(const uint8_t *node_mac, const char *ota_url)
{
    if (node_mac == NULL || ota_url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(active_config.ssid) == 0) {
        ESP_LOGW(TAG, "Sin SSID configurado en el hub: no hay credenciales que enviar.");
        return ESP_ERR_INVALID_STATE;
    }

    espnow_payload_config_t cfg = {0};
    cfg.magic = ESPNOW_CONFIG_MAGIC;
    strncpy(cfg.ssid, active_config.ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, active_config.password, sizeof(cfg.password) - 1);
    strncpy(cfg.ota_url, ota_url, sizeof(cfg.ota_url) - 1);

    espnow_frame_t frame;
    uint16_t seq = (uint16_t)(esp_timer_get_time() / 1000000);
    if (espnow_build_frame(&frame, ESPNOW_FT_CMD, seq, hub_mac, node_mac,
                           (const uint8_t *)&cfg, sizeof(cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo construir el frame de configuracion");
        return ESP_FAIL;
    }

    esp_err_t err = esp_now_send(node_mac, (const uint8_t *)&frame,
                                 espnow_frame_size(sizeof(cfg)));
    if (err == ESP_OK) {
        /* Se registra la longitud y no el valor: la contrasena no debe
         * aparecer en los logs. Una longitud de 0 delata el problema. */
        ESP_LOGI(TAG, "Config OTA enviada al nodo (SSID '%s', clave de %u bytes, URL '%s')",
                 cfg.ssid, (unsigned)strlen(cfg.password), cfg.ota_url);
    } else {
        ESP_LOGE(TAG, "No se pudo enviar la config: %s", esp_err_to_name(err));
    }
    return err;
}

/* Publica el resultado de la prueba en el topico /diag del hub. No va por
 * /event a proposito: un PING no es una alarma, y si pasara por alli acabaria
 * en la base de datos y en el movil de todos como si fuera un aviso. */
static void publish_ping_result(const uint8_t *mac, bool ok, const char *razon,
                                int rtt_ms, int intentos, int seq) {
    if (mqtt_client == NULL || !mqtt_is_connected) {
        ESP_LOGW(TAG, "PING_RESULT no publicado: MQTT caido.");
        return;
    }

    char topic[128];
    get_mqtt_topic(topic, sizeof(topic), "diag");

    char mac_str[18];
    format_mac(mac, mac_str, sizeof(mac_str));

    char payload[320];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"PING_RESULT\",\"mac\":\"%s\",\"ok\":%s,"
             "\"rtt_ms\":%d,\"attempts\":%d,\"seq\":%d,"
             "\"reason\":\"%s\",\"tid\":\"%s\"}",
             mac_str, ok ? "true" : "false", rtt_ms, intentos, seq,
             razon ? razon : "", s_ping.tid);

    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Diag: PING %s -> %s (rtt=%d ms, intentos=%d)",
             mac_str, ok ? "OK" : razon, rtt_ms, intentos);
}


/* Latido de estado por MQTT.

   Sin esto el hub solo se anunciaba al conectar, y como el broker conserva el
   last will como mensaje retenido, cada reinicio de Node-RED vuelve a recibir
   ese HUB_OFFLINE y deja el hub como caido hasta que alguien lo reinicia a
   mano. Con un latido cada cinco minutos, hubs.last_seen pasa a ser de verdad
   una prueba de vida: si deja de subir, es que el hub se cayo. El margen
   cubre el ventana de "vivo" que usa el backend (600 s). */
#define HUB_LATIDO_US (5 * 60 * 1000 * 1000)
static esp_timer_handle_t s_latido_timer = NULL;

static void hub_latido_cb(void *arg)
{
    (void)arg;
    if (mqtt_client == NULL || !mqtt_is_connected) {
        ESP_LOGW(TAG, "Latido de estado omitido: MQTT caido.");
        return;
    }

    char mac_str[18];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    format_mac(mac, mac_str, sizeof(mac_str));

    const esp_app_desc_t *app = esp_app_get_description();
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"event\":\"HUB_HEARTBEAT\",\"mac\":\"%s\",\"hub_name\":\"%s\","
        "\"version\":\"%s\",\"uptime_s\":%lu}",
        mac_str, active_config.hub_name,
        (app != NULL) ? app->version : "desconocida",
        (unsigned long)(esp_timer_get_time() / 1000000ULL));

    if (esp_mqtt_client_publish(mqtt_client, "seguimiento360/hub/status",
                                buf, 0, 1, 1) < 0) {
        ESP_LOGW(TAG, "No se publico el latido de estado.");
    } else {
        ESP_LOGI(TAG, "Latido de estado publicado.");
    }
}

static void hub_enviar_ping(void) {
    espnow_payload_ping_t ping;
    memset(&ping, 0, sizeof(ping));
    ping.magic = ESPNOW_PING_MAGIC;
    ping.nonce = (uint8_t)(esp_timer_get_time() & 0xFF);
    strncpy(ping.tid, s_ping.tid, sizeof(ping.tid) - 1);

    espnow_frame_t frame;
    if (espnow_build_frame(&frame, ESPNOW_FT_CMD, (uint16_t)s_ping.intentos,
                           hub_mac, s_ping.mac,
                           (const uint8_t *)&ping, sizeof(ping)) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo construir el PING.");
        s_ping.pendiente = false;
        publish_ping_result(s_ping.mac, false, "frame", 0, s_ping.intentos, -1);
        return;
    }

    s_ping.enviado_us = esp_timer_get_time();
    s_ping.fallo_radio = false;

    esp_err_t err = esp_now_send(s_ping.mac, (const uint8_t *)&frame,
                                 espnow_frame_size(sizeof(ping)));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PING no enviado: %s", esp_err_to_name(err));
        s_ping.pendiente = false;
        publish_ping_result(s_ping.mac, false, "radio", 0, s_ping.intentos, -1);
        return;
    }
    ESP_LOGI(TAG, "PING %d/%d al nodo (esperando PONG).",
             s_ping.intentos, PING_INTENTOS_MAX);
}

static void ping_timer_cb(void *arg) {
    if (!s_ping.pendiente) {
        return;
    }
    if (s_ping.intentos < PING_INTENTOS_MAX) {
        s_ping.intentos++;
        hub_enviar_ping();
        esp_timer_start_once(s_ping_timer, PING_ESPERA_US);
        return;
    }
    /* Se agotaron los reintentos. Importa el matiz: si la radio nos dijo que
     * el nodo no contesta ni a nivel de enlace, el problema es de canal o de
     * alcance; si el enlace fue bueno pero no hubo PONG, el nodo esta vivo
     * pero su firmware no responde a la prueba. */
    s_ping.pendiente = false;
    publish_ping_result(s_ping.mac, false,
                        s_ping.fallo_radio ? "radio" : "sin_respuesta",
                        0, s_ping.intentos, -1);
}

/* Enterado del resultado del envio a nivel 802.11. Un ESP_NOW_SEND_FAIL no
 * significa "el nodo no funciona": significa que su radio no devolvio ACK,
 * que es justo lo que falla cuando hub y nodo no estan en el mismo canal. */
static void on_espnow_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (s_ping.pendiente && memcmp(mac_addr, s_ping.mac, 6) == 0 &&
        status == ESP_NOW_SEND_FAIL) {
        s_ping.fallo_radio = true;
    }
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    /* Instante de recepcion, lo primero que se ejecuta: todo lo que se haga
     * antes se imputaria al presupuesto del ACK. */
    const int64_t rx_us = esp_timer_get_time();

    /* Frame v1: magic, version y CRC16. Un paquete que no valide se descarta. */
    espnow_frame_t frame;
    esp_err_t verr = espnow_validate_frame(data, (size_t)len, &frame);
    if (verr != ESP_OK) {
        ESP_LOGW(TAG, "Frame descartado (%s): %d bytes", esp_err_to_name(verr), len);
        return;
    }

    if (frame.frame_type != ESPNOW_FT_DATA_EVENT) {
        /* Los ACK de otros nodos y los BEACON no se procesan aqui. */
        return;
    }

    if (frame.payload_len != sizeof(security_payload_t)) {
        ESP_LOGW(TAG, "Payload de %u bytes, se esperaban %u",
                 frame.payload_len, (unsigned)sizeof(security_payload_t));
        return;
    }

    /* La MAC de origen va en el frame; el hub la usa como identidad del nodo.
     * info->src_addr es la fuente real del enlace, que debe coincidir. */
    const uint8_t *sender_mac = frame.src_mac;
    char mac_str[18];
    format_mac(sender_mac, mac_str, sizeof(mac_str));

    security_payload_t *payload = (security_payload_t *)frame.payload;

    char event_str[5] = {0};
    memcpy(event_str, payload->event_code, 4);

    ESP_LOGI(TAG, "ESP-NOW RX (v1): Code=%s, MAC=%s, Seq=%d, Bat=%d%%",
             event_str, mac_str, payload->seq, payload->battery_level);

    /* Filtro de destinatario. ESP-NOW entrega a este hub cualquier trama
     * que su radio oiga, incluidas las de nodos de otros hubs cercanos.
     * Un PAIR solo se atiende si va dirigido a una MAC de este hub: de lo
     * contrario dos instalaciones vecinas se emparejarian entre si. */
    if (!mac_es_de_este_hub(frame.dst_mac)) {
        ESP_LOGW(TAG, "Trama para %02x:%02x:%02x:%02x:%02x:%02x, no para este hub. Descartada.",
                 frame.dst_mac[0], frame.dst_mac[1], frame.dst_mac[2],
                 frame.dst_mac[3], frame.dst_mac[4], frame.dst_mac[5]);
        return;
    }

    // --- LÓGICA DE VINCULACIÓN (PAIRING) ---
    if (memcmp(payload->event_code, "PAIR", 4) == 0) {
        ESP_LOGI(TAG, "Solicitud de PAIRING recibida de: %s", mac_str);

        if (is_mac_registered(sender_mac)) {
            ESP_LOGI(TAG, "El nodo %s ya está vinculado. Enviando ACK.", mac_str);
            send_ack_v1(sender_mac, payload->seq, rx_us);
            return;
        }

        if (node_registry.count >= MAX_NODES) {
            ESP_LOGW(TAG, "Límite de nodos alcanzado (%d). Pairing rechazado.", MAX_NODES);
            blink_indicator(5, LED_ERROR_MS);
            return;
        }

        /* La ventana decide si este hub adopta nodos desconocidos.
         *
         * Cerrada es el estado normal: asi el hub del primer piso no adopta el
         * nodo del segundo cuando lo oye. El rechazo se registra porque un
         * silencio dejaria al instalador sin pista de por que no vincula. */
        if (!pair_window_open()) {
            ESP_LOGW(TAG, "PAIR de %s rechazado: la ventana de vinculacion esta cerrada. "
                          "Abrala desde la app o pulsando el boton del hub.",
                     mac_str);
            return;
        }

        ESP_LOGI(TAG, "PAIR de %s aceptado: quedan %lu s de ventana.",
                 mac_str, (unsigned long)pair_window_remaining());

        esp_now_peer_info_t new_peer = { 
            .channel = router_channel, 
            .ifidx = ESP_IF_WIFI_STA, 
            .encrypt = false 
        };
        memcpy(new_peer.peer_addr, sender_mac, 6);
        
        if (esp_now_add_peer(&new_peer) == ESP_OK) {
            const char *dev_type = "GENERIC";
            
            memcpy(node_registry.nodes[node_registry.count].mac, sender_mac, 6);
            strncpy((char *)node_registry.nodes[node_registry.count].type, dev_type, 15);
            node_registry.nodes[node_registry.count].type[15] = '\0';
            node_registry.count++;
            
            save_node_registry();
            
            ESP_LOGI(TAG, "✅ Nodo vinculado exitosamente. Total: %d/%d | Tipo: %s | Batería: %d%%", 
                     node_registry.count, MAX_NODES, dev_type, payload->battery_level);
            
            blink_indicator(3, 100); 

            send_ack_v1(sender_mac, payload->seq, rx_us);
        }
        return;
    }

    /* Respuesta a la prueba de enlace. Se resuelve aqui mismo y no sube como
     * evento: no es una lectura del sensor, y contaminaria el historial. */
    if (memcmp(payload->event_code, "PONG", 4) == 0) {
        if (s_ping.pendiente && memcmp(sender_mac, s_ping.mac, 6) == 0) {
            uint32_t ms = (uint32_t)((rx_us - s_ping.enviado_us) / 1000);
            s_ping.pendiente = false;
            esp_timer_stop(s_ping_timer);
            publish_ping_result(sender_mac, true, "ok", (int)ms,
                                s_ping.intentos, payload->seq);
        } else {
            ESP_LOGW(TAG, "PONG de %s sin prueba pendiente: descartado.", mac_str);
        }
        return;
    }

    // --- LÓGICA DE EVENTOS NORMALES (ALRM, HEAR, etc.) ---
    int node_idx = find_node_index(sender_mac);
    if (node_idx >= 0) {
        // Actualizar last_ping y guardar en NVS
        node_registry.nodes[node_idx].last_ping = get_unix_now();
        save_node_registry();
        publish_event_to_mqtt(sender_mac, payload, rx_us);
    } else {
        ESP_LOGW(TAG, "Paquete ignorado: MAC %s no registrada.", mac_str);
    }
}

// ============================================================================
// WIFI EVENT HANDLER
// ============================================================================

/* Espera para el intento numero `intento` (1 = primero), con jitter.
 *
 * Se calcula en lugar de guardar el valor para que el jitter no se acumule:
 * si se aplicara sobre el valor anterior, la espera derivaria. */
static uint32_t backoff_ms(int intento)
{
    uint32_t base = S360_BACKOFF_MIN_MS;
    for (int i = 1; i < intento && base < S360_BACKOFF_MAX_MS; i++) {
        base *= 2;
    }
    if (base > S360_BACKOFF_MAX_MS) {
        base = S360_BACKOFF_MAX_MS;
    }

    /* Jitter hacia abajo, hasta un segundo: reparte los reintentos de varios
     * equipos sin alargar la espera por encima del maximo. */
    uint32_t jitter = 0;
    if (base > S360_BACKOFF_JITTER_MS) {
        jitter = (uint32_t)(esp_random() % S360_BACKOFF_JITTER_MS);
    }
    return base - jitter;
}

/* Tarea de reconexion WiFi con espera creciente.
 *
 * Se avisa desde el handler de desconexion y desde el de MQTT: ambos modos
 * de fallo dejan al hub aislado, y el mismo reintento sirve para los dos.
 * Una sola tarea evita que dos avisos simultaneos lancen dos reconexiones. */
static TaskHandle_t s_reconnect_task = NULL;

static void reconnect_task(void *arg)
{
    int intento = 0;

    while (1) {
        /* Espera ilimitada hasta que alguien pida reconectar. Asi la tarea no
         * consume CPU cuando todo va bien. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!wifi_configured) {
            ESP_LOGW(TAG, "Sin credenciales WiFi: no hay nada que reconectar.");
            continue;
        }

        intento++;
        uint32_t espera = backoff_ms(intento);
        ESP_LOGW(TAG, "Reconexion WiFi intento %d en %lu ms", intento,
                 (unsigned long)espera);
        vTaskDelay(pdMS_TO_TICKS(espera));

        /* Si mientras esperabamos llego la conexion, no se reintenta: evita
         * una reconexion innecesaria sobre un enlace ya sano. */
        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Enlace recuperado durante la espera.");
            intento = 0;
            continue;
        }

        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
        }

        /* Se comprueba si el intento sirvio. Si no, el bucle vuelve a
         * notificarse a si mismo para el siguiente intento. */
        vTaskDelay(pdMS_TO_TICKS(8000));
        bits = xEventGroupGetBits(wifi_event_group);
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi reconectado tras %d intento(s).", intento);
            intento = 0;
        } else {
            xTaskNotifyGive(s_reconnect_task);
        }
    }
}

/* Pide una reconexion. Se puede llamar desde cualquier tarea o callback. */
/* true si el hub debe aceptar nodos desconocidos ahora mismo. */
static bool pair_window_open(void)
{
    if (s_pair_window_started == 0) {
        return false;
    }
    uint32_t ahora = (uint32_t)(esp_timer_get_time() / 1000000);
    if ((ahora - s_pair_window_started) >= PAIR_WINDOW_SEC) {
        s_pair_window_started = 0;   /* caduco */
        return false;
    }
    return true;
}

/* Abre la ventana. Reiniciarla estando abierta extiende el plazo, que es lo
 * que se espera si alguien vuelve a pulsar. */
static void pair_window_open_now(const char *origen)
{
    s_pair_window_started = (uint32_t)(esp_timer_get_time() / 1000000);
    ESP_LOGW(TAG, "Ventana de vinculacion ABIERTA %d s (por %s).",
             PAIR_WINDOW_SEC, origen);
}

/* Segundos que le quedan a la ventana, 0 si esta cerrada. */
static uint32_t pair_window_remaining(void)
{
    if (!pair_window_open()) {
        return 0;
    }
    uint32_t ahora = (uint32_t)(esp_timer_get_time() / 1000000);
    return PAIR_WINDOW_SEC - (ahora - s_pair_window_started);
}

/* Tarea que vigila el boton fisico.
 *
 * GPIO 0 es el BOOT de la placa: tiene pull-up, asi que se lee en bajo al
 * pulsar. Se sondea en vez de usar interrupciones porque un pulsador mecanico
 * genera rebotes y aqui no hace falta precision: 50 ms de sondeo sobran. */
static void pair_button_task(void *arg)
{
    gpio_reset_pin(PAIR_BUTTON_PIN);
    gpio_set_direction(PAIR_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PAIR_BUTTON_PIN, GPIO_PULLUP_ONLY);

    int bajo_previo = 0;
    while (1) {
        int bajo = (gpio_get_level(PAIR_BUTTON_PIN) == 0);
        if (bajo && !bajo_previo) {
            pair_window_open_now("boton fisico");
        }
        bajo_previo = bajo;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* Tarea que parpadea el LED mientras la ventana esta abierta.
 *
 * Es la senal visible de que el hub esta escuchando. Sin ella el instalador
 * no tiene forma de saber si pulso bien o si ya caduco. */
static void pair_window_led_task(void *arg)
{
    int estaba_abierta = 0;

    while (1) {
        if (pair_window_open()) {
            estaba_abierta = 1;
            gpio_set_level(INDICATOR_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(LED_EMPAREJA_MS));
            gpio_set_level(INDICATOR_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(LED_EMPAREJA_MS));
        } else {
            /* Al cerrarse la ventana se devuelve el LED a su estado normal.
             *
             * El LED lo comparten la alarma y esta ventana: si la tarea se
             * quedara escribiendolo, borraria el indicador de "armado" que
             * mantiene sm_alarm. Solo se toca mientras la ventana esta
             * abierta, y al cerrar se deja como estaba (apagado). */
            if (estaba_abierta) {
                estaba_abierta = 0;
                gpio_set_level(INDICATOR_LED_PIN,
                               sm_alarm_get_state() == SM_ALARM_STATE_ARMED ? 1 : 0);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

static void reconnect_request(void)
{
    if (s_reconnect_task != NULL) {
        xTaskNotifyGive(s_reconnect_task);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (wifi_configured) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        /* Aqui solo se ponia el bit, y nadie lo leia: el hub se quedaba sin
         * WiFi hasta el siguiente reinicio. */
        ESP_LOGW(TAG, "WiFi desconectado. Se programara la reconexion.");
        reconnect_request();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Obtenida IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        // Iniciar SNTP para tener hora real (solo una vez)
        if (!sntp_synced) {
            init_sntp();
        }
        // Iniciar MQTT estrictamente cuando ya tenemos IP y no está conectado
        if (!mqtt_is_connected && mqtt_client == NULL) {
            start_mqtt_client(MQTT_BROKER_URI);
        }
    }
}

// ============================================================================
// HANDLERS DEL PORTAL CAUTIVO V2
// ============================================================================

static esp_err_t provisioning_handler(httpd_req_t *req) {
    last_portal_activity = esp_timer_get_time();
    
    const char *error_banner = (current_portal_state == PORTAL_STATE_FAILED) ? 
        "<div style='background:#f8d7da;color:#721c24;padding:10px;border-radius:4px;"
        "margin-bottom:15px;border:1px solid #f5c6cb;'>⚠️ Contraseña incorrecta o red no "
        "encontrada. Intente de nuevo.</div>" : "";

    char html[4096];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Seguimiento 360 - Config</title>"
        "<style>"
        "body{font-family:system-ui,sans-serif;padding:20px;max-width:420px;margin:0 auto;"
        "background:#f5f7fa;color:#333;}"
        "h2{color:#004085;margin-bottom:20px;text-align:center;}"
        "label{display:block;margin-top:12px;font-weight:600;font-size:14px;color:#495057;}"
        ".input-group{position:relative;margin-bottom:4px;}"
        "input[type='text'],input[type='password']{width:100%%;padding:10px 40px 10px 10px;"
        "border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;font-size:14px;}"
        "input:focus{outline:none;border-color:#007bff;box-shadow:0 0 0 3px rgba(0,123,255,0.15);}"
        ".toggle-pass{position:absolute;right:10px;top:50%%;transform:translateY(-50%%);"
        "background:none;border:none;cursor:pointer;font-size:18px;color:#6c757d;padding:0;}"
        ".toggle-pass:hover{color:#007bff;}"
        ".help{font-size:12px;color:#6c757d;margin:4px 0 8px 0;line-height:1.4;}"
        ".help code{background:#e9ecef;padding:1px 5px;border-radius:3px;font-size:11px;}"
        "button[type='submit']{width:100%%;padding:12px;background:#007bff;color:white;"
        "border:none;border-radius:6px;font-size:16px;font-weight:600;cursor:pointer;margin-top:18px;}"
        "button[type='submit']:hover{background:#0056b3;}"
        "hr{margin:20px 0;border:0;border-top:1px solid #dee2e6;}"
        ".section-title{font-size:13px;color:#007bff;text-transform:uppercase;"
        "letter-spacing:0.5px;margin:15px 0 5px 0;font-weight:600;}"
        "</style></head><body>"
        "<h2>📡 Seguimiento 360</h2>%s"
        "<form action='/save' method='POST'>"
        "<div class='section-title'>🌐 Conexión Wi-Fi</div>"
        "<label>Red Wi-Fi (SSID):</label>"
        "<input type='text' name='ssid' value='%s' required placeholder='Ej: MiRedWiFi'>"
        "<label>Contraseña Wi-Fi:</label>"
        "<div class='input-group'>"
        "<input type='password' id='password' name='password' value='%s' "
        "required placeholder='Contraseña'>"
        "<button type='button' class='toggle-pass' onclick='togglePassword()' "
        "id='toggleBtn'>👁️</button>"
        "</div>"
        "<hr>"
        "<div class='section-title'>🏢 Identificación del HUB</div>"
        "<label>Tenant ID:</label>"
        "<input type='text' name='tenant_id' value='%s' placeholder='0' maxlength='15'>"
        "<div class='help'>💡 ID del cliente/empresa. Usa <code>0</code> para uso personal. "
        "Ej: <code>abc</code>, <code>casa</code>.</div>"
        "<label>Site ID:</label>"
        "<input type='text' name='site_id' value='%s' placeholder='site0' maxlength='31'>"
        "<div class='help'>💡 ID de ubicación/sucursal. Ej: <code>bodega_central</code>, "
        "<code>garage</code>.</div>"
        "<button type='submit'>✅ Validar y Guardar</button>"
        "<label>Codigo de vinculacion (claim code)</label>"
        "<input type='text' name='claim_code' id='claim_code' maxlength='11' "
        "placeholder='XX-YYYYYY-Z' value='%s' "
        "style='text-transform:uppercase'>"
        "<p style='font-size:13px;color:#6c757d;margin-top:6px'>"
        "Opcional. Es el codigo que te dio el administrador: con el, el hub "
        "queda vinculado a tu cuenta sin escribir identificadores internos. "
        "Si lo dejas vacio, usa el tenant y site de abajo.</p>"
        "</form>"
        "<script>"
        "function togglePassword(){"
        "var p=document.getElementById('password');"
        "var b=document.getElementById('toggleBtn');"
        "if(p.type==='password'){p.type='text';b.textContent='🙈';}"
        "else{p.type='password';b.textContent='👁️';}"
        "}"
        "</script></body></html>",
        error_banner,
        active_config.ssid, active_config.password,
        active_config.tenant_id, active_config.site_id,
        active_config.claim_code
    );

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html, strlen(html));
}

/* Valida un claim code con el mismo algoritmo que el backend (HUB-F-016).
 *
 * Formato XX-YYYYYY-Z, charset 23456789ABCDEFGHJKMNPQRSTUVWXYZ (sin vocales
 * ni caracteres que se confunden al leerlos: 0/O, 1/I/L) y un caracter de
 * control que es la suma de los anteriores modulo la longitud del charset.
 *
 * Se valida aqui para que un codigo mal tecleado se detecte antes de
 * enviarlo: si se aceptara sin comprobarlo, el error solo apareceria al
 * canjearlo en la aplicacion, y el instalador ya no estaria delante. */
static const char CLAIM_CHARSET[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";

static bool claim_code_valido(const char *codigo)
{
    if (codigo == NULL || strlen(codigo) != 11) {
        return false;
    }
    if (codigo[2] != '-' || codigo[9] != '-') {
        return false;
    }

    /* La suma es sobre los OCHO caracteres utiles: prefijo (2) + aleatorio
     * (6). No entran ni los guiones (posiciones 2 y 9) ni el propio caracter
     * de control (posicion 10).
     *
     * Incluir el control en la suma fue el primer intento, y daba 622 en vez
     * de 545: rechazaba todos los codigos validos. El backend calcula
     * `base = prefix + random` y suma solo eso. */
    int suma = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 2 || i == 9) {
            continue;   /* guiones */
        }
        char c = codigo[i];
        if (strchr(CLAIM_CHARSET, c) == NULL) {
            return false;
        }
        suma += (unsigned char)c;
    }

    char esperado = CLAIM_CHARSET[suma % (int)(sizeof(CLAIM_CHARSET) - 1)];
    return codigo[10] == esperado;
}

static esp_err_t save_config_handler(httpd_req_t *req) {
    last_portal_activity = esp_timer_get_time();
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char temp_ssid[33] = {0}, temp_pass[65] = {0};
    char temp_tenant[16] = "0", temp_site[32] = "site0";
    char temp_claim[16] = {0};

    get_form_param(buf, "ssid", temp_ssid, sizeof(temp_ssid));
    get_form_param(buf, "password", temp_pass, sizeof(temp_pass));
    get_form_param(buf, "tenant_id", temp_tenant, sizeof(temp_tenant));
    get_form_param(buf, "site_id", temp_site, sizeof(temp_site));
    get_form_param(buf, "claim_code", temp_claim, sizeof(temp_claim));

    if (strlen(temp_ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID requerido");
        return ESP_FAIL;
    }

    /* El claim code es opcional, pero si viene tiene que ser valido. Se
     * comprueba aqui y no en la aplicacion: un codigo mal tecleado se
     * detecta mientras el instalador sigue delante del equipo. */
    if (strlen(temp_claim) > 0) {
        for (char *c = temp_claim; *c; c++) {
            *c = toupper((unsigned char)*c);
        }
        if (!claim_code_valido(temp_claim)) {
            ESP_LOGW(TAG, "Claim code con formato invalido: %s", temp_claim);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Claim code invalido. Formato XX-YYYYYY-Z");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Claim code valido: %s", temp_claim);
    }

    // 1. Guardar configuración inmediatamente en NVS
    active_config.version = 3;
    strncpy(active_config.ssid, temp_ssid, sizeof(active_config.ssid) - 1);
    active_config.ssid[sizeof(active_config.ssid) - 1] = '\0';
    strncpy(active_config.password, temp_pass, sizeof(active_config.password) - 1);
    active_config.password[sizeof(active_config.password) - 1] = '\0';
    strncpy(active_config.tenant_id, temp_tenant, sizeof(active_config.tenant_id) - 1);
    active_config.tenant_id[sizeof(active_config.tenant_id) - 1] = '\0';
    strncpy(active_config.site_id, temp_site, sizeof(active_config.site_id) - 1);
    active_config.site_id[sizeof(active_config.site_id) - 1] = '\0';
    strncpy(active_config.claim_code, temp_claim, sizeof(active_config.claim_code) - 1);
    active_config.claim_code[sizeof(active_config.claim_code) - 1] = '\0';

    save_config();
    wifi_configured = true;

    // 2. Enviar respuesta HTML inmediata con auto-refresco
    const char *html_template = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='8;url=/status'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Conectando...</title>"
        "<style>body{font-family:system-ui,sans-serif;padding:40px;text-align:center;"
        "background:#f5f7fa;}"
        ".spinner{border:4px solid #f3f3f3;border-top:4px solid #007bff;border-radius:50%%;"
        "width:40px;height:40px;animation:spin 1s linear infinite;margin:0 auto 20px;}"
        "@keyframes spin{0%%{transform:rotate(0deg);}100%%{transform:rotate(360deg);}}"
        "</style></head>"
        "<body><div class='spinner'></div>"
        "<h2>✅ Configuración Guardada</h2>"
        "<p>El Hub se está conectando a: <b>%s</b></p>"
        "<p>Espera 8 segundos, serás redirigido automáticamente...</p>"
        "<p><a href='/status'>Ir al estado ahora →</a></p>"
        "</body></html>";
    
    char resp_html[1024];
    snprintf(resp_html, sizeof(resp_html), html_template, temp_ssid);
    
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, resp_html, strlen(resp_html));

    // 3. Espera crítica para que el HTTP termine de enviar antes de tocar el Wi-Fi
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 4. Reconfigurar y conectar al nuevo Wi-Fi
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    wifi_config_t wifi_cfg = {0};
    strncpy((char*)wifi_cfg.sta.ssid, temp_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char*)wifi_cfg.sta.password, temp_pass, sizeof(wifi_cfg.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_wifi_connect();
    
    return ESP_OK;
}



static esp_err_t factory_reset_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Solicitado reinicio de fábrica. Borrando NVS...");
    nvs_erase_all(nvs_handle_pairing);
    nvs_commit(nvs_handle_pairing);
    
    const char *html = "<!DOCTYPE html><html><head><meta charset='UTF-8'></head>"
                       "<body style='font-family:sans-serif;padding:20px;text-align:center;'>"
                       "<h3>🔄 Reiniciando...</h3>"
                       "<p>El portal estará disponible en 10 segundos "
                       "como 'Seguimiento360_HUB'.</p>"
                       "</body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, strlen(html));
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// ============================================================================
// API REST PARA GESTIÓN DE NODOS (Desvinculación)
// ============================================================================

// GET /api/nodes - Lista todos los nodos en formato JSON



// ============================================================================
// API REST PARA CONFIGURACIÓN Y ESTADO DEL HUB
// ============================================================================









static esp_err_t api_nodes_list_handler(httpd_req_t *req) {
    last_portal_activity = esp_timer_get_time();
    char json[2048];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset, 
        "{\"count\":%d,\"max\":%d,\"nodes\":[", 
        node_registry.count, MAX_NODES);
    
    for (int i = 0; i < node_registry.count; i++) {
        char mac_display[18];
        format_mac(node_registry.nodes[i].mac, mac_display, sizeof(mac_display));
        if (i > 0) offset += snprintf(json + offset, sizeof(json) - offset, ",");
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"index\":%d,\"mac\":\"%s\",\"type\":\"%s\"}",
            i, mac_display, node_registry.nodes[i].type);
    }
    offset += snprintf(json + offset, sizeof(json) - offset, "]}");
    
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, json, offset);
}

// DELETE /api/nodes?mac=XX:XX:XX:XX:XX:XX - Desvincular un nodo
static esp_err_t api_nodes_delete_handler(httpd_req_t *req) {
    last_portal_activity = esp_timer_get_time();
    char mac_query[32] = {0};
    size_t query_len = httpd_req_get_url_query_len(req);
    
    if (query_len > 0) {
        char query[64];
        httpd_req_get_url_query_str(req, query, sizeof(query));
        get_form_param(query, "mac", mac_query, sizeof(mac_query));
    }
    
    if (strlen(mac_query) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MAC requerida");
        return ESP_FAIL;
    }
    
    // Parsear MAC de string a bytes
    uint8_t target_mac[6];
    int values[6];
    if (sscanf(mac_query, "%x:%x:%x:%x:%x:%x", 
               &values[0], &values[1], &values[2], 
               &values[3], &values[4], &values[5]) != 6) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MAC inválida");
        return ESP_FAIL;
    }
    for (int i = 0; i < 6; i++) target_mac[i] = (uint8_t)values[i];
    
    // Buscar el nodo en el registro
    int found_idx = -1;
    for (int i = 0; i < node_registry.count; i++) {
        if (memcmp(node_registry.nodes[i].mac, target_mac, 6) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Nodo no encontrado");
        return ESP_FAIL;
    }
    
    // Eliminar de ESP-NOW
    esp_now_del_peer(target_mac);
    char mac_display[18];
    format_mac(target_mac, mac_display, sizeof(mac_display));
    ESP_LOGI(TAG, "🗑️ Nodo desvinculado: %s", mac_display);
    
    // Desplazar el array para llenar el hueco
    for (int i = found_idx; i < node_registry.count - 1; i++) {
        node_registry.nodes[i] = node_registry.nodes[i + 1];
    }
    node_registry.count--;
    save_node_registry();
    
    // Respuesta JSON de éxito
    char json[128];
    snprintf(json, sizeof(json), 
        "{\"success\":true,\"message\":\"Nodo %s desvinculado\",\"new_count\":%d}",
        mac_display, node_registry.count);
    
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, json, strlen(json));
}

// ============================================================================
// TAREA DE VIGILANCIA DEL PORTAL
// ============================================================================

/* ============================================================================
 * VIGILANCIA DE NODOS (HUB-F-008, AT-004)
 *
 * Publica un evento cuando un nodo lleva NODE_LOST_MISSED heartbeats sin
 * dar señales. Tras avisar, pone last_ping a 0 para no repetir el aviso en
 * cada ciclo; vuelve a contar cuando el nodo se manifieste de nuevo.
 * ========================================================================== */
static void publish_node_lost(const uint8_t *mac, const char *name)
{
    if (mqtt_client == NULL || !mqtt_is_connected) {
        return;
    }

    char mac_str[18];
    format_mac(mac, mac_str, sizeof(mac_str));

    char topic[128];
    get_mqtt_topic(topic, sizeof(topic), "event");

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{"
             "\"mac\":\"%s\","
             "\"node_name\":\"%s\","
             "\"event\":\"NODE_LOST\","
             "\"tenant\":\"%s\","
             "\"site\":\"%s\""
             "}",
             mac_str, name,
             active_config.tenant_id, active_config.site_id);

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
    if (msg_id > 0) {
        ESP_LOGW(TAG, "Nodo perdido: %s (%s). Evento publicado.", name, mac_str);
    } else {
        ESP_LOGE(TAG, "No se pudo publicar NODE_LOST de %s", mac_str);
    }
}

/* Callback que sm_alarm usa para publicar. Se mantiene por si en el futuro
 * se integra sm_alarm_process_espnow_data; hoy la ruta de eventos sigue en
 * publish_event_to_mqtt, que es el formato que Node-RED ya consume. */
static void sm_alarm_publish(const char *json_payload)
{
    if (mqtt_client == NULL || !mqtt_is_connected || json_payload == NULL) {
        return;
    }
    char topic[128];
    get_mqtt_topic(topic, sizeof(topic), "event");
    esp_mqtt_client_publish(mqtt_client, topic, json_payload, 0, 1, 0);
}

static void node_watchdog_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Vigilancia de nodos activa: umbral %d s (%d heartbeats de %d s)",
             NODE_LOST_TIMEOUT_S, NODE_LOST_MISSED, NODE_HEARTBEAT_SEC);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(NODE_WATCHDOG_PERIOD_S * 1000));

        uint32_t now = get_unix_now();
        if (now < 1600000000U) {
            /* Sin hora real todavia (SNTP no sincronizado): no se puede
             * comparar contra last_ping, que es un instante absoluto. */
            continue;
        }

        for (int i = 0; i < node_registry.count; i++) {
            uint32_t last = node_registry.nodes[i].last_ping;
            if (last == 0) {
                continue;   /* ya avisado, o nunca visto */
            }
            if ((now - last) > (uint32_t)NODE_LOST_TIMEOUT_S) {
                publish_node_lost(node_registry.nodes[i].mac,
                                  node_registry.nodes[i].name);
                node_registry.nodes[i].last_ping = 0;
                save_node_registry();
            }
        }
    }
}

static void portal_inactivity_task(void *pvParameters) {
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (current_portal_state == PORTAL_STATE_CONNECTED && server != NULL) {
            uint64_t now = esp_timer_get_time();
            if ((now - last_portal_activity) > (PORTAL_INACTIVITY_TIMEOUT_MS * 1000ULL)) {
                ESP_LOGI(TAG, "Inactividad detectada. Deteniendo portal cautivo.");
                httpd_stop(server);
                server = NULL;
                vTaskDelete(NULL);
            }
        }
    }
}

// ============================================================================
// INICIAR PORTAL CAUTIVO
// ============================================================================


// Handler específico para Android (espera estrictamente 204 No Content)
static esp_err_t captive_portal_android_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Handler específico para iOS / Otros (acepta redirección)
static esp_err_t captive_portal_apple_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t start_captive_portal(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;  // 16 KB para evitar stack overflow con HTML grande
    config.max_uri_handlers = 12;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_config[] = {
            {.uri = "/", .method = HTTP_GET, .handler = provisioning_handler, .user_ctx = NULL},
            {.uri = "/save", .method = HTTP_POST, .handler = save_config_handler, .user_ctx = NULL},
            {.uri = "/factory_reset", .method = HTTP_GET, .handler = factory_reset_handler, .user_ctx = NULL},
            {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler, .user_ctx = NULL},
            // API REST para gestión de nodos
            {.uri = "/api/nodes", .method = HTTP_GET, .handler = api_nodes_list_handler, .user_ctx = NULL},
            {.uri = "/api/nodes", .method = HTTP_DELETE, .handler = api_nodes_delete_handler, .user_ctx = NULL},
            // URLs de prueba para forzar el pop-up del portal cautivo (Estándar CNA)
            {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_android_handler, .user_ctx = NULL},
            {.uri = "/gen_204", .method = HTTP_GET, .handler = captive_portal_android_handler, .user_ctx = NULL},
            {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_apple_handler, .user_ctx = NULL},
            {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_portal_apple_handler, .user_ctx = NULL},
            {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_portal_apple_handler, .user_ctx = NULL}
        };
        
        for (int i = 0; i < 7; i++) {
            httpd_register_uri_handler(server, &uri_config[i]);



        }
        
        last_portal_activity = esp_timer_get_time();
        xTaskCreate(&portal_inactivity_task, "portal_watchdog", 2048, NULL, 1, NULL);
        xTaskCreate(&node_watchdog_task, "node_watchdog", 4096, NULL, 2, NULL);
        ESP_LOGI(TAG, "Portal Cautivo v2 iniciado en http://192.168.4.1");
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ============================================================================
// APP MAIN
// ============================================================================
void app_main(void) {
    // 1. Inicialización NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Particiones propias de nvs_manager (nvs_state, nvs_nodes, config).
     * Sin esto, nvs_manager_set_alarm_state() falla con
     * ESP_ERR_NVS_PART_NOT_FOUND: la particion existe en partitions.csv pero
     * nadie la habia inicializado. */
    esp_err_t nvs_err = nvs_manager_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_manager_init fallo: %s. El estado de alarma no se "
                      "persistira entre reinicios.", esp_err_to_name(nvs_err));
    } else {
        ESP_LOGI(TAG, "Particiones NVS propias inicializadas");
    }

    // 2. Cargar configuración (con migración automática si es legacy)
    load_config();

    // 3. Obtener MAC del HUB
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memcpy(hub_mac, mac, sizeof(hub_mac));
    /* El SoftAP lleva la STA + 1 en el ultimo octeto. Se guarda para que el
     * filtro de destinatario reconozca los PAIR que el nodo dirige a la AP. */
    esp_read_mac(hub_ap_mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(hub_mac_str, sizeof(hub_mac_str), "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "MAC STA %02x:%02x:%02x:%02x:%02x:%02x | AP %02x:%02x:%02x:%02x:%02x:%02x",
             hub_mac[0], hub_mac[1], hub_mac[2], hub_mac[3], hub_mac[4], hub_mac[5],
             hub_ap_mac[0], hub_ap_mac[1], hub_ap_mac[2], hub_ap_mac[3], hub_ap_mac[4], hub_ap_mac[5]);

    // 4. Inicializar Event Group
    wifi_event_group = xEventGroupCreate();

    /* Tarea de reconexion (HUB-NF-005). Se crea antes de arrancar WiFi para
     * que el primer evento de desconexion ya encuentre quien lo atienda. */
    if (xTaskCreate(reconnect_task, "wifi_reconn", 4096, NULL, 4,
                    &s_reconnect_task) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea de reconexion.");
        s_reconnect_task = NULL;
    }

    /* Ventana de vinculacion: el boton fisico y el parpadeo de aviso.
     *
     * Se crean despues de configurar el LED (gpio_reset_pin mas abajo), asi
     * que la tarea del LED espera a que el pin este listo. El boton no
     * depende de nada. */
    xTaskCreate(pair_button_task, "pair_button", 3072, NULL, 3, NULL);

    // 5. Configurar GPIO 48 (LED RGB) como salida para indicador
    gpio_reset_pin(INDICATOR_LED_PIN);
    gpio_set_direction(INDICATOR_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(INDICATOR_LED_PIN, 0);

    /* El parpadeo de la ventana empieza cuando el LED ya esta configurado. */
    xTaskCreate(pair_window_led_task, "pair_led", 2560, NULL, 2, NULL);

    /* FSM de alarma: restaura el estado guardado en NVS y deja el LED
     * reflejandolo, para que sobreviva a un reinicio (AT-006). */
    if (sm_alarm_init(sm_alarm_publish) == ESP_OK) {
        gpio_set_level(INDICATOR_LED_PIN,
                       sm_alarm_get_state() == SM_ALARM_STATE_ARMED ? 1 : 0);
        ESP_LOGI(TAG, "Alarma: %s | LED %s",
                 sm_alarm_state_str(sm_alarm_get_state()),
                 sm_alarm_get_state() == SM_ALARM_STATE_ARMED ? "ON" : "OFF");
    } else {
        ESP_LOGE(TAG, "No se pudo inicializar la FSM de alarma");
    }

    // 6. Inicializar Red y Wi-Fi (APSTA)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_ps(WIFI_PS_NONE);  // Anti-brownout

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                         &wifi_event_handler, NULL, NULL));

    // 7. Configurar AP para Portal Cautivo.
    //    SSID y contrasena derivan de la MAC: con varios hubs en la misma
    //    casa cada uno emite su propia red y el instalador puede elegir.
    //    Formato: S360-XXXX, con los ultimos 4 hex de la MAC en mayusculas.
    char ap_ssid[16];
    char ap_pass[16];
    snprintf(ap_ssid, sizeof(ap_ssid), "S360-%c%c%c%c",
             toupper((unsigned char)hub_mac_str[8]),
             toupper((unsigned char)hub_mac_str[9]),
             toupper((unsigned char)hub_mac_str[10]),
             toupper((unsigned char)hub_mac_str[11]));
    snprintf(ap_pass, sizeof(ap_pass), "S360-%c%c%c%c",
             toupper((unsigned char)hub_mac_str[8]),
             toupper((unsigned char)hub_mac_str[9]),
             toupper((unsigned char)hub_mac_str[10]),
             toupper((unsigned char)hub_mac_str[11]));

    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    strncpy((char *)ap_config.ap.password, ap_pass, sizeof(ap_config.ap.password) - 1);

    /* El SoftAP solo se levanta si al hub le falta configuracion.
     *
     * Antes arrancaba siempre en APSTA, y su AP quedaba abierto incluso en un
     * hub ya configurado y funcionando. Cualquiera dentro del alcance podia
     * conectarse y reconfigurarlo. Un equipo que ya tiene credenciales no
     * necesita portal: si hay que reconfigurarlo, se borra con el boton o con
     * /factory_reset estando cerca. */
    bool necesita_portal = !(wifi_configured && strlen(active_config.ssid) > 0);
    if (necesita_portal) {
        ESP_LOGW(TAG, "Sin configuracion: SoftAP de provisioning activo.");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    } else {
        ESP_LOGI(TAG, "Hub configurado: modo STA, sin SoftAP de provisioning.");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
    s_portal_habilitado = necesita_portal;

    // 8. Conectar STA si hay credenciales guardadas
    if (wifi_configured && strlen(active_config.ssid) > 0) {
        wifi_config_t sta_config = {0};
        strncpy((char*)sta_config.sta.ssid, active_config.ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, active_config.password, sizeof(sta_config.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        current_portal_state = PORTAL_STATE_CONNECTED;
    } else {
        ESP_ERROR_CHECK(esp_wifi_start());
        current_portal_state = PORTAL_STATE_CONFIGURING;
    }

    // 9. Inicializar ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    /* Para saber si el nodo siquiera entiendo el nivel de enlace. */
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_espnow_sent));

    const esp_timer_create_args_t ping_timer_args = {
        .callback = &ping_timer_cb,
        .name     = "ping_to"
    };
    ESP_ERROR_CHECK(esp_timer_create(&ping_timer_args, &s_ping_timer));

    /* Latido de estado: mismo sitio donde se arma el timer del ping. */
    const esp_timer_create_args_t latido_args = {
        .callback = &hub_latido_cb,
        .name     = "hub_latido"
    };
    ESP_ERROR_CHECK(esp_timer_create(&latido_args, &s_latido_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_latido_timer, HUB_LATIDO_US));

    // 10. Cargar registro de nodos y restaurar peers en ESP-NOW
    load_node_registry();
    for (int i = 0; i < node_registry.count; i++) {
        esp_now_peer_info_t peer = { 
            .channel = router_channel, 
            .ifidx = ESP_IF_WIFI_STA, 
            .encrypt = false 
        };
        memcpy(peer.peer_addr, node_registry.nodes[i].mac, 6);
        if (esp_now_add_peer(&peer) == ESP_OK) {
            char mac_display[18];
            format_mac(node_registry.nodes[i].mac, mac_display, sizeof(mac_display));
            ESP_LOGI(TAG, "Peer restaurado: %s (%s)", mac_display, node_registry.nodes[i].type);
        }
    }
    ESP_LOGI(TAG, "Se restauraron %d nodos en la lista de peers de ESP-NOW.", 
             node_registry.count);

    // 11. MQTT se iniciará automáticamente en el evento IP_EVENT_STA_GOT_IP (cuando ya tengamos IP)

    // 12. Portal Cautivo, solo si falta configuracion
    if (s_portal_habilitado) {
        start_captive_portal();
    } else {
        ESP_LOGI(TAG, "Portal no iniciado: el hub ya esta configurado.");
    }
    
    // Feedback visual de inicio exitoso (2 parpadeos)
    blink_indicator(2, 200);
    
    ESP_LOGI(TAG, "============================================================");
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "   Seguimiento 360 HUB %s - PRODUCCIÓN", app_desc->version);
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "MAC: %s", hub_mac_str);
    ESP_LOGI(TAG, "Tenant: %s | Site: %s", active_config.tenant_id, active_config.site_id);
    ESP_LOGI(TAG, "Nodos vinculados: %d/%d", node_registry.count, MAX_NODES);
    ESP_LOGI(TAG, "Broker MQTT: %s", MQTT_BROKER_URI);
    if (s_portal_habilitado) {
        ESP_LOGI(TAG, "Portal Cautivo: http://192.168.4.1 (timeout: 10 min)");
        ESP_LOGI(TAG, "  Red WiFi: %s  |  Clave: %s", ap_ssid, ap_pass);
    } else {
        ESP_LOGI(TAG, "Sin portal de provisioning (hub ya configurado).");
    }
    ESP_LOGI(TAG, "============================================================");
}
