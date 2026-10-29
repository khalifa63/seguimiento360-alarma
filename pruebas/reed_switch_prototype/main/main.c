/**
 * @file main.c
 * @brief Prototipo: lectura del estado de un Reed Switch magnético de 28 mm
 *        en un ESP32 DevKit V1, con reporte por el monitor serial.
 *
 * Proyecto: seguimiento360-alarma / pruebas / reed_switch_prototype
 *
 * Hardware:
 *   - Reed Switch de 28 mm Normalmente Abierto (N.O.), entre GPIO4 y GND.
 *   - Resistencia pull-up INTERNA del ESP32 en GPIO4 (activada por firmware,
 *     ~45 k Ohm), lo que evita estados flotantes sin hardware adicional.
 *
 * Lógica de lectura (Reed N.O. con pull-up):
 *   - Imán CERCA -> contactos cerrados (conduce) -> GPIO4 en LOW  -> "Switch Cerrado".
 *   - Imán LEJOS -> contactos abiertos           -> GPIO4 en HIGH -> "Switch Abierto".
 *
 * Nota: si el Reed Switch fuera Normalmente Cerrado (N.C.), la lógica se
 * invierte; ver sección "Problemas conocidos" del README.md.
 *
 * Salida serial: 115200 baudios vía ESP_LOGI. SOLO se imprime cuando el
 * estado cambia (más una línea con el estado inicial al arrancar).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ============================== Configuración ============================== */

/**
 * GPIO4 se elige porque es un pin seguro en el DevKit V1:
 *  - No es pin de "strapping" (GPIO0, GPIO2, GPIO5, GPIO12, GPIO15) ni parte
 *    de la flash interna (GPIO6-GPIO11), por lo que no interfiere en el
 *    arranque del ESP32.
 *  - No pertenece al rango GPIO34-GPIO39, que es de entrada únicamente y
 *    NO tiene resistencias pull-up/pull-down internas.
 *  - Soporta entrada digital y pull-up interna.
 */
#define REED_SWITCH_GPIO     GPIO_NUM_4

/** Retardo entre lecturas en ms: evita lecturas repetitivas y atenúa el
 *  rebote mecánico del Reed (contact bounce). */
#define REED_READ_PERIOD_MS  500

/** Etiqueta de log: permite filtrar los mensajes con idf.py monitor. */
static const char *TAG = "REED_SWITCH";

/* ============================ Inicialización =============================== */

/**
 * @brief Configura el pin del Reed Switch como entrada digital con
 *        resistencia pull-up interna habilitada.
 */
static void reed_switch_init(void)
{
    /* Con inicializadores designados, todo campo no listado queda en 0. */
    const gpio_config_t reed_conf = {
        .pin_bit_mask = (1ULL << REED_SWITCH_GPIO),  /* Máscara: solo GPIO4.   */
        .mode         = GPIO_MODE_INPUT,             /* Entrada digital.       */
        .pull_up_en   = GPIO_PULLUP_ENABLE,          /* Pull-up interna: evita */
                                                     /* el estado flotante.    */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,       /* Pull-down innecesario. */
        .intr_type    = GPIO_INTR_DISABLE,           /* Sin interrupciones en  */
                                                     /* este prototipo.        */
    };
    ESP_ERROR_CHECK(gpio_config(&reed_conf));        /* Aborta si hay error.   */
}

/* =============================== Aplicación ================================ */

/**
 * @brief Punto de entrada de la aplicación (tarea principal de FreeRTOS).
 *
 * Sondeo cada 500 ms: SOLO imprime cuando el estado del Reed Switch CAMBIA
 * (un mensaje por transición), no en cada vuelta del bucle. El retardo
 * actúa además como filtro del rebote mecánico (contact bounce).
 */
void app_main(void)
{
    reed_switch_init();

    /* Nivel actual de GPIO4 y último nivel reportado por el serial. */
    int level      = gpio_get_level(REED_SWITCH_GPIO);
    int last_level = level;

    ESP_LOGI(TAG, "Prototipo Reed Switch iniciado (GPIO %d)", (int)REED_SWITCH_GPIO);

    /* Estado inicial: una sola línea de referencia en el arranque. */
    ESP_LOGI(TAG, "Estado inicial: %s", (level == 0) ? "Switch Cerrado" : "Switch Abierto");

    while (1) {
        level = gpio_get_level(REED_SWITCH_GPIO);

        if (level != last_level) {
            if (level == 0) {
                /* LOW  (0): el Reed conduce porque el imán está cerca. */
                ESP_LOGI(TAG, "Switch Cerrado");
            } else {
                /* HIGH (1): el Reed no conduce; el pull-up lleva el pin a 3.3 V. */
                ESP_LOGI(TAG, "Switch Abierto");
            }
            last_level = level;  /* Actualiza la referencia: no repite el mensaje. */
        }

        /* Sondeo cada 500 ms (bloqueo cooperativo, no busy-wait):
         * filtra el rebote mecánico y evita lecturas repetitivas. */
        vTaskDelay(pdMS_TO_TICKS(REED_READ_PERIOD_MS));
    }
}
