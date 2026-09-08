// ============================================================================
// Seguimiento 360 - NODO DE SEGURIDAD
// Compatible con HUB Fase 4 (Multi-tenant + Clasificación de dispositivos)
// Versión: 2.1
// ============================================================================

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

static const char *TAG = "NODE_SECURITY";

// ============================================================================
// CONFIGURACIÓN DE HARDWARE
// ============================================================================
#define BUTTON_PIN              GPIO_NUM_0    // Botón BOOT del ESP32
#define LED_PIN                 GPIO_NUM_2    // LED indicador
#define WIFI_CHANNEL            5             // DEBE COINCIDIR CON EL HUB
#define HEARTBEAT_INTERVAL_MS   (10 * 60 * 1000)  // 10 minutos
#define DEBOUNCE_TIME_MS        200           // Anti-rebote del botón

// ============================================================================
// TIPOS DE DISPOSITIVO SOPORTADOS
// ============================================================================
typedef enum {
    DEVICE_GENERIC  = 0,   // Genérico (default)
    DEVICE_SMOKE    = 1,   // Detector de humo
    DEVICE_MOTION   = 2,   // Sensor de movimiento (PIR)
    DEVICE_DOOR     = 3,   // Apertura de puerta/ventana
    DEVICE_TEMP     = 4,   // Sensor de temperatura
    DEVICE_GAS      = 5,   // Detector de gas
    DEVICE_PRESENCE = 6,   // Sensor de presencia
    DEVICE_WATER    = 7    // Sensor de inundación
} device_type_t;

// ⚙️ CONFIGURA AQUÍ EL TIPO DE NODO QUE ESTÁS COMPILANDO
#define MY_DEVICE_TYPE DEVICE_SMOKE

// Mapeo de tipos a strings (máximo 15 caracteres)
static const char* device_type_str[] = {
    "GENERIC", "SMOKE", "MOTION", "DOOR", "TEMP", "GAS", "PRESENCE", "WATER"
};

// ============================================================================
// ESTRUCTURA DEL PAYLOAD (COMPATIBLE CON HUB FASE 4)
// ============================================================================
typedef struct {
    uint8_t event_code[4];    // "PAIR", "ALRM", "HEAR", "P_ACK"
    uint8_t seq;              // Contador de secuencia (0-255)
    uint8_t battery_level;    // Nivel de batería (0-100%)
    uint8_t reserved;         // Reservado para uso futuro
    char device_type[16];     // Tipo de dispositivo (ej: "SMOKE", "MOTION")
} __attribute__((packed)) security_payload_t;

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
static uint8_t seq_counter = 0;
static SemaphoreHandle_t button_semaphore = NULL;
static esp_now_peer_info_t peerInfo;
static uint8_t hub_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Broadcast
static uint8_t my_mac[6];
static char my_mac_str[18];
static bool is_paired = false;

// ============================================================================
// CALLBACKS ESP-NOW
// ============================================================================

