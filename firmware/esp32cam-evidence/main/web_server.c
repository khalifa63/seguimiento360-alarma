#include "web_server.h"
#include "config.h"
#include "sd_card.h"
#include "camera.h"
#include "tokens.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

static esp_err_t health_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"service\":\"esp32cam-evidence\"}");
    return ESP_OK;
}

static esp_err_t sd_status_handler(httpd_req_t* req) {
    char buf[512];
    sd_card_report_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t sd_recover_handler(httpd_req_t* req) {
    // Verificar token cámara
    char token_buf[64];
    size_t len = httpd_req_get_hdr_value_len(req, "X-Camera-Token");
    if (len > 0 && len < sizeof(token_buf)) {
        httpd_req_get_hdr_value_str(req, "X-Camera-Token", token_buf, sizeof(token_buf));
        if (strcmp(token_buf, CAMERA_TOKEN) != 0) {
            httpd_resp_set_status(req, "401");
            httpd_resp_sendstr(req, "{\"ok\":false}");
            return ESP_OK;
        }
    } else {
        httpd_resp_set_status(req, "401");
        httpd_resp_sendstr(req, "{\"ok\":false}");
        return ESP_OK;
    }

    const app_config_t* cfg = config_get();
    if (cfg->capture_on_event || cfg->manual_capture) {
        // No bloquear por captura en curso en diagnóstico local
    }

    esp_err_t err = sd_card_recover();
    char buf[512];
    sd_card_report_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, err == ESP_OK ? "200" : "503");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t clip_status_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"recording\":false,\"uploading\":false,\"clips\":0}");
    return ESP_OK;
}

static esp_err_t config_handler(httpd_req_t* req) {
    char buf[2048];
    config_to_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return ESP_FAIL;
    }

    httpd_uri_t uris[] = {
        { .uri = "/health", .method = HTTP_GET, .handler = health_handler },
        { .uri = "/sd-status", .method = HTTP_GET, .handler = sd_status_handler },
        { .uri = "/sd-recover", .method = HTTP_POST, .handler = sd_recover_handler },
        { .uri = "/clip-status", .method = HTTP_GET, .handler = clip_status_handler },
        { .uri = "/config", .method = HTTP_GET, .handler = config_handler },
    };

    for (size_t i = 0; i < sizeof(uris)/sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI handler");
            httpd_stop(server);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Web server started on port 80");
    return ESP_OK;
}

void web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}