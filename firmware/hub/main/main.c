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
#include "espnow_protocol.h"
#include "esp_app_desc.h"
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
    uint8_t distance_cm;      // Distancia medida (0 si no aplica)
} __attribute__((packed)) security_payload_t;

// Verificación en tiempo de compilación
_Static_assert(sizeof(security_payload_t) == 7, 
               "¡ERROR: El tamaño del payload no coincide con el nodo (debe ser 7 bytes)!");

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
static nvs_handle_t nvs_handle_pairing;
static bool wifi_configured = false;
static httpd_handle_t server = NULL;
/* Canal compartido con los nodos. Debe coincidir con WIFI_CHANNEL del
 * firmware del nodo y con el canal en que queda el AP del hub.
 * Estaba a 0 y nunca se asignaba: los peers se registraban en el canal 0
 * mientras los nodos transmiten en el 5, asi que no se escuchaban. */
#define S360_ESPNOW_CHANNEL  5
static uint8_t router_channel = S360_ESPNOW_CHANNEL;
static char hub_mac_str[18];
static uint8_t hub_mac[6];   /* MAC STA; el frame v1 la lleva como src_mac */

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
static void publish_event_to_mqtt(const uint8_t *sender_mac, security_payload_t *payload);
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
    
    char online_payload[256];
    snprintf(online_payload, sizeof(online_payload), 
        "{\"event\":\"HUB_ONLINE\",\"mac\":\"%s\",\"hub_name\":\"%s\"}", 
        mac_str, active_config.hub_name);
    
    esp_mqtt_client_publish(mqtt_client, "seguimiento360/hub/status", online_payload, 0, 1, 0);
    ESP_LOGI(TAG, "📡 Publicado HUB_ONLINE al servidor central");

            mqtt_is_connected = true;
        
        // --- NUEVO: Suscribirse a comandos OTA ---
        char ota_cmd_topic[128];
        get_mqtt_topic(ota_cmd_topic, sizeof(ota_cmd_topic), "ota_cmd");
        esp_mqtt_client_subscribe(mqtt_client, ota_cmd_topic, 1);
        ESP_LOGI(TAG, "Suscrito a comandos OTA: %s", ota_cmd_topic);

        // Suscribirse a configuración de nodos
        char node_cfg_topic[128];
        get_mqtt_topic(node_cfg_topic, sizeof(node_cfg_topic), "node_cfg");
        esp_mqtt_client_subscribe(mqtt_client, node_cfg_topic, 1);
        ESP_LOGI(TAG, "📡 Suscrito a configuración de nodos: %s", node_cfg_topic);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "❌ MQTT_EVENT_DISCONNECTED");
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

                        ESP_LOGI(TAG, "🔗 URL limpia para OTA: '%s' (longitud: %d)", url_buffer, clean_len);
                        ESP_LOG_BUFFER_HEX("OTA_URL_HEX", url_buffer, clean_len);
                        
                        esp_err_t ret = ota_manager_start(url_buffer);
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "Error al iniciar tarea OTA: %s", esp_err_to_name(ret));
                        }
                        
                    }
                }
            }
            // --- MANEJO DE CONFIGURACIÓN DE NODOS ---
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

static void publish_event_to_mqtt(const uint8_t *sender_mac, security_payload_t *payload) {
    if (mqtt_client == NULL || !mqtt_is_connected) {
        ESP_LOGW(TAG, "MQTT no está conectado. Evento descartado.");
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
    uint8_t distance = payload->distance_cm;

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
        "\"distance_cm\":%d,"
        "\"tenant\":\"%s\","
        "\"site\":\"%s\""
        "}",
        mac_str, node_name, location, room, event_str, 
        payload->seq, payload->battery_level, distance,
        active_config.tenant_id, active_config.site_id
    );

    ESP_LOGI(TAG, "📤 Publicando en MQTT -> Topic: %s", mqtt_topic);
    ESP_LOGI(TAG, "📦 Payload: %s", json_payload);

    // Publicar con QoS 1 (al menos una vez)
    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic, json_payload, 0, 1, 0);
    if (msg_id > 0) {
        ESP_LOGI(TAG, "✅ Evento publicado exitosamente (msg_id=%d)", msg_id);
    } else {
        ESP_LOGE(TAG, "❌ Error al publicar evento MQTT");
    }
}

