#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mqtt_client.h"

#define TAG "HUB_GATEWAY"
#define WIFI_SSID_AP "Seguimiento360_HUB"
#define WIFI_PASS_AP "hub_password_2026"
#define MAX_PEERS 20
#define MQTT_BROKER_URI "wss://mqtt.seguimiento360.com"

typedef struct { 
    uint8_t event_code[4]; 
    uint8_t seq; 
    uint8_t battery_level; 
    uint8_t reserved; 
} security_payload_t;

typedef struct { 
    char ssid[33]; 
    char password[65]; 
} wifi_credentials_t;

// ==========================================
// VARIABLES GLOBALES
// ==========================================
static esp_now_peer_info_t peer_list[MAX_PEERS];
static int peer_count = 0;
static nvs_handle_t nvs_handle_pairing;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static char hub_mac_str[18];
static bool wifi_configured = false;
static httpd_handle_t server = NULL;
static uint8_t router_channel = 1;

// ==========================================
// HTML PORTAL
// ==========================================
static const char *portal_html = 
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Configuracion HUB</title>"
    "<style>body{font-family:Arial,sans-serif;max-width:450px;margin:40px auto;padding:20px;background:#f0f2f5;}"
    ".container{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.1);}"
    "h1{color:#2c3e50;text-align:center;font-size:22px;}.form-group{margin-bottom:20px;}"
    "label{display:block;margin-bottom:8px;font-weight:bold;color:#34495e;}"
    "input[type='text'],input[type='password']{width:100%;padding:12px;border:1px solid #ddd;border-radius:6px;font-size:16px;box-sizing:border-box;}"
    "button{width:100%;padding:15px;background:#27ae60;color:white;border:none;border-radius:6px;font-size:18px;font-weight:bold;cursor:pointer;margin-top:10px;}"
    "button:hover{background:#219150;}.toggle-pwd{font-weight:normal;font-size:14px;color:#7f8c8d;margin-top:5px;display:flex;align-items:center;}"
    ".toggle-pwd input{margin-right:8px;}</style></head><body>"
    "<div class='container'><h1>Configuracion HUB</h1>"
    "<p style='text-align:center;color:#7f8c8d;margin-bottom:25px;'>Seguimiento 360 - Sistema de Seguridad</p>"
    "<form action='/save_wifi' method='GET'>"
    "<div class='form-group'><label for='ssid'>Nombre de la Red Wi-Fi (SSID):</label>"
    "<input type='text' id='ssid' name='ssid' required placeholder='Ej: MiCasa_WiFi'></div>"
    "<div class='form-group'><label for='password'>Contrasena Wi-Fi:</label>"
    "<input type='password' id='password' name='password' required placeholder='Contrasena de tu Wi-Fi'>"
    "<label class='toggle-pwd'><input type='checkbox' onclick='togglePwd()'> Mostrar contrasena</label></div>"
    "<button type='submit'>Conectar HUB a Internet</button></form></div>"
    "<script>function togglePwd(){var p=document.getElementById('password');p.type=p.type==='password'?'text':'password';}</script>"
    "</body></html>";

// ==========================================
// HELPERS
// ==========================================
static void format_mac(const uint8_t *mac, char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void url_decode(const char *src, char *dst, size_t dst_size) {
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '%') {
            if (src[i+1] && src[i+2]) {
                char hex[3] = {src[i+1], src[i+2], 0};
                dst[j++] = (char)strtol(hex, NULL, 16); i += 3;
            }
        } else if (src[i] == '+') { dst[j++] = ' '; i++; }
        else { dst[j++] = src[i++]; }
    }
    dst[j] = '\0';
}

// ==========================================
// NVS
// ==========================================
static esp_err_t save_wifi_credentials(const char *ssid, const char *password) {
    wifi_credentials_t creds;
    strncpy(creds.ssid, ssid, sizeof(creds.ssid) - 1); creds.ssid[sizeof(creds.ssid) - 1] = '\0';
    strncpy(creds.password, password, sizeof(creds.password) - 1); creds.password[sizeof(creds.password) - 1] = '\0';
    esp_err_t err = nvs_set_blob(nvs_handle_pairing, "wifi_creds", &creds, sizeof(creds));
    if (err == ESP_OK) err = nvs_commit(nvs_handle_pairing);
    return err;
}

static esp_err_t load_wifi_credentials(wifi_credentials_t *creds) {
    size_t len = sizeof(wifi_credentials_t);
    return nvs_get_blob(nvs_handle_pairing, "wifi_creds", creds, &len);
}

