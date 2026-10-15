#include "ota_manager.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_crt_bundle.h"

static const char *TAG = "OTA_MANAGER";

static void ota_task(void *pvParameters) {
    const char *url = (const char *)pvParameters;
    ESP_LOGI(TAG, "Iniciando OTA desde: %s", url);

    // 1. Configurar el cliente HTTP (sintaxis correcta para ESP-IDF v5.x)
    esp_http_client_config_t http_config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000, // 10 segundos de timeout para evitar bloqueos
    };

    // 2. Configurar el gestor OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config, // Aquí va el puntero
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar OTA: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // 3. Bucle de descarga
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // Opcional: Aquí podrías reportar progreso si quisieras
    }

    // 4. Verificar que la descarga esté completa
    if (esp_https_ota_is_complete_data_received(ota_handle) != true) {
        ESP_LOGE(TAG, "La descarga se interrumpió o está incompleta.");
        esp_https_ota_abort(ota_handle);
        vTaskDelete(NULL);
        return;
    }

    // 5. Finalizar y marcar la nueva partición como válida
    err = esp_https_ota_finish(ota_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✅ OTA exitoso. Reiniciando en 3 segundos...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "❌ Error al finalizar OTA: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle); // Limpieza en caso de fallo
    }
    
    vTaskDelete(NULL);
}

esp_err_t ota_manager_start(const char *firmware_url) {
    if (firmware_url == NULL) return ESP_ERR_INVALID_ARG;
    
    BaseType_t ret = xTaskCreate(
        ota_task, 
        "ota_task", 
        8192,             // 8KB de stack (TLS consume bastante)
        (void *)firmware_url, 
        5,                // Prioridad 5
        NULL
    );
    
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
