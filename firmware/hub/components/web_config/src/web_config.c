/*
 * web_config.c
 * Servidor HTTP de provisioning.
 * [HUB-F-030] [AT-05] Endpoints: GET /, POST /save, GET /status
 */
#include "web_config.h"
#include "sm_prov.h"
#include "nvs_manager.h"
#include "index_html.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "web_cfg";
static httpd_handle_t s_server = NULL;

/* ---------- Helpers de URL-decode ---------- */
static char hex2c(char a, char b)
{
    int v = 0;
    if (a >= '0' && a <= '9') v = (a - '0') << 4;
    else if (a >= 'a' && a <= 'f') v = (a - 'a' + 10) << 4;
    else if (a >= 'A' && a <= 'F') v = (a - 'A' + 10) << 4;
    if (b >= '0' && b <= '9') v |= (b - '0');
    else if (b >= 'a' && b <= 'f') v |= (b - 'a' + 10);
    else if (b >= 'A' && b <= 'F') v |= (b - 'A' + 10);
    return (char)v;
}

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t i = 0;
    while (*src && i + 1 < dst_size) {
        if (*src == '%' && src[1] && src[2]) {
            dst[i++] = hex2c(src[1], src[2]);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

/* Busca valor de una clave en body application/x-www-form-urlencoded */
static bool find_kv(const char *body, const char *key, char *out, size_t out_sz)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || *(p - 1) == '&') && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *end = strchr(v, '&');
            size_t vlen = end ? (size_t)(end - v) : strlen(v);
            char tmp[256];
            if (vlen >= sizeof(tmp)) vlen = sizeof(tmp) - 1;
            memcpy(tmp, v, vlen);
            tmp[vlen] = '\0';
            url_decode(out, tmp, out_sz);
            return true;
        }
        p += klen + 1;
    }
    return false;
}

/* ---------- Handlers ---------- */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state",
                            sm_prov_state_name(sm_prov_get_state()));
    cJSON_AddNumberToObject(root, "state_id", (int)sm_prov_get_state());

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char macs[18];
    snprintf(macs, sizeof(macs), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "device_mac", macs);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received,
                                 req->content_len - received);
        if (ret <= 0) { free(body); return ESP_FAIL; }
        received += ret;
    }
    body[req->content_len] = '\0';

    char ssid[33] = {0}, pass[65] = {0};
    char tid[33] = {0}, sid[33] = {0};
    char muser[65] = {0}, mpass[65] = {0};

    find_kv(body, "ssid",  ssid,  sizeof(ssid));
    find_kv(body, "pass",  pass,  sizeof(pass));
    find_kv(body, "tid",   tid,   sizeof(tid));
    find_kv(body, "sid",   sid,   sizeof(sid));
    find_kv(body, "muser", muser, sizeof(muser));
    find_kv(body, "mpass", mpass, sizeof(mpass));
    free(body);

    /* Validación: SSID requerido */
    if (strlen(ssid) == 0) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "msg", "SSID requerido");
        char *j = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, j, strlen(j));
        free(j); cJSON_Delete(resp);
        return ESP_OK;
    }

    /* Guardar en NVS [HUB-F-030] */
    esp_err_t err = nvs_manager_set_wifi(ssid, pass);
    if (err == ESP_OK) {
        err = nvs_manager_set_site(tid[0] ? tid : "0", sid[0] ? sid : "site0");
    }
    if (err == ESP_OK && (muser[0] || mpass[0])) {
        err = nvs_manager_set_mqtt_auth(muser, mpass);
    }

    cJSON *resp = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(resp, "status", "ok");
        char *j = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, j, strlen(j));
        free(j); cJSON_Delete(resp);
        ESP_LOGI(TAG, "Creds: ssid='%s'(len=%d) pass_len=%d mqtt_user='%s'", ssid, (int)strlen(ssid), (int)strlen(pass), muser);
        ESP_LOGI(TAG, "Credenciales guardadas. Programando validacion...");
        /* Notificar a la FSM para que avance a VALIDATE */
        sm_prov_on_credentials_received();
    } else {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "msg", esp_err_to_name(err));
        char *j = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, j, strlen(j));
        free(j); cJSON_Delete(resp);
    }
    return ESP_OK;
}

/* ---------- API pública ---------- */
esp_err_t web_config_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 5;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error arrancando HTTPD: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler
    };
    httpd_uri_t uri_save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post_handler
    };
    httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = status_get_handler
    };
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_save);
    httpd_register_uri_handler(s_server, &uri_status);

    ESP_LOGI(TAG, "HTTP servidor en http://192.168.4.1");
    dns_server_start();
    return ESP_OK;
}

esp_err_t web_config_stop(void)
{
    dns_server_stop();
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}
