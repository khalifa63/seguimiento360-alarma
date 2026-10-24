/*
 * ota_node.c — Actualizacion del nodo por el aire (FW-012)
 *
 * Secuencia:
 *   1. El hub manda un frame ESPNOW_FT_CMD con WiFi y URL del servidor OTA.
 *   2. El nodo se asocia a esa red. Hasta ahora el radio solo servia para
 *      ESP-NOW; se anade la asociacion sin tocar el canal, que debe seguir
 *      siendo el mismo o se pierde el enlace con el hub.
 *   3. Descarga por HTTPS y verifica la firma RSA-3072.
 *   4. Escribe en la particion inactiva y reinicia.
 *
 * La firma la valida esp_https_ota antes de escribir: una imagen sin firmar
 * valida devuelve ESP_ERR_OTA_VALIDATE_FAILED y no llega a arrancar.
 */
#include "ota_node.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_secure_boot.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "OTA_NODE";

/* El canal de ESP-NOW es fijo y compartido con el hub. Al asociarse a un AP
 * el radio salta al canal del AP, lo que romperia el enlace. Se comprueba
 * antes de empezar y se aborta si no coinciden. */
#define OTA_NODE_REQUIRED_CHANNEL   5
#define OTA_WIFI_TIMEOUT_MS         20000
#define OTA_NVS_NS                  "s360node"
#define OTA_NVS_KEY_PENDING         "ota_pending"

static volatile bool s_in_progress = false;
static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILED_BIT     BIT1

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

#define OTA_CONNECT_RETRIES   3

static esp_err_t ota_node_connect_wifi(const char *ssid, const char *password)
{
    /* El grupo de eventos y los handlers se preparan ANTES del escaneo.
     *
     * Estaban despues, y el escaneo dejaba eventos que nadie recogia: la
     * primera asociacion tras arrancar fallaba casi siempre y solo funcionaba
     * al reintentar. Un OTA no puede depender de acertar a la segunda. */
    esp_wifi_disconnect();
    if (s_wifi_events != NULL) {
        vEventGroupDelete(s_wifi_events);
        s_wifi_events = NULL;
    }
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    static bool handlers_registered = false;
    if (!handlers_registered) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   &wifi_event_handler, NULL));
        handlers_registered = true;
    }

    /* Comprobar el canal del AP antes de asociarse. Si el AP esta en otro
     * canal, la conexion moveria el radio y el nodo perderia el hub. */
    wifi_ap_record_t ap = {0};
    wifi_scan_config_t scan = { .ssid = (uint8_t *)ssid, .show_hidden = true };
    esp_err_t serr = esp_wifi_scan_start(&scan, true);
    if (serr == ESP_OK) {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > 0 && esp_wifi_scan_get_ap_records(&n, &ap) == ESP_OK) {
            if (ap.primary != OTA_NODE_REQUIRED_CHANNEL) {
                ESP_LOGE(TAG, "El AP '%s' esta en el canal %u y ESP-NOW usa el %d. "
                              "Se aborta: asociarse perderia el enlace con el hub.",
                         ssid, ap.primary, OTA_NODE_REQUIRED_CHANNEL);
                return ESP_ERR_INVALID_STATE;
            }
            ESP_LOGI(TAG, "AP '%s' en canal %u, compatible con ESP-NOW.",
                     ssid, ap.primary);
        }
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    /* Varios intentos: la primera asociacion tras un arranque puede fallar
     * por razones transitorias del radio (calibracion, canal, scan previo). */
    esp_err_t err = ESP_FAIL;
    for (int intento = 1; intento <= OTA_CONNECT_RETRIES; intento++) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Intento %d: esp_wifi_connect: %s",
                     intento, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                               WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                               pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(OTA_WIFI_TIMEOUT_MS));
        if (bits & WIFI_CONNECTED_BIT) {
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Intento %d de %d sin exito.", intento, OTA_CONNECT_RETRIES);
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "No se pudo asociar a '%s' tras %d intentos",
             ssid, OTA_CONNECT_RETRIES);
    return ESP_ERR_TIMEOUT;
}

/* Marca en NVS que hay una actualizacion pendiente de confirmar. Si el nodo
 * se reinicia antes de confirmarla, al arrancar sabra que esta sin validar. */
