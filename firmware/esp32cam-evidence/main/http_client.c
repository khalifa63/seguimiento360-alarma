#include "http_client.h"
#include "config.h"
#include "tokens.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "HTTP_CLIENT";

static const char* upload_path = "/api/upload";
static const char* status_path = "/api/camera-status";
static const char* command_path = "/api/capture-command";

esp_err_t http_client_init(void) {
    ESP_LOGI(TAG, "HTTP client initialized, host=%s", HOST);
    return ESP_OK;
}

esp_err_t http_upload_frame(const char* operation_id, const camera_fb_t* fb) {
    if (!operation_id || !fb || !fb->buf) return ESP_ERR_INVALID_ARG;

    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", HOST, upload_path);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 4000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    esp_http_client_set_header(client, "X-Camera-Token", CAMERA_TOKEN);
    esp_http_client_set_header(client, "X-Operation-Id", operation_id);
    esp_http_client_set_post_field(client, (const char*)fb->buf, fb->len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 201) {
        ESP_LOGI(TAG, "Upload OK: op=%s bytes=%u", operation_id, fb->len);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Upload failed: err=%s status=%d", esp_err_to_name(err), status);
    return ESP_FAIL;
}

esp_err_t http_report_status(const char* operation_id, const char* kind,
                             const char* state, const char* message,
                             uint16_t current, uint16_t total, bool uploads_confirmed) {
    if (!operation_id || !kind || !state) return ESP_ERR_INVALID_ARG;

    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", HOST, status_path);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "operationId", operation_id);
    cJSON_AddStringToObject(root, "kind", kind);
    cJSON_AddStringToObject(root, "state", state);
    if (message) cJSON_AddStringToObject(root, "message", message);
    cJSON_AddNumberToObject(root, "current", current);
    cJSON_AddNumberToObject(root, "total", total);
    cJSON_AddBoolToObject(root, "uploadsConfirmed", uploads_confirmed);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    char url_full[128];
    snprintf(url_full, sizeof(url_full), "https://%s%s", HOST, status_path);

    esp_http_client_config_t config = {
        .url = url_full,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 4000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(json); return ESP_FAIL; }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Camera-Token", CAMERA_TOKEN);
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(json);

    if (err == ESP_OK && (status == 200 || status == 409)) {
        ESP_LOGI(TAG, "Status report OK: op=%s state=%s code=%d", operation_id, state, status);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Status report failed: err=%s status=%d", esp_err_to_name(err), status);
    return ESP_FAIL;
}

bool http_poll_command(char* operation_id, size_t opid_size, char* command, size_t cmd_size) {
    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", HOST, command_path);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 4000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "X-Camera-Token", CAMERA_TOKEN);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK || status != 200) {
        esp_http_client_cleanup(client);
        return false;
    }

    int len = esp_http_client_read(client, (char*)esp_http_client_get_response_buffer(client), esp_http_client_get_response_buffer_size(client));
    esp_http_client_cleanup(client);
    if (len <= 0) return false;

    char* buf = malloc(len + 1);
    if (!buf) return false;
    memcpy(buf, esp_http_client_get_response_buffer(client), len);
    buf[len] = '\0';

    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    cJSON* opid = cJSON_GetObjectItem(root, "operationId");
    cJSON* cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(opid) || !cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return false;
    }

    strncpy(operation_id, opid->valuestring, opid_size - 1);
    operation_id[opid_size - 1] = '\0';
    strncpy(command, cmd->valuestring, cmd_size - 1);
    command[cmd_size - 1] = '\0';

    cJSON_Delete(root);
    return true;
}

esp_err_t wifi_init_from_config(void) {
    const app_config_t* cfg = config_get();
    if (!cfg->wifi_ssid[0]) {
        ESP_LOGW(TAG, "No WiFi SSID configured");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", cfg->wifi_ssid);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, cfg->wifi_pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started, waiting for connection...");
    return ESP_OK;
}