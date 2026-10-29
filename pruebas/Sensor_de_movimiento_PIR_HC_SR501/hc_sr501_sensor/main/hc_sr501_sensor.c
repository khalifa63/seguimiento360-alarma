#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#define SENSOR_PIN      GPIO_NUM_27
#define TAG             "HC_SR501"
#define UART_NUM        UART_NUM_0
#define BUF_SIZE        256
#define RD_BUF_SIZE     (BUF_SIZE * 2)
#define NVS_NAMESPACE   "hc_sr501"

#define WIFI_CONNECTED_BIT  BIT0
#define SNTP_SYNC_BIT       BIT1

// Parámetros configurables
typedef struct {
    int sensitivity_threshold;
    int sample_window;
    int read_interval_ms;
    int hold_time_ms;
    int show_raw;
    char wifi_ssid[33];
    char wifi_pass[65];
    char timezone[50];
} sensor_config_t;

static sensor_config_t config = {
    .sensitivity_threshold = 3,
    .sample_window = 5,
    .read_interval_ms = 100,
    .hold_time_ms = 2500,
    .show_raw = 0,
    .wifi_ssid = "",
    .wifi_pass = "",
    .timezone = "UTC0"
};

static int detection_count = 0;
static int *samples = NULL;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_time_synced = false;
static int64_t start_time_us = 0;

// === Eventos WiFi ===
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Reconectando a WiFi (intento %d)...", s_retry_num);
        } else {
            ESP_LOGE(TAG, "No se pudo conectar a WiFi después de 5 intentos");
            s_time_synced = false;
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// === Callback de sincronización SNTP ===
static void sntp_sync_notification(struct timeval *tv) {
    ESP_LOGI(TAG, "Hora sincronizada con servidor NTP");
    s_time_synced = true;
    xEventGroupSetBits(s_wifi_event_group, SNTP_SYNC_BIT);
}

// === Inicializar WiFi ===
void wifi_init_sta(void) {
    if (strlen(config.wifi_ssid) == 0) {
        ESP_LOGW(TAG, "WiFi no configurado. Usa 'set wifi_ssid <nombre>' y 'set wifi_pass <clave>'");
        return;
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, config.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config.wifi_pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi iniciado. Conectando a SSID: %s", config.wifi_ssid);
}

// === Inicializar SNTP (CORREGIDO para ESP-IDF v5.x) ===
void sntp_init_task(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando cliente SNTP...");
    esp_sntp_setoperatingmode(SNTP_SYNC_MODE_SMOOTH);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification);
    esp_sntp_init();

    // Configurar zona horaria
    setenv("TZ", config.timezone, 1);
    tzset();

    // Esperar sincronización con timeout de 30 segundos
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           SNTP_SYNC_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (bits & SNTP_SYNC_BIT) {
        ESP_LOGI(TAG, "Hora sincronizada correctamente");
    } else {
        ESP_LOGW(TAG, "Timeout esperando sincronización SNTP");
    }
    
    vTaskDelete(NULL);
}

// === Función para obtener timestamp formateado ===
void get_timestamp(char *buffer, size_t buffer_size) {
    if (s_time_synced) {
        // Hora real con fecha
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm *tm_info = localtime(&tv.tv_sec);
        int ms = tv.tv_usec / 1000;
        
        snprintf(buffer, buffer_size, "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ms);
    } else {
        // Fallback: uptime desde inicio
        int64_t current_time_us = esp_timer_get_time();
        int64_t uptime_us = current_time_us - start_time_us;
        
        int64_t total_seconds = uptime_us / 1000000;
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;
        int milliseconds = (uptime_us % 1000000) / 1000;
        
        snprintf(buffer, buffer_size, "[%02d:%02d:%02d.%03d]", 
                 hours, minutes, seconds, milliseconds);
    }
}

#define PRINT_TIMESTAMPED(fmt, ...) do { \
    char timestamp[32]; \
    get_timestamp(timestamp, sizeof(timestamp)); \
    printf("%s " fmt "\n", timestamp, ##__VA_ARGS__); \
} while(0)

// === NVS ===
esp_err_t save_config_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_i32(handle, "sens_thresh", config.sensitivity_threshold);
    nvs_set_i32(handle, "sample_win", config.sample_window);
    nvs_set_i32(handle, "read_int", config.read_interval_ms);
    nvs_set_i32(handle, "hold_time", config.hold_time_ms);
    nvs_set_i32(handle, "show_raw", config.show_raw);
    nvs_set_str(handle, "wifi_ssid", config.wifi_ssid);
    nvs_set_str(handle, "wifi_pass", config.wifi_pass);
    nvs_set_str(handle, "timezone", config.timezone);

    err = nvs_commit(handle);
    nvs_close(handle);
    PRINT_TIMESTAMPED("Configuración guardada en NVS");
    return err;
}

esp_err_t load_config_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        PRINT_TIMESTAMPED("No hay configuración guardada, usando valores por defecto");
        return err;
    }

    int32_t val;
    if (nvs_get_i32(handle, "sens_thresh", &val) == ESP_OK) config.sensitivity_threshold = val;
    if (nvs_get_i32(handle, "sample_win", &val) == ESP_OK) config.sample_window = val;
    if (nvs_get_i32(handle, "read_int", &val) == ESP_OK) config.read_interval_ms = val;
    if (nvs_get_i32(handle, "hold_time", &val) == ESP_OK) config.hold_time_ms = val;
    if (nvs_get_i32(handle, "show_raw", &val) == ESP_OK) config.show_raw = val;

    size_t len;
    len = sizeof(config.wifi_ssid);
    if (nvs_get_str(handle, "wifi_ssid", config.wifi_ssid, &len) != ESP_OK) config.wifi_ssid[0] = '\0';
    len = sizeof(config.wifi_pass);
    if (nvs_get_str(handle, "wifi_pass", config.wifi_pass, &len) != ESP_OK) config.wifi_pass[0] = '\0';
    len = sizeof(config.timezone);
    if (nvs_get_str(handle, "timezone", config.timezone, &len) != ESP_OK) strcpy(config.timezone, "UTC0");

    nvs_close(handle);
    PRINT_TIMESTAMPED("Configuración cargada desde NVS");
    return ESP_OK;
}

