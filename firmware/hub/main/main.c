/**
 * @file main.c
 * @brief Punto de entrada - Hito M1: Fundación BSP y NVS
 *
 * Specs: A7 (M1), M1-SPEC-IMPL v0.2, A1 v0.2 (tareas).
 *
 * Crea:
 *  - Cola xQ_Tamper (5 slots) para ISR de tamper
 *  - Inicializa NVS (3 particiones segregadas)
 *  - Inicializa BSP (GPIOs, ADC, ISR)
 *  - Valida lectura/escritura persistente de NVS
 *  - Blink de éxito y tarea monitor con WDT
 */

#include "bsp.h"
#include "nvs_manager.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "MAIN";

// Tamaño de cola de tamper (A1 v0.2 sección 4)
#define TAMPER_QUEUE_LEN  5

// Tarea monitor de M1 (en M2+ será reemplazada por t_sys_mon)
#define TASK_M1_MON_NAME   "t_m1_mon"
#define TASK_M1_MON_STACK  4096
#define TASK_M1_MON_PRIO   1

// Cola de tamper (global de M1)
static QueueHandle_t s_xQ_Tamper = NULL;

/**
 * @brief Tarea monitor M1: alimenta WDT, consume tamper y power_loss.
 */
static void task_m1_monitor(void *pvParameters)
{
    // [HUB-F-030] Registrar tarea con el Task Watchdog
    esp_task_wdt_add(NULL);

    tamper_event_t evt;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t blink_counter = 0;

    ESP_LOGI(TAG, "[M1] Tarea monitor iniciada (WDT activo)");

    while (1) {
        // [HUB-F-030] Alimentar watchdog
        esp_task_wdt_reset();

        // Consumir eventos de tamper (no bloqueante)
        if (xQueueReceive(s_xQ_Tamper, &evt, 0) == pdTRUE) {
            ESP_LOGW(TAG, "[M1] TAMPER EVENTO! type=%d ts=%lu ms",
                     evt.type, (unsigned long)evt.timestamp_ms);
            // En M3+ transicionará SM_ALARM a ALARM.
        }

        // Monitoreo de power_loss (cada 500ms)
        // [HUB-NF-003] En M2+ t_sys_mon forzará nvs_commit de emergencia
        if (bsp_is_power_loss_detected()) {
            ESP_LOGE(TAG, "[M1] POWER LOSS inminente! Commit NVS de emergencia...");
            nvs_manager_emergency_commit();
        }

        // Heartbeat LED: 1 pulso cada 5s (tarea viva)
        if (++blink_counter >= 10) {
            blink_counter = 0;
            bsp_set_status_led(true);
            vTaskDelay(pdMS_TO_TICKS(100));
            bsp_set_status_led(false);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=======================================================");
    ESP_LOGI(TAG, "  HUB-S3 | Hito M1: Fundación BSP y NVS");
    ESP_LOGI(TAG, "  Specs: A1 v0.2, A2 v0.2, A3 v0.1, A4 v0.2");
    ESP_LOGI(TAG, "=======================================================");

    // =====================================================================
    // [AT-05] Paso 1: NVS (3 particiones segregadas - A3 v0.1)
    // =====================================================================
    ESP_LOGI(TAG, "[M1] Inicializando NVS...");
    ESP_ERROR_CHECK(nvs_manager_init());

    // =====================================================================
    // [AT-05] Paso 2: Cola IPC para eventos de tamper
    // =====================================================================
    ESP_LOGI(TAG, "[M1] Creando cola xQ_Tamper...");
    s_xQ_Tamper = xQueueCreate(TAMPER_QUEUE_LEN, sizeof(tamper_event_t));
    if (s_xQ_Tamper == NULL) {
        ESP_LOGE(TAG, "FATAL: no se pudo crear xQ_Tamper");
        abort();
    }

    // =====================================================================
    // [AT-05] Paso 3: BSP (GPIOs + ADC + ISR tamper)
    // =====================================================================
    ESP_LOGI(TAG, "[M1] Inicializando BSP...");
    ESP_ERROR_CHECK(bsp_init(s_xQ_Tamper));

    // =====================================================================
    // [AT-05] Paso 4: Validar persistencia NVS (config)
    // =====================================================================
    ESP_LOGI(TAG, "[M1] Validando persistencia NVS...");
    char dev_id[32] = {0};
    esp_err_t ret = nvs_manager_get_device_id(dev_id, sizeof(dev_id));

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[M1] Device ID no existe, escribiendo default...");
        ESP_ERROR_CHECK(nvs_manager_set_device_id("HUB-S3-0001"));
        ESP_ERROR_CHECK(nvs_manager_get_device_id(dev_id, sizeof(dev_id)));
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[M1] FATAL: error leyendo Device ID: %s",
                 esp_err_to_name(ret));
        abort();
    }
    ESP_LOGI(TAG, "[M1] Device ID persistente: '%s'", dev_id);

    // Tenant ID (0 = estado factory, sin provisioning aún)
    uint32_t tenant_id = 0;
    ret = nvs_manager_get_tenant_id(&tenant_id);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[M1] Tenant ID no existe (estado factory OK)");
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[M1] Tenant ID persistente: %lu",
                 (unsigned long)tenant_id);
    }

    // =====================================================================
    // [AT-05] Paso 5: Validar estado de alarma persistente (nvs_state)
    // =====================================================================
    uint8_t alarm_state = 0xFF;
    ret = nvs_manager_get_alarm_state(&alarm_state);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[M1] Estado alarma no existe, init DISARMED (0)");
        ESP_ERROR_CHECK(nvs_manager_set_alarm_state(0));
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[M1] Estado alarma persistente: %u (0=disarmed)",
                 alarm_state);
    }

    // =====================================================================
    // [AT-05] Paso 6: Blink de éxito (3 parpadeos de 200ms)
    // =====================================================================
    ESP_LOGI(TAG, "[M1] Indicador visual de éxito...");
    ESP_ERROR_CHECK(bsp_blink_led(3, 200));

    // =====================================================================
    // [AT-05] Paso 7: Tarea monitor con WDT (Core 0, Pri 1 - A1 v0.2)
    // =====================================================================
    ESP_LOGI(TAG, "[M1] Lanzando tarea monitor (Core 0, Pri 1)...");
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        task_m1_monitor,
        TASK_M1_MON_NAME,
        TASK_M1_MON_STACK,
        NULL,
        TASK_M1_MON_PRIO,
        NULL,
        0  // Core 0 (tiempo real / RF según A1 v0.2)
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "[M1] FATAL: no se pudo crear tarea monitor");
        abort();
    }

    // =====================================================================
    // app_main retorna; el scheduler FreeRTOS continúa
    // =====================================================================
    ESP_LOGI(TAG, "[M1] ======================================================");
    ESP_LOGI(TAG, "[M1]  M1 COMPLETADO - Fundación BSP y NVS operativa");
    ESP_LOGI(TAG, "[M1]  Pruebas: GPIO2->GND = tamper | bajar V = power_loss");
    ESP_LOGI(TAG, "[M1] ======================================================");
}