// ============================================================================
// ESP-NOW: RECEPCIÓN DE DATOS Y VINCULACIÓN
// ============================================================================

/* Responde al nodo con un frame v1 de tipo ACK.
 * El ACK se envia antes de procesar el evento para cumplir el presupuesto
 * de 50 ms (AT-02). */
static void send_ack_v1(const uint8_t *dst_mac, uint16_t seq) {
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
    }
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
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

    // --- LÓGICA DE VINCULACIÓN (PAIRING) ---
    if (memcmp(payload->event_code, "PAIR", 4) == 0) {
        ESP_LOGI(TAG, "Solicitud de PAIRING recibida de: %s", mac_str);

        if (is_mac_registered(sender_mac)) {
            ESP_LOGI(TAG, "El nodo %s ya está vinculado. Enviando ACK.", mac_str);
            send_ack_v1(sender_mac, payload->seq);
            return;
        }

        if (node_registry.count >= MAX_NODES) {
            ESP_LOGW(TAG, "Límite de nodos alcanzado (%d). Pairing rechazado.", MAX_NODES);
            blink_indicator(5, 300);
            return;
        }

        esp_now_peer_info_t new_peer = { 
            .channel = router_channel, 
            .ifidx = ESP_IF_WIFI_AP, 
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

            send_ack_v1(sender_mac, payload->seq);
        }
        return;
    }

    // --- LÓGICA DE EVENTOS NORMALES (ALRM, HEAR, etc.) ---
    int node_idx = find_node_index(sender_mac);
    if (node_idx >= 0) {
        // Actualizar last_ping y guardar en NVS
        node_registry.nodes[node_idx].last_ping = get_unix_now();
        save_node_registry();
        publish_event_to_mqtt(sender_mac, payload);
    } else {
        ESP_LOGW(TAG, "Paquete ignorado: MAC %s no registrada.", mac_str);
    }
}

// ============================================================================
// WIFI EVENT HANDLER
// ============================================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (wifi_configured) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
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
        active_config.tenant_id, active_config.site_id
    );

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html, strlen(html));
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

    get_form_param(buf, "ssid", temp_ssid, sizeof(temp_ssid));
    get_form_param(buf, "password", temp_pass, sizeof(temp_pass));
    get_form_param(buf, "tenant_id", temp_tenant, sizeof(temp_tenant));
    get_form_param(buf, "site_id", temp_site, sizeof(temp_site));

    if (strlen(temp_ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID requerido");
        return ESP_FAIL;
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

    // 2. Cargar configuración (con migración automática si es legacy)
    load_config();

    // 3. Obtener MAC del HUB
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memcpy(hub_mac, mac, sizeof(hub_mac));
    snprintf(hub_mac_str, sizeof(hub_mac_str), "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 4. Inicializar Event Group
    wifi_event_group = xEventGroupCreate();

    // 5. Configurar GPIO 48 (LED RGB) como salida para indicador
    gpio_reset_pin(INDICATOR_LED_PIN);
    gpio_set_direction(INDICATOR_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(INDICATOR_LED_PIN, 0);

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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    
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

    // 10. Cargar registro de nodos y restaurar peers en ESP-NOW
    load_node_registry();
    for (int i = 0; i < node_registry.count; i++) {
        esp_now_peer_info_t peer = { 
            .channel = router_channel, 
            .ifidx = ESP_IF_WIFI_AP, 
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

    // 12. Iniciar Portal Cautivo
    start_captive_portal();
    
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
    ESP_LOGI(TAG, "Portal Cautivo: http://192.168.4.1 (timeout: 10 min)");
    ESP_LOGI(TAG, "  Red WiFi: %s  |  Clave: %s", ap_ssid, ap_pass);
    ESP_LOGI(TAG, "============================================================");
}