void print_config(void) {
    printf("\n");
    PRINT_TIMESTAMPED("=== CONFIGURACIÓN ACTUAL ===");
    PRINT_TIMESTAMPED("  sensitivity_threshold : %d / %d muestras", config.sensitivity_threshold, config.sample_window);
    PRINT_TIMESTAMPED("  sample_window         : %d muestras", config.sample_window);
    PRINT_TIMESTAMPED("  read_interval_ms      : %d ms", config.read_interval_ms);
    PRINT_TIMESTAMPED("  hold_time_ms          : %d ms", config.hold_time_ms);
    PRINT_TIMESTAMPED("  show_raw              : %s", config.show_raw ? "ON" : "OFF");
    PRINT_TIMESTAMPED("  wifi_ssid             : %s", strlen(config.wifi_ssid) > 0 ? config.wifi_ssid : "(no configurado)");
    PRINT_TIMESTAMPED("  wifi_pass             : %s", strlen(config.wifi_pass) > 0 ? "********" : "(no configurado)");
    PRINT_TIMESTAMPED("  timezone              : %s", config.timezone);
    PRINT_TIMESTAMPED("  hora sincronizada     : %s", s_time_synced ? "SI" : "NO");
    PRINT_TIMESTAMPED("  detecciones totales   : %d", detection_count);
    PRINT_TIMESTAMPED("============================");
    printf("\n");
}

void print_help(void) {
    printf("\n");
    PRINT_TIMESTAMPED("=== COMANDOS DISPONIBLES ===");
    PRINT_TIMESTAMPED("  help                    - Muestra esta ayuda");
    PRINT_TIMESTAMPED("  status                  - Muestra configuración actual");
    PRINT_TIMESTAMPED("  set sens <1-N>          - Cambia umbral de sensibilidad");
    PRINT_TIMESTAMPED("  set window <1-20>       - Cambia ventana de muestreo");
    PRINT_TIMESTAMPED("  set interval <ms>       - Cambia intervalo de lectura");
    PRINT_TIMESTAMPED("  set hold <ms>           - Cambia tiempo de retención activo");
    PRINT_TIMESTAMPED("  set raw <0|1>           - Activa/desactiva lecturas raw");
    PRINT_TIMESTAMPED("  set wifi_ssid <nombre>  - Configura SSID WiFi");
    PRINT_TIMESTAMPED("  set wifi_pass <clave>   - Configura password WiFi");
    PRINT_TIMESTAMPED("  set timezone <TZ>       - Configura zona horaria (ej: CST6, UTC0)");
    PRINT_TIMESTAMPED("  time_status             - Muestra estado de sincronización NTP");
    PRINT_TIMESTAMPED("  save                    - Guarda configuración en NVS");
    PRINT_TIMESTAMPED("  load                    - Carga configuración desde NVS");
    PRINT_TIMESTAMPED("  reset                   - Restaura valores por defecto");
    PRINT_TIMESTAMPED("  reboot                  - Reinicia el ESP32");
    PRINT_TIMESTAMPED("============================");
    printf("\n");
}