// ==========================================
// SERVIDOR HTTP (Portal Cautivo)
// ==========================================
static esp_err_t portal_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, portal_html, strlen(portal_html));
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    char ssid[33] = {0};
    char password[65] = {0};
    
    char *query = strdup(req->uri);
    char *ssid_param = strstr(query, "ssid=");
    char *pass_param = strstr(query, "password=");
    
    if (ssid_param) { ssid_param += 5; char *end = strchr(ssid_param, '&'); if (end) *end = '\0'; url_decode(ssid_param, ssid, sizeof(ssid)); }
    if (pass_param) { pass_param += 9; url_decode(pass_param, password, sizeof(password)); }
    free(query);
    
    ESP_LOGI(TAG, "Recibido SSID: %s", ssid);
    esp_err_t err = save_wifi_credentials(ssid, password);
    
    const char *html_resp;
    if (err == ESP_OK) {
        html_resp = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Exito</title>"
            "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f2f5;}"
            ".box{background:white;padding:40px;border-radius:12px;max-width:400px;margin:auto;box-shadow:0 4px 15px rgba(0,0,0,0.1);}"
            "h1{color:#27ae60;}</style></head><body><div class='box'>"
            "<h1>Configuracion Guardada</h1>"
            "<p>El HUB se esta conectando a tu red Wi-Fi y se reiniciara automaticamente en 5 segundos.</p>"
            "<p style='color:#7f8c8d;font-size:14px;'>Ya puedes cerrar esta ventana.</p></div></body></html>";
        
        ESP_LOGI(TAG, "Credenciales guardadas. Reiniciando en 5 segundos...");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html_resp, strlen(html_resp));
        
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    } else {
        html_resp = "<html><body><h1>Error al guardar</h1></body></html>";
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html_resp, strlen(html_resp));
    }
    return ESP_OK;
}

static void start_captive_portal(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 2;
    config.stack_size = 8192;
    
    esp_err_t err = httpd_start(&server, &config);
    if (err == ESP_OK) {
        httpd_uri_t portal_uri = { .uri = "/", .method = HTTP_GET, .handler = portal_handler };
        httpd_uri_t save_uri = { .uri = "/save_wifi", .method = HTTP_GET, .handler = save_wifi_handler };
        httpd_register_uri_handler(server, &portal_uri);
        httpd_register_uri_handler(server, &save_uri);
        
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_ip_info_t ip_info;
        if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "Portal Cautivo iniciado en http://" IPSTR, IP2STR(&ip_info.ip));
        }
    } else {
        ESP_LOGE(TAG, "ERROR al iniciar servidor HTTP: %s", esp_err_to_name(err));
    }
}

// ==========================================
// MQTT (con TLS via Cloudflare Tunnel)
// ==========================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) ESP_LOGI(TAG, "MQTT Conectado al Broker");
    else if (event_id == MQTT_EVENT_DISCONNECTED) ESP_LOGW(TAG, "MQTT Desconectado");
    else if (event_id == MQTT_EVENT_ERROR) ESP_LOGE(TAG, "MQTT Error");
}

static void start_mqtt_client(const char *broker_uri) {
    // Generar un Client ID unico SIN dos puntos para maxima compatibilidad
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char client_id_str[32];
    snprintf(client_id_str, sizeof(client_id_str), "hub_%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = client_id_str,
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

static void publish_event_to_mqtt(const uint8_t *sender_mac, const security_payload_t *payload) {
    if (!mqtt_client) return;
    char sender_mac_str[18]; format_mac(sender_mac, sender_mac_str, sizeof(sender_mac_str));
    char json_payload[512];
    snprintf(json_payload, sizeof(json_payload),
        "{\"measurement\":\"alarm_event\",\"tags\":{\"device_id\":\"%s\",\"device_type\":\"esp32_node\",\"hub_mac\":\"%s\"},"
        "\"fields\":{\"seq\":%d,\"event_code\":\"%.4s\",\"message\":\"%.4s\",\"system_state\":\"disarmed\"}}",
        sender_mac_str, hub_mac_str, payload->seq, payload->event_code, payload->event_code);
    char topic[128]; snprintf(topic, sizeof(topic), "seguimiento360/tenant/0/site/site0/hub/%s/event", hub_mac_str);
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, json_payload, strlen(json_payload), 1, 0);
    ESP_LOGI(TAG, "MQTT Publicado (ID: %d) en topico: %s", msg_id, topic);
}

// ==========================================
// ESP-NOW
// ==========================================
static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < sizeof(security_payload_t)) return;
    security_payload_t *payload = (security_payload_t *)data;
    const uint8_t *sender_mac = info->src_addr;
    char mac_str[18]; format_mac(sender_mac, mac_str, sizeof(mac_str));
    ESP_LOGI(TAG, "ESP-NOW RX: Code=%.4s, MAC=%s", payload->event_code, mac_str);

    if (strncmp((char *)payload->event_code, "PAIR", 4) == 0) {
        bool already_paired = false;
        for (int i = 0; i < peer_count; i++) if (memcmp(peer_list[i].peer_addr, sender_mac, 6) == 0) { already_paired = true; break; }
        if (!already_paired && peer_count < MAX_PEERS) {
            esp_now_peer_info_t new_peer = { .channel = router_channel, .ifidx = ESP_IF_WIFI_AP, .encrypt = false };
            memcpy(new_peer.peer_addr, sender_mac, 6);
            if (esp_now_add_peer(&new_peer) == ESP_OK) {
                memcpy(peer_list[peer_count].peer_addr, sender_mac, 6); peer_count++;
                char key[10]; sprintf(key, "peer_%d", peer_count - 1);
                nvs_set_blob(nvs_handle_pairing, key, sender_mac, 6); nvs_commit(nvs_handle_pairing);
                ESP_LOGI(TAG, "Nodo vinculado. Total: %d", peer_count);
                
                security_payload_t ack; memset(&ack, 0, sizeof(ack));
                strncpy((char *)ack.event_code, "P_ACK", 4); ack.seq = payload->seq;
                esp_now_send(sender_mac, (uint8_t *)&ack, sizeof(ack));
            }
        }
        return;
    }
    publish_event_to_mqtt(sender_mac, payload);
}

