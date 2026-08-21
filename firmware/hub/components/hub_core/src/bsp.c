/**
 * @file bsp.c
 * @brief Implementación del BSP para HUB-S3
 *
 * Specs: A1 v0.2, A4 v0.2 (tamper ISR), M1-SPEC-IMPL v0.2.
 */

#include "bsp.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP";

// Handle global del ADC (modo oneshot)
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

// Cola hacia t_logic (guardada para uso en ISR)
static QueueHandle_t s_xQ_Tamper = NULL;

// Umbral ADC para power_loss (divisor 1/11, 12-bit, atten 12dB)
#define POWER_LOSS_ADC_THRESHOLD  2500

// 0 = DevKit (sin circuito divisor), 1 = PCB HUB-S3 final
#define BSP_POWER_LOSS_ENABLED 0

/* =========================================================================
 * [HUB-F-015] ISR de Tamper (Case Open) - ligera, solo encola
 * ========================================================================= */
static void IRAM_ATTR tamper_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    tamper_event_t evt = {
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .type = TAMPER_CASE_OPEN,
        .system_state = 0  // Se completa en t_logic al consumir
    };

    if (s_xQ_Tamper != NULL) {
        xQueueSendFromISR(s_xQ_Tamper, &evt, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* =========================================================================
 * Inicialización de GPIOs
 * ========================================================================= */
static esp_err_t bsp_init_gpios(void)
{
    // [HUB-F-010] Salidas: LED de estado y Power Enable
    gpio_config_t io_out = {
        .pin_bit_mask = (1ULL << BSP_GPIO_STATUS_LED) | (1ULL << BSP_GPIO_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando salidas: %s", esp_err_to_name(ret));
        return ret;
    }

    // Estado inicial: LED apagado, sensores habilitados
    gpio_set_level(BSP_GPIO_STATUS_LED, 0);
    gpio_set_level(BSP_GPIO_POWER_EN, 1);

    // [HUB-F-015] Tamper: pull-up interno, interrupción flanco descendente
    gpio_config_t io_tamper = {
        .pin_bit_mask = (1ULL << BSP_GPIO_TAMPER_INPUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE   // Activo bajo al abrir carcasa
    };
    ret = gpio_config(&io_tamper);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando tamper: %s", esp_err_to_name(ret));
        return ret;
    }

    // Servicio de interrupciones + handler
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Error instalando ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = gpio_isr_handler_add(BSP_GPIO_TAMPER_INPUT, tamper_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando ISR tamper: %s", esp_err_to_name(ret));
        return ret;
    }

    // [HUB-F-020] Botón factory: pull-up, lectura por polling (sin ISR)
    gpio_config_t io_btn = {
        .pin_bit_mask = (1ULL << BSP_GPIO_FACTORY_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&io_btn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando botón factory: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GPIOs OK (LED:%d PWR:%d TAMP:%d BTN:%d)",
             BSP_GPIO_STATUS_LED, BSP_GPIO_POWER_EN,
             BSP_GPIO_TAMPER_INPUT, BSP_GPIO_FACTORY_BTN);
    return ESP_OK;
}

/* =========================================================================
 * Inicialización del ADC (power_loss)
 * ========================================================================= */
static esp_err_t bsp_init_adc(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error creando unidad ADC: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12
    };
    ret = adc_oneshot_config_channel(s_adc_handle, BSP_ADC_POWER_LOSS_CHAN, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando canal ADC: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ADC power_loss OK (canal %d, 12-bit, 12dB)", BSP_ADC_POWER_LOSS_CHAN);
    return ESP_OK;
}

/* =========================================================================
 * API pública
 * ========================================================================= */
esp_err_t bsp_init(QueueHandle_t xQ_Tamper)
{
    if (xQ_Tamper == NULL) {
        ESP_LOGE(TAG, "xQ_Tamper no puede ser NULL");
        return ESP_ERR_INVALID_ARG;
    }
    s_xQ_Tamper = xQ_Tamper;

    esp_err_t ret = bsp_init_gpios();
    if (ret != ESP_OK) return ret;

    ret = bsp_init_adc();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "BSP inicializado correctamente");
    return ESP_OK;
}

esp_err_t bsp_set_status_led(bool state)
{
    gpio_set_level(BSP_GPIO_STATUS_LED, state ? 1 : 0);
    return ESP_OK;
}

esp_err_t bsp_blink_led(uint8_t times, uint32_t delay_ms)
{
    for (uint8_t i = 0; i < times; i++) {
        gpio_set_level(BSP_GPIO_STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(BSP_GPIO_STATUS_LED, 0);
        if (i + 1 < times) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
    return ESP_OK;
}

bool bsp_is_power_loss_detected(void)
{
    if (!BSP_POWER_LOSS_ENABLED) return false;
    if (s_adc_handle == NULL) return false;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, BSP_ADC_POWER_LOSS_CHAN, &raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Error leyendo ADC: %s", esp_err_to_name(ret));
        return false;
    }

    // [HUB-NF-003] Voltaje bajo umbral => corte inminente
    bool loss = (raw < POWER_LOSS_ADC_THRESHOLD);
    if (loss) {
        ESP_LOGW(TAG, "Power loss! ADC raw=%d (< %d)", raw, POWER_LOSS_ADC_THRESHOLD);
    }
    return loss;
}

esp_err_t bsp_set_power_enable(bool enable)
{
    gpio_set_level(BSP_GPIO_POWER_EN, enable ? 1 : 0);
    return ESP_OK;
}