static void ota_node_set_pending(bool pending)
{
    nvs_handle_t h;
    if (nvs_open(OTA_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (pending) {
        nvs_set_u8(h, OTA_NVS_KEY_PENDING, 1);
    } else {
        nvs_erase_key(h, OTA_NVS_KEY_PENDING);
    }
    nvs_commit(h);
    nvs_close(h);
}

static void ota_node_task(void *arg)
{
    char *ssid = NULL, *pass = NULL, *url = NULL;
    /* El payload llega en un buffer que liberamos al final; se copia aqui
     * para no depender de la vida del frame recibido. */
    char *packed = (char *)arg;
    if (packed != NULL) {
        ssid = packed;
        pass = packed + 33;   /* ESPNOW_WIFI_SSID_MAX */
        url  = packed + 98;   /* 33 + 65 */
    }

    ESP_LOGI(TAG, "Actualizacion solicitada: %s", url ? url : "(sin URL)");

    /* Con que claves se verificara la imagen.
     *
     * Con SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT no hay clave compilada: la
     * verificacion usa los digests de la app EN EJECUCION. Si esta no esta
     * firmada, esp_secure_boot_init_checks() aborta al arrancar, asi que si
     * el nodo llega hasta aqui es que la clave esta disponible. Se registra
     * cual es, porque sin este dato un rechazo no es diagnosticable. */
    esp_image_sig_public_key_digests_t digs = {0};
    if (esp_secure_boot_get_signature_blocks_for_running_app(true, &digs) == ESP_OK) {
        ESP_LOGI(TAG, "Claves de verificacion disponibles: %u", digs.num_digests);
        for (unsigned i = 0; i < digs.num_digests; i++) {
            char hex[2 * ESP_SECURE_BOOT_DIGEST_LEN + 1];
            for (unsigned b = 0; b < ESP_SECURE_BOOT_DIGEST_LEN; b++) {
                snprintf(hex + b * 2, 3, "%02x", digs.key_digests[i][b]);
            }
            ESP_LOGI(TAG, "  clave[%u] sha256=%s", i, hex);
        }
    } else {
        ESP_LOGW(TAG, "No se pudieron leer los digests de la app en ejecucion.");
    }

    esp_err_t err = ota_node_connect_wifi(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sin WiFi no hay OTA: %s", esp_err_to_name(err));
        goto fin;
    }
    ESP_LOGI(TAG, "Conectado a '%s'", ssid);

    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* 700 KB del nodo: menos que el hub, pero margen igual. */
        .timeout_ms = 60000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http };

    esp_https_ota_handle_t h = NULL;
    err = esp_https_ota_begin(&ota_cfg, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin: %s", esp_err_to_name(err));
        goto fin;
    }

    int last_pct = -1;
    while (1) {
        err = esp_https_ota_perform(h);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int total = esp_https_ota_get_image_size(h);
        int done  = esp_https_ota_get_image_len_read(h);
        if (total > 0) {
            int pct = (done * 100) / total;
            if (pct >= last_pct + 20) {
                last_pct = pct;
                ESP_LOGI(TAG, "Descarga %d%% (%d/%d)", pct, done, total);
            }
        }
    }

    if (!esp_https_ota_is_complete_data_received(h)) {
        ESP_LOGE(TAG, "Descarga incompleta");
        esp_https_ota_abort(h);
        goto fin;
    }

    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        /* ESP_ERR_OTA_VALIDATE_FAILED aqui significa firma invalida: la
         * imagen se descarto y la particion activa sigue intacta. */
        ESP_LOGE(TAG, "OTA rechazado: %s", esp_err_to_name(err));
        esp_https_ota_abort(h);
        goto fin;
    }

    ota_node_set_pending(true);
    ESP_LOGI(TAG, "OTA aplicado. Reiniciando en 3 s...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

fin:
    free(packed);
    s_in_progress = false;
    vTaskDelete(NULL);
}

esp_err_t ota_node_start_from_config(const char *ssid, const char *password,
                                     const char *url)
{
    if (ssid == NULL || url == NULL || strlen(ssid) == 0 || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_in_progress) {
        ESP_LOGW(TAG, "Ya hay una actualizacion en curso");
        return ESP_ERR_INVALID_STATE;
    }

    /* Se copia el payload completo a un bloque propio: la tarea lo libera. */
    size_t need = 33 + 65 + 96;
    char *packed = calloc(1, need);
    if (packed == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(packed,      ssid,                    32);
    strncpy(packed + 33, password ? password : "", 64);
    strncpy(packed + 98, url,                      95);

    s_in_progress = true;
    if (xTaskCreate(ota_node_task, "ota_node", 8192, packed, 5, NULL) != pdPASS) {
        free(packed);
        s_in_progress = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ota_node_in_progress(void)
{
    return s_in_progress;
}
