// ============================================================================
// Seguimiento 360 - SENSOR ULTRASÓNICO HC-SR04
// Compatible con HUB v6.1.0 (Payload estricto de 7 bytes)
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

static const char *TAG = "NODE_ULTRASONIC";

// 1. CONFIGURACIÓN DE HARDWARE (Pines ajustados para SuperMini)
#define TRIG_PIN                GPIO_NUM_5
#define ECHO_PIN                GPIO_NUM_4
#define LED_PIN                 GPIO_NUM_2
#define WIFI_CHANNEL            5
#define HEARTBEAT_INTERVAL_SEC  (10 * 60)
#define MEASUREMENT_INTERVAL_SEC  5
#define DISTANCE_THRESHOLD_CM   50
#define MAX_DISTANCE_CM         400

// 2. PAYLOAD ESTRICTO DE 7 BYTES
typedef struct {
    uint8_t event_code[4];
    uint8_t seq;
    uint8_t battery_level;
    uint8_t reserved;
} __attribute__((packed)) security_payload_t;

_Static_assert(sizeof(security_payload_t) == 7, "EL PAYLOAD DEBE SER DE 7 BYTES EXACTOS");

// 3. VARIABLES GLOBALES
static uint8_t seq_counter = 0;
static esp_now_peer_info_t peerInfo;
static uint8_t hub_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t last_measurement = 0;
static float last_distance = 0;

// 4. SENSOR HC-SR04
static float measure_distance(void) {
    uint64_t start_us, end_us;
    float distance_cm;
    
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
    if (timeout == 0) return 0;
    
    start_us = esp_timer_get_time();
    timeout = 100000;
    
    while (gpio_get_level(ECHO_PIN) == 1 && timeout > 0) {
        esp_rom_delay_us(1);
        timeout--;
    }
    end_us = esp_timer_get_time();
    
    uint64_t duration_us = end_us - start_us;
    distance_cm = (duration_us * 0.0343) / 2.0;
    
    if (distance_cm > MAX_DISTANCE_CM || distance_cm < 0) return 0;
    return distance_cm;
}

static void init_ultrasonic_sensor(void) {
    gpio_reset_pin(TRIG_PIN);
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIG_PIN, 0);
    
    gpio_reset_pin(ECHO_PIN);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
    gpio_pullup_dis(ECHO_PIN);
    gpio_pulldown_en(ECHO_PIN);
    
    ESP_LOGI(TAG, "Sensor HC-SR04 inicializado (TRIG=%d, ECHO=%d)", TRIG_PIN, ECHO_PIN);
}

// 5. CALLBACKS ESP-NOW
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "✅ Mensaje enviado correctamente");
        gpio_set_level(LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, " Error al enviar: %s", esp_err_to_name(status));
    }
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len >= 4 && memcmp(data, "P_ACK", 5) == 0) {
        ESP_LOGI(TAG, "✅ P_ACK recibido del HUB.");
    }
}

static void send_event(const char *event_code, uint8_t distance_cm) {
    security_payload_t payload = {0};
    strncpy((char *)payload.event_code, event_code, 4);
    payload.seq = seq_counter++;
    payload.battery_level = 95;
    payload.reserved = distance_cm;
    
    ESP_LOGI(TAG, "📡 Enviando: %s | Dist: %dcm | Seq: %d", event_code, distance_cm, payload.seq);
    gpio_set_level(LED_PIN, 1);
    esp_now_send(hub_mac, (uint8_t *)&payload, sizeof(payload));
    vTaskDelay(pdMS_TO_TICKS(200));
}

// 6. APP MAIN
void app_main(void) {
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
    
    // Configurar canal (sin ESP_ERROR_CHECK para evitar crash en v5.x)
    esp_err_t err_channel = esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err_channel != ESP_OK) {
        ESP_LOGW(TAG, "Aviso canal: %s", esp_err_to_name(err_channel));
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, hub_mac, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.ifidx = ESP_IF_WIFI_STA;
    peerInfo.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    init_ultrasonic_sensor();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  SENSOR ULTRASÓNICO HC-SR04 v1.0");
    ESP_LOGI(TAG, "  MAC: a0:f2:62:f1:d0:f0");
    ESP_LOGI(TAG, "  Umbral: %d cm | Medición: %d seg", DISTANCE_THRESHOLD_CM, MEASUREMENT_INTERVAL_SEC);
    ESP_LOGI(TAG, "========================================");

    send_event("PAIR", 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000000;
        
        if (now - last_measurement >= MEASUREMENT_INTERVAL_SEC) {
            float distance = measure_distance();
            last_distance = distance;
            last_measurement = now;
            
            ESP_LOGI(TAG, "📏 Distancia: %.1f cm", distance);
            
            if (distance > 0 && distance < DISTANCE_THRESHOLD_CM) {
                ESP_LOGW(TAG, "🚨 ¡OBJETO DETECTADO! %.1f cm", distance);
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