static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    ESP_LOGI(TAG, "TX: %s", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ==========================================
// Wi-Fi Event Handler
// ==========================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi STA Conectado. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        if (server) { httpd_stop(server); server = NULL; }
        start_mqtt_client(MQTT_BROKER_URI);
    }
}

// ==========================================
// Cargar peers desde NVS
// ==========================================
static void load_peers_from_nvs(void) {
    peer_count = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        char key[10]; sprintf(key, "peer_%d", i);
        uint8_t mac[6]; size_t len = 6;
        if (nvs_get_blob(nvs_handle_pairing, key, mac, &len) == ESP_OK) {
            esp_now_peer_info_t peer = { .channel = router_channel, .ifidx = ESP_IF_WIFI_AP, .encrypt = false };
            memcpy(peer.peer_addr, mac, 6); esp_now_add_peer(&peer);
            memcpy(peer_list[peer_count].peer_addr, mac, 6); peer_count++;
        } else break;
    }
    ESP_LOGI(TAG, "%d nodos cargados desde NVS", peer_count);
}

// ==========================================
// MAIN
// ==========================================
void app_main(void) {
    ESP_LOGI(TAG, "Iniciando HUB Gateway...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init(); }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_open("pairing_storage", NVS_READWRITE, &nvs_handle_pairing));

    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    format_mac(mac, hub_mac_str, sizeof(hub_mac_str));

    wifi_credentials_t creds;
    esp_err_t creds_err = load_wifi_credentials(&creds);
    wifi_configured = (creds_err == ESP_OK && strlen(creds.ssid) > 0);
    ESP_LOGI(TAG, "Wi-Fi Configurado: %s", wifi_configured ? "SI" : "NO");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    if (wifi_configured) {
        wifi_config_t sta_config = { 0 };
        strncpy((char *)sta_config.sta.ssid, creds.ssid, sizeof(sta_config.sta.ssid));
        strncpy((char *)sta_config.sta.password, creds.password, sizeof(sta_config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    }
    
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Desactivar ahorro de energia del Wi-Fi para evitar brownout
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    // Esperar a que se conecte y obtener el canal del router
    if (wifi_configured) {
        for (int i = 0; i < 50; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                router_channel = ap_info.primary;
                ESP_LOGI(TAG, "Canal del router detectado: %d", router_channel);
                break;
            }
        }
    }
    
    // Configurar AP en el mismo canal que el STA (CRITICO para ESP-NOW)
    wifi_config_t ap_config = { 
        .ap = { 
            .ssid = WIFI_SSID_AP, 
            .ssid_len = strlen(WIFI_SSID_AP), 
            .password = WIFI_PASS_AP, 
            .max_connection = MAX_PEERS, 
            .authmode = WIFI_AUTH_WPA2_PSK, 
            .channel = router_channel
        } 
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_start(ap_netif);
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) ESP_LOGI(TAG, "AP IP: " IPSTR, IP2STR(&ip_info.ip));
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    
    // Agregar peer de broadcast para recibir pairing inicial
    esp_now_peer_info_t broadcast_peer = {
        .channel = router_channel,
        .ifidx = ESP_IF_WIFI_AP,
        .encrypt = false
    };
    memset(broadcast_peer.peer_addr, 0xFF, 6);
    esp_now_add_peer(&broadcast_peer);
    ESP_LOGI(TAG, "Modo broadcast activado para recibir pairing");
    
    load_peers_from_nvs();

    if (!wifi_configured) start_captive_portal();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  HUB GATEWAY INICIADO - MAC: %s", hub_mac_str);
    ESP_LOGI(TAG, "  Canal actual: %d", router_channel);
    ESP_LOGI(TAG, "  MQTT Broker: %s", MQTT_BROKER_URI);
    ESP_LOGI(TAG, "  Nodos vinculados: %d / %d", peer_count, MAX_PEERS);
    ESP_LOGI(TAG, "========================================");

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