void sensor_task(void *pvParameters) {
    samples = (int *)malloc(config.sample_window * sizeof(int));
    for (int i = 0; i < config.sample_window; i++) samples[i] = 0;

    int sample_index = 0;
    int filtered_state = 0;
    int last_filtered_state = 0;
    TickType_t hold_until = 0;

    while (1) {
        int current_state = gpio_get_level(SENSOR_PIN);

        if (config.show_raw) {
            PRINT_TIMESTAMPED("[RAW] %d", current_state);
        }

        if (sample_index >= config.sample_window) sample_index = 0;
        samples[sample_index] = current_state;
        sample_index = (sample_index + 1) % config.sample_window;

        int sum = 0;
        for (int i = 0; i < config.sample_window; i++) {
            sum += samples[i];
        }

        filtered_state = (sum >= config.sensitivity_threshold) ? 1 : 0;

        if (filtered_state == 1) {
            hold_until = xTaskGetTickCount() + pdMS_TO_TICKS(config.hold_time_ms);
        }
        int active_state = (xTaskGetTickCount() < hold_until) ? 1 : 0;

        if (active_state == 1 && last_filtered_state == 0) {
            detection_count++;
            PRINT_TIMESTAMPED("¡Movimiento detectado! Total: %d (hold %d ms)", detection_count, config.hold_time_ms);
        }
        last_filtered_state = active_state;

        vTaskDelay(pdMS_TO_TICKS(config.read_interval_ms));
    }
}

