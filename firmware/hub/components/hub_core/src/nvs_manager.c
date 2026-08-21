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
