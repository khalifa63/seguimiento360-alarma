#include "nvs_manager.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS_MGR";

static esp_err_t init_partition(const char *part_name)
{
    esp_err_t ret = nvs_flash_init_partition(part_name);

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Partición '%s' corrupta, formateando...", part_name);
        const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, part_name);
        if (part == NULL) {
            ESP_LOGE(TAG, "Partición '%s' no encontrada", part_name);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_ERROR_CHECK(nvs_flash_erase_partition_ptr(part));
        ret = nvs_flash_init_partition(part_name);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error init '%s': %s", part_name, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partición '%s' inicializada", part_name);
    }
    return ret;
}

esp_err_t nvs_manager_init(void)
{
    esp_err_t ret;

    ret = init_partition(NVS_PART_CONFIG);
    if (ret != ESP_OK) return ret;

    ret = init_partition(NVS_PART_STATE);
    if (ret != ESP_OK) return ret;

    ret = init_partition(NVS_PART_NODES);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "3 particiones NVS OK");
    return ESP_OK;
}

esp_err_t nvs_manager_set_device_id(const char *dev_id)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_CONFIG,
                                            NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(h, NVS_KEY_DEV_ID, dev_id);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Device ID guardado: %s", dev_id);
    }
    return ret;
}

esp_err_t nvs_manager_get_device_id(char *dev_id, size_t max_len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_CONFIG,
                                            NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;

    size_t required = max_len;
    ret = nvs_get_str(h, NVS_KEY_DEV_ID, dev_id, &required);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_set_tenant_id(uint32_t tenant_id)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_CONFIG,
                                            NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u32(h, NVS_KEY_TENANT_ID, tenant_id);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_get_tenant_id(uint32_t *tenant_id)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_CONFIG,
                                            NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_u32(h, NVS_KEY_TENANT_ID, tenant_id);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_set_alarm_state(uint8_t state)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open_from_partition(NVS_PART_STATE, NVS_NS_STATE,
                                            NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u8(h, NVS_KEY_ALARM_STATE, state);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_get_alarm_state(uint8_t *state)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open_from_partition(NVS_PART_STATE, NVS_NS_STATE,
                                            NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_u8(h, NVS_KEY_ALARM_STATE, state);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_emergency_commit(void)
{
    ESP_LOGW(TAG, "Emergency commit (placeholder)");
    return ESP_OK;
}

esp_err_t nvs_manager_factory_reset(void)
{
    ESP_LOGW(TAG, "=== FACTORY RESET ===");

    const char *parts[] = { NVS_PART_CONFIG, NVS_PART_STATE, NVS_PART_NODES };
    for (int i = 0; i < 3; i++) {
        const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, parts[i]);
        if (p != NULL) {
            nvs_flash_erase_partition_ptr(p);
            ESP_LOGI(TAG, "Partición '%s' borrada", parts[i]);
        }
    }
    return ESP_OK;
}

/* =========================================================
 * M4: Provisioning — credenciales WiFi / Site / MQTT auth
 * [HUB-F-030] Namespace 'hub_cfg' en partición 'nvs' (config)
 * ========================================================= */
#define NVS_NS_HUB_CFG   "hub_cfg"
#define K_WIFI_SSID  "wifi_ssid"
#define K_WIFI_PASS  "wifi_pass"
#define K_SITE_TID   "site_tid"
#define K_SITE_SID   "site_sid"
#define K_MQTT_USER  "mqtt_user"
#define K_MQTT_PASS  "mqtt_pass"

esp_err_t nvs_manager_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, K_WIFI_SSID, ssid);
    if (err == ESP_OK && pass && strlen(pass) > 0) {
        err = nvs_set_str(h, K_WIFI_PASS, pass);
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_manager_get_wifi(char *ssid, size_t ssid_len,
                               char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t l = ssid_len;
    err = nvs_get_str(h, K_WIFI_SSID, ssid, &l);
    if (err != ESP_OK) { nvs_close(h); return err; }
    if (pass) {
        l = pass_len;
        esp_err_t e2 = nvs_get_str(h, K_WIFI_PASS, pass, &l);
        if (e2 == ESP_ERR_NVS_NOT_FOUND && pass_len > 0) pass[0] = '\0';
        else if (e2 != ESP_OK) err = e2;
    }
    nvs_close(h);
    return err;
}

esp_err_t nvs_manager_set_site(const char *tid, const char *sid)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (tid) err = nvs_set_str(h, K_SITE_TID, tid);
    if (err == ESP_OK && sid) err = nvs_set_str(h, K_SITE_SID, sid);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_manager_get_site(char *tid, size_t tid_len,
                               char *sid, size_t sid_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    if (tid) {
        size_t l = tid_len;
        esp_err_t e = nvs_get_str(h, K_SITE_TID, tid, &l);
        if (e == ESP_ERR_NVS_NOT_FOUND && tid_len > 0) tid[0] = '\0';
        else if (e != ESP_OK) err = e;
    }
    if (sid) {
        size_t l = sid_len;
        esp_err_t e = nvs_get_str(h, K_SITE_SID, sid, &l);
        if (e == ESP_ERR_NVS_NOT_FOUND && sid_len > 0) sid[0] = '\0';
        else if (e != ESP_OK && err == ESP_OK) err = e;
    }
    nvs_close(h);
    return err;
}

esp_err_t nvs_manager_set_mqtt_auth(const char *user, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (user) err = nvs_set_str(h, K_MQTT_USER, user);
    if (err == ESP_OK && pass) err = nvs_set_str(h, K_MQTT_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_manager_get_mqtt_auth(char *user, size_t u_len,
                                    char *pass, size_t p_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    if (user) {
        size_t l = u_len;
        esp_err_t e = nvs_get_str(h, K_MQTT_USER, user, &l);
        if (e == ESP_ERR_NVS_NOT_FOUND && u_len > 0) user[0] = '\0';
        else if (e != ESP_OK) err = e;
    }
    if (pass) {
        size_t l = p_len;
        esp_err_t e = nvs_get_str(h, K_MQTT_PASS, pass, &l);
        if (e == ESP_ERR_NVS_NOT_FOUND && p_len > 0) pass[0] = '\0';
        else if (e != ESP_OK && err == ESP_OK) err = e;
    }
    nvs_close(h);
    return err;
}

bool nvs_manager_has_credentials(void)
{
    char ssid[33] = {0};
    esp_err_t err = nvs_manager_get_wifi(ssid, sizeof(ssid), NULL, 0);
    return (err == ESP_OK && ssid[0] != '\0');
}

esp_err_t nvs_manager_factory_reset_credentials(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PART_CONFIG, NVS_NS_HUB_CFG,
                                            NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "Factory reset de credenciales: %s", esp_err_to_name(err));
    return err;
}