void uart_cmd_task(void *pvParameters) {
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, RD_BUF_SIZE, 0, 0, NULL, 0));

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    int pos = 0;

    while (1) {
        int len = uart_read_bytes(UART_NUM, data + pos, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (data[pos] == '\n' || data[pos] == '\r') {
                data[pos] = '\0';
                if (pos > 0) {
                    char *cmd = (char *)data;
                    char *arg1 = NULL, *arg2 = NULL;

                    char *tok = strtok(cmd, " ");
                    if (tok) {
                        if (strcmp(tok, "help") == 0) {
                            print_help();
                        } else if (strcmp(tok, "status") == 0) {
                            print_config();
                        } else if (strcmp(tok, "save") == 0) {
                            save_config_to_nvs();
                        } else if (strcmp(tok, "load") == 0) {
                            load_config_from_nvs();
                            print_config();
                        } else if (strcmp(tok, "reset") == 0) {
                            config.sensitivity_threshold = 3;
                            config.sample_window = 5;
                            config.read_interval_ms = 100;
                            config.hold_time_ms = 2500;
                            config.show_raw = 0;
                            PRINT_TIMESTAMPED("Configuración restaurada a valores por defecto");
                            print_config();
                        } else if (strcmp(tok, "time_status") == 0) {
                            PRINT_TIMESTAMPED("Hora sincronizada: %s", s_time_synced ? "SI" : "NO");
                            PRINT_TIMESTAMPED("WiFi conectado: %s", 
                                (s_wifi_event_group && (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)) ? "SI" : "NO");
                            struct timeval tv;
                            gettimeofday(&tv, NULL);
                            struct tm *tm_info = localtime(&tv.tv_sec);
                            char timebuf[64];
                            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
                            PRINT_TIMESTAMPED("Hora actual del sistema: %s", timebuf);
                        } else if (strcmp(tok, "reboot") == 0) {
                            PRINT_TIMESTAMPED("Reiniciando en 1 segundo...");
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            esp_restart();
                        } else if (strcmp(tok, "set") == 0) {
                            arg1 = strtok(NULL, " ");
                            if (arg1) {
                                if (strcmp(arg1, "wifi_ssid") == 0 || strcmp(arg1, "wifi_pass") == 0 || strcmp(arg1, "timezone") == 0) {
                                    char *rest = strtok(NULL, "");
                                    if (rest) {
                                        int l = strlen(rest);
                                        while (l > 0 && (rest[l-1] == ' ' || rest[l-1] == '\n' || rest[l-1] == '\r')) {
                                            rest[--l] = '\0';
                                        }
                                        if (strcmp(arg1, "wifi_ssid") == 0) {
                                            strncpy(config.wifi_ssid, rest, sizeof(config.wifi_ssid) - 1);
                                            PRINT_TIMESTAMPED("wifi_ssid = %s", config.wifi_ssid);
                                        } else if (strcmp(arg1, "wifi_pass") == 0) {
                                            strncpy(config.wifi_pass, rest, sizeof(config.wifi_pass) - 1);
                                            PRINT_TIMESTAMPED("wifi_pass = ********");
                                        } else if (strcmp(arg1, "timezone") == 0) {
                                            strncpy(config.timezone, rest, sizeof(config.timezone) - 1);
                                            setenv("TZ", config.timezone, 1);
                                            tzset();
                                            PRINT_TIMESTAMPED("timezone = %s", config.timezone);
                                        }
                                    }
                                } else {
                                    arg2 = strtok(NULL, " ");
                                    if (arg2) {
                                        int val = atoi(arg2);
                                        if (strcmp(arg1, "sens") == 0) {
                                            if (val >= 1 && val <= config.sample_window) {
                                                config.sensitivity_threshold = val;
                                                PRINT_TIMESTAMPED("sensitivity_threshold = %d", val);
                                            } else PRINT_TIMESTAMPED("Error: valor debe estar entre 1 y %d", config.sample_window);
                                        } else if (strcmp(arg1, "window") == 0) {
                                            if (val >= 1 && val <= 20) {
                                                config.sample_window = val;
                                                PRINT_TIMESTAMPED("sample_window = %d", val);
                                            } else PRINT_TIMESTAMPED("Error: valor debe estar entre 1 y 20");
                                        } else if (strcmp(arg1, "interval") == 0) {
                                            if (val >= 10 && val <= 5000) {
                                                config.read_interval_ms = val;
                                                PRINT_TIMESTAMPED("read_interval_ms = %d", val);
                                            } else PRINT_TIMESTAMPED("Error: valor debe estar entre 10 y 5000");
                                        } else if (strcmp(arg1, "hold") == 0) {
                                            if (val >= 0 && val <= 60000) {
                                                config.hold_time_ms = val;
                                                PRINT_TIMESTAMPED("hold_time_ms = %d", val);
                                            } else PRINT_TIMESTAMPED("Error: valor debe estar entre 0 y 60000");
                                        } else if (strcmp(arg1, "raw") == 0) {
                                            if (val == 0 || val == 1) {
                                                config.show_raw = val;
                                                PRINT_TIMESTAMPED("show_raw = %s", val ? "ON" : "OFF");
                                            } else PRINT_TIMESTAMPED("Error: valor debe ser 0 o 1");
                                        } else {
                                            PRINT_TIMESTAMPED("Parámetro desconocido: %s", arg1);
                                        }
                                    } else {
                                        PRINT_TIMESTAMPED("Uso: set <param> <valor>");
                                    }
                                }
                            } else {
                                PRINT_TIMESTAMPED("Uso: set <param> <valor>");
                            }
                        } else {
                            PRINT_TIMESTAMPED("Comando desconocido. Escribe 'help'");
                        }
                    }
                    pos = 0;
                }
            } else {
                pos++;
                if (pos >= BUF_SIZE - 1) pos = 0;
            }
        }
    }
}

void app_main(void) {
    start_time_us = esp_timer_get_time();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    load_config_from_nvs();

    PRINT_TIMESTAMPED("Sensor HC-SR501 inicializado en GPIO%d", SENSOR_PIN);
    PRINT_TIMESTAMPED("Escribe 'help' para ver los comandos disponibles");
    print_config();

    // Iniciar WiFi y SNTP si hay credenciales
    if (strlen(config.wifi_ssid) > 0) {
        wifi_init_sta();
        xTaskCreate(sntp_init_task, "sntp_task", 4096, NULL, 5, NULL);
    } else {
        PRINT_TIMESTAMPED("WiFi no configurado. Timestamp usará uptime.");
        PRINT_TIMESTAMPED("Configura WiFi con: set wifi_ssid <nombre>");
        PRINT_TIMESTAMPED("                    set wifi_pass <clave>");
        PRINT_TIMESTAMPED("                    save");
        PRINT_TIMESTAMPED("                    reboot");
    }

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_cmd_task, "uart_cmd_task", 4096, NULL, 5, NULL);
}