static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "✅ Mensaje enviado correctamente");
        gpio_set_level(LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, "❌ Error al enviar mensaje");
        gpio_set_level(LED_PIN, 1);
    }
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < sizeof(security_payload_t)) {
        ESP_LOGW(TAG, "Paquete recibido muy pequeño: %d bytes", len);
        return;
    }
    
    security_payload_t *payload = (security_payload_t *)data;
    
    // Procesar ACK de emparejamiento del HUB
    if (memcmp(payload->event_code, "P_ACK", 5) == 0) {
        ESP_LOGI(TAG, "✅ P_ACK recibido del HUB. Emparejamiento exitoso!");
        is_paired = true;
        
        // Feedback visual: 3 parpadeos rápidos
        for (int i = 0; i < 3; i++) {
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else {
        ESP_LOGI(TAG, "Paquete recibido del HUB (len=%d, code=%.4s)", 
                 len, payload->event_code);
    }
}

// ============================================================================
// INTERRUPCIÓN DEL BOTÓN
// ============================================================================

static void IRAM_ATTR button_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(button_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ============================================================================
// ENVÍO DE EVENTOS DE SEGURIDAD
// ============================================================================

static void send_security_event(const char *event_code) {
    security_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    
    // Copiar código de evento
    strncpy((char *)payload.event_code, event_code, 4);
    
    // Incrementar contador de secuencia
    payload.seq = seq_counter++;
    
    // Nivel de batería (simulado, puedes leerlo de un ADC real)
    payload.battery_level = 95;
    
    // Tipo de dispositivo
    strncpy(payload.device_type, device_type_str[MY_DEVICE_TYPE], 15);
    payload.device_type[15] = '\0';
    
    ESP_LOGI(TAG, "📡 Enviando evento: %s (seq=%d, tipo=%s, bat=%d%%)", 
             event_code, payload.seq, payload.device_type, payload.battery_level);
    
    // Feedback visual
    gpio_set_level(LED_PIN, 1);
    
    // Enviar paquete
    esp_err_t result = esp_now_send(hub_mac, (uint8_t *)&payload, sizeof(payload));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "❌ Error al enviar: %s", esp_err_to_name(result));
        gpio_set_level(LED_PIN, 0);
    }
}

// ============================================================================
// FUNCIÓN AUXILIAR: Formatear MAC
// ============================================================================

static void format_mac(const uint8_t *mac, char *out, size_t out_size) {
    snprintf(out, out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ============================================================================
// APP MAIN
// ============================================================================

void app_main(void) {
    // 1. Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Obtener MAC del dispositivo
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    format_mac(my_mac, my_mac_str, sizeof(my_mac_str));

    // 3. Inicializar red Wi-Fi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // CRÍTICO: Forzar el canal Wi-Fi al mismo que el HUB (Canal 5)
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // 4. Inicializar ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    // 5. Configurar peer broadcast para pairing
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, hub_mac, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.ifidx = ESP_IF_WIFI_STA;
    peerInfo.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));

    // 6. Configurar GPIO del botón BOOT
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    // 7. Configurar LED indicador
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    // 8. Crear semáforo para el botón
    button_semaphore = xSemaphoreCreateBinary();

    // 9. Mensaje de inicio
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  SEGUIMIENTO 360 - NODO v2.1");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "MAC: %s", my_mac_str);
    ESP_LOGI(TAG, "Tipo: %s", device_type_str[MY_DEVICE_TYPE]);
    ESP_LOGI(TAG, "Canal Wi-Fi: %d", WIFI_CHANNEL);
    ESP_LOGI(TAG, "Botón BOOT = Simular Alarma");
    ESP_LOGI(TAG, "Heartbeat cada %d minutos", HEARTBEAT_INTERVAL_MS / 60000);
    ESP_LOGI(TAG, "========================================");

    // 10. Feedback visual de inicio (2 parpadeos)
    for (int i = 0; i < 2; i++) {
        gpio_set_level(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // 11. Intentar emparejamiento inicial
    ESP_LOGI(TAG, "Iniciando protocolo de vinculación (PAIR)...");
    send_security_event("PAIR");
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 12. Bucle principal
    uint32_t last_heartbeat = 0;
    
    while (1) {
        // Esperar evento del botón o timeout de 1 segundo
        if (xSemaphoreTake(button_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Debounce
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
            
            // Verificar que el botón sigue presionado
            if (gpio_get_level(BUTTON_PIN) == 0) {
                ESP_LOGI(TAG, "🚨 ¡ALARMA DETECTADA! (Botón presionado)");
                send_security_event("ALRM");
            }
        }

        // Heartbeat periódico
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            ESP_LOGI(TAG, "💓 Heartbeat enviado");
            send_security_event("HEAR");
            last_heartbeat = now;
            
            // Parpadeo corto de confirmación
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(LED_PIN, 0);
        }

        // Yield para no saturar CPU
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}