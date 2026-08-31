#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#define TAG "NODE_SECURITY"

#define BUTTON_PIN GPIO_NUM_0
#define LED_PIN GPIO_NUM_2
#define HEARTBEAT_INTERVAL_MS (10 * 60 * 1000)
#define DEBOUNCE_TIME_MS 200

typedef struct {
    uint8_t event_code[4];
    uint8_t seq;
    uint8_t battery_level;
    uint8_t reserved;
} security_payload_t;

static uint8_t seq_counter = 0;
static SemaphoreHandle_t button_semaphore = NULL;
static esp_now_peer_info_t peerInfo;
static uint8_t hub_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Mensaje enviado correctamente");
        gpio_set_level(LED_PIN, 0);
    } else {
        ESP_LOGE(TAG, "Error al enviar mensaje");
        gpio_set_level(LED_PIN, 1);
    }
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    ESP_LOGI(TAG, "Paquete recibido del HUB (len=%d)", len);
}

static void IRAM_ATTR button_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(button_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void send_security_event(const char *event_code) {
    security_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    
    strncpy((char *)payload.event_code, event_code, 4);
    payload.seq = seq_counter++;
    payload.battery_level = 95;
    
    ESP_LOGI(TAG, "Enviando evento: %s (seq=%d)", event_code, payload.seq);
    gpio_set_level(LED_PIN, 1);
    
    esp_err_t result = esp_now_send(hub_mac, (uint8_t *)&payload, sizeof(payload));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Error al enviar: %s", esp_err_to_name(result));
        gpio_set_level(LED_PIN, 0);
    }
}

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

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, hub_mac, 6);
    peerInfo.channel = 0;
    peerInfo.ifidx = ESP_IF_WIFI_STA;
    peerInfo.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peerInfo));

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

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    button_semaphore = xSemaphoreCreateBinary();

ESP_LOGI(TAG, "========================================");
ESP_LOGI(TAG, "  NODO DE SEGURIDAD INICIADO");
ESP_LOGI(TAG, "  Botón BOOT = Simular Alarma");
ESP_LOGI(TAG, "  Heartbeat cada 10 minutos");
ESP_LOGI(TAG, "========================================");

// AGREGAR ESTAS LÍNEAS:
ESP_LOGI(TAG, "Rehaciendo pairing...");
send_security_event("PAIR");
vTaskDelay(pdMS_TO_TICKS(3000));

// Bucle principal
while (1) {

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  NODO DE SEGURIDAD INICIADO");
    ESP_LOGI(TAG, "  Botón BOOT = Simular Alarma");
    ESP_LOGI(TAG, "  Heartbeat cada 10 minutos");
    ESP_LOGI(TAG, "========================================");

    // ESP_LOGI(TAG, "Iniciando protocolo de vinculación (PAIR)...");
    // send_security_event("PAIR");
    // vTaskDelay(pdMS_TO_TICKS(3000));

    uint32_t last_heartbeat = 0;
    
    while (1) {
        if (xSemaphoreTake(button_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
            
            if (gpio_get_level(BUTTON_PIN) == 0) {
                ESP_LOGI(TAG, "¡ALARMA DETECTADA! (Botón presionado)");
                send_security_event("ALRM");
            }
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            ESP_LOGI(TAG, "Heartbeat enviado");
            send_security_event("HEAR");
            last_heartbeat = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
