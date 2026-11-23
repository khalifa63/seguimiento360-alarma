#include "config.h"
#include "camera.h"
#include "sd_card.h"
#include "tokens.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "CONFIG";
static const char* NVS_NAMESPACE = "camproto";
static const char* NVS_KEY_CONFIG = "config";

static app_config_t current_config = {
    .wifi_ssid = "",
    .wifi_pass = "",
    .burst_count = 5,
    .burst_interval_ms = 1000,
    .clip_interval_ms = 1000,
    .clip_duration_sec = 30,
    .capture_on_event = true,
    .manual_capture = true,
    .active_profile = 1,  // night por defecto
    .profiles = {
        // Day
        { .frame_size = FRAMESIZE_SVGA, .jpeg_quality = 10, .flash_enabled = false, .flash_for_clips = false,
          .flash_duration_ms = 120, .low_light_mode = "auto", .gain_ceiling = 2,
          .exposure_mode = "auto", .exposure_value = 200, .sensor_gain = 4 },
        // Night
        { .frame_size = FRAMESIZE_SVGA, .jpeg_quality = 10, .flash_enabled = true, .flash_for_clips = false,
          .flash_duration_ms = 120, .low_light_mode = "auto", .gain_ceiling = 8,
          .exposure_mode = "manual", .exposure_value = 200, .sensor_gain = 4 }
    }
};

static const char* profile_names[2] = {"day", "night"};

esp_err_t config_init(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s, using defaults", esp_err_to_name(err));
        config_apply_active_profile();
        return ESP_OK;
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle, NVS_KEY_CONFIG, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No config in NVS, using defaults (legacy migration to night)");
        config_apply_active_profile();
        nvs_close(handle);
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS get size failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    char* json_buf = malloc(required_size + 1);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM reading config");
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, NVS_KEY_CONFIG, json_buf, &required_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        free(json_buf);
        return err;
    }
    json_buf[required_size] = '\0';

    cJSON* root = cJSON_Parse(json_buf);
    free(json_buf);
    if (!root) {
        ESP_LOGE(TAG, "Config JSON parse failed");
        return ESP_ERR_INVALID_ARG;
    }

    // Migración legacy: si no hay profiles/active_profile, clona a night y crea day defaults
    cJSON* profiles = cJSON_GetObjectItem(root, "profiles");
    cJSON* active = cJSON_GetObjectItem(root, "activeProfile");

    if (!profiles || !active) {
        ESP_LOGI(TAG, "Legacy config detected, migrating to day/night profiles");
        // Los valores actuales ya son defaults night; persistir estructura nueva
        config_persist();
        cJSON_Delete(root);
        return ESP_OK;
    }

    // Cargar activeProfile
    if (cJSON_IsString(active)) {
        if (strcmp(active->valuestring, "day") == 0) current_config.active_profile = 0;
        else if (strcmp(active->valuestring, "night") == 0) current_config.active_profile = 1;
    }

    // Cargar perfiles
    for (int i = 0; i < 2; i++) {
        cJSON* p = cJSON_GetObjectItem(profiles, profile_names[i]);
        if (cJSON_IsObject(p)) {
            capture_profile_t* prof = &current_config.profiles[i];
            cJSON* item;

            item = cJSON_GetObjectItem(p, "frameSize");
            if (cJSON_IsString(item)) {
                if (strcmp(item->valuestring, "QQVGA") == 0) prof->frame_size = FRAMESIZE_QQVGA;
                else if (strcmp(item->valuestring, "QVGA") == 0) prof->frame_size = FRAMESIZE_QVGA;
                else if (strcmp(item->valuestring, "VGA") == 0) prof->frame_size = FRAMESIZE_VGA;
                else prof->frame_size = FRAMESIZE_SVGA;
            }

            item = cJSON_GetObjectItem(p, "jpegQuality");
            if (cJSON_IsNumber(item)) prof->jpeg_quality = item->valueint;

            item = cJSON_GetObjectItem(p, "flashEnabled");
            if (cJSON_IsBool(item)) prof->flash_enabled = cJSON_IsTrue(item);

            item = cJSON_GetObjectItem(p, "flashForClips");
            if (cJSON_IsBool(item)) prof->flash_for_clips = cJSON_IsTrue(item);

            item = cJSON_GetObjectItem(p, "flashDurationMs");
            if (cJSON_IsNumber(item)) prof->flash_duration_ms = item->valueint;

            item = cJSON_GetObjectItem(p, "lowLightMode");
            if (cJSON_IsString(item)) prof->low_light_mode = item->valuestring;

            item = cJSON_GetObjectItem(p, "gainCeiling");
            if (cJSON_IsNumber(item)) prof->gain_ceiling = item->valueint;

            item = cJSON_GetObjectItem(p, "exposureMode");
            if (cJSON_IsString(item)) prof->exposure_mode = item->valuestring;

            item = cJSON_GetObjectItem(p, "exposureValue");
            if (cJSON_IsNumber(item)) prof->exposure_value = item->valueint;

            item = cJSON_GetObjectItem(p, "sensorGain");
            if (cJSON_IsNumber(item)) prof->sensor_gain = item->valueint;
        }
    }

    // Globales
    cJSON* item;
    item = cJSON_GetObjectItem(root, "wifiSsid");
    if (cJSON_IsString(item)) strncpy(current_config.wifi_ssid, item->valuestring, 32);
    item = cJSON_GetObjectItem(root, "wifiPassword");
    if (cJSON_IsString(item)) strncpy(current_config.wifi_pass, item->valuestring, 64);
    item = cJSON_GetObjectItem(root, "burstCount");
    if (cJSON_IsNumber(item)) current_config.burst_count = item->valueint;
    item = cJSON_GetObjectItem(root, "burstIntervalMs");
    if (cJSON_IsNumber(item)) current_config.burst_interval_ms = item->valueint;
    item = cJSON_GetObjectItem(root, "clipIntervalMs");
    if (cJSON_IsNumber(item)) current_config.clip_interval_ms = item->valueint;
    item = cJSON_GetObjectItem(root, "clipDurationSeconds");
    if (cJSON_IsNumber(item)) current_config.clip_duration_sec = item->valueint;
    item = cJSON_GetObjectItem(root, "captureOnEvent");
    if (cJSON_IsBool(item)) current_config.capture_on_event = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "manualCapture");
    if (cJSON_IsBool(item)) current_config.manual_capture = cJSON_IsTrue(item);

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Config loaded from NVS, active=%s", profile_names[current_config.active_profile]);
    config_apply_active_profile();
    return ESP_OK;
}

const app_config_t* config_get(void) {
    return &current_config;
}

esp_err_t config_update(const app_config_t* partial) {
    if (!partial) return ESP_ERR_INVALID_ARG;

    // Actualizar campos globales si vienen
    if (partial->wifi_ssid[0]) strncpy(current_config.wifi_ssid, partial->wifi_ssid, 32);
    if (partial->wifi_pass[0]) strncpy(current_config.wifi_pass, partial->wifi_pass, 64);
    if (partial->burst_count) current_config.burst_count = partial->burst_count;
    if (partial->burst_interval_ms) current_config.burst_interval_ms = partial->burst_interval_ms;
    if (partial->clip_interval_ms) current_config.clip_interval_ms = partial->clip_interval_ms;
    if (partial->clip_duration_sec) current_config.clip_duration_sec = partial->clip_duration_sec;
    if (partial->capture_on_event != current_config.capture_on_event) current_config.capture_on_event = partial->capture_on_event;
    if (partial->manual_capture != current_config.manual_capture) current_config.manual_capture = partial->manual_capture;
    if (partial->active_profile >= 0 && partial->active_profile <= 1) current_config.active_profile = partial->active_profile;

    // Actualizar perfiles (fusionar campos válidos)
    for (int i = 0; i < 2; i++) {
        capture_profile_t* dst = &current_config.profiles[i];
        const capture_profile_t* src = &partial->profiles[i];
        if (src->frame_size) dst->frame_size = src->frame_size;
        if (src->jpeg_quality) dst->jpeg_quality = src->jpeg_quality;
        if (src->flash_enabled != dst->flash_enabled) dst->flash_enabled = src->flash_enabled;
        if (src->flash_for_clips != dst->flash_for_clips) dst->flash_for_clips = src->flash_for_clips;
        if (src->flash_duration_ms) dst->flash_duration_ms = src->flash_duration_ms;
        if (src->low_light_mode) dst->low_light_mode = src->low_light_mode;
        if (src->gain_ceiling) dst->gain_ceiling = src->gain_ceiling;
        if (src->exposure_mode) dst->exposure_mode = src->exposure_mode;
        if (src->exposure_value) dst->exposure_value = src->exposure_value;
        if (src->sensor_gain) dst->sensor_gain = src->sensor_gain;
    }

    return config_persist();
}

static esp_err_t config_persist(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifiSsid", current_config.wifi_ssid);
    cJSON_AddStringToObject(root, "wifiPassword", current_config.wifi_pass);
    cJSON_AddNumberToObject(root, "burstCount", current_config.burst_count);
    cJSON_AddNumberToObject(root, "burstIntervalMs", current_config.burst_interval_ms);
    cJSON_AddNumberToObject(root, "clipIntervalMs", current_config.clip_interval_ms);
    cJSON_AddNumberToObject(root, "clipDurationSeconds", current_config.clip_duration_sec);
    cJSON_AddBoolToObject(root, "captureOnEvent", current_config.capture_on_event);
    cJSON_AddBoolToObject(root, "manualCapture", current_config.manual_capture);
    cJSON_AddStringToObject(root, "activeProfile", profile_names[current_config.active_profile]);

    cJSON* profiles = cJSON_CreateObject();
    for (int i = 0; i < 2; i++) {
        cJSON* p = cJSON_CreateObject();
        capture_profile_t* prof = &current_config.profiles[i];
        const char* fs_str = (prof->frame_size == FRAMESIZE_QQVGA) ? "QQVGA" :
                             (prof->frame_size == FRAMESIZE_QVGA) ? "QVGA" :
                             (prof->frame_size == FRAMESIZE_VGA) ? "VGA" : "SVGA";
        cJSON_AddStringToObject(p, "frameSize", fs_str);
        cJSON_AddNumberToObject(p, "jpegQuality", prof->jpeg_quality);
        cJSON_AddBoolToObject(p, "flashEnabled", prof->flash_enabled);
        cJSON_AddBoolToObject(p, "flashForClips", prof->flash_for_clips);
        cJSON_AddNumberToObject(p, "flashDurationMs", prof->flash_duration_ms);
        cJSON_AddStringToObject(p, "lowLightMode", prof->low_light_mode);
        cJSON_AddNumberToObject(p, "gainCeiling", prof->gain_ceiling);
        cJSON_AddStringToObject(p, "exposureMode", prof->exposure_mode);
        cJSON_AddNumberToObject(p, "exposureValue", prof->exposure_value);
        cJSON_AddNumberToObject(p, "sensorGain", prof->sensor_gain);
        cJSON_AddItemToObject(profiles, profile_names[i], p);
    }
    cJSON_AddItemToObject(root, "profiles", profiles);

    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    err = nvs_set_blob(handle, NVS_KEY_CONFIG, json_str, strlen(json_str));
    if (err == ESP_OK) err = nvs_commit(handle);
    free(json_str);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config persisted, active=%s", profile_names[current_config.active_profile]);
        config_apply_active_profile();
    }
    return err;
}

void config_apply_active_profile(void) {
    capture_profile_t* p = &current_config.profiles[current_config.active_profile];
    camera_set_framesize(p->frame_size);
    camera_set_quality(p->jpeg_quality);
    camera_set_flash(p->flash_enabled, p->flash_for_clips, p->flash_duration_ms);
    camera_set_exposure(p->exposure_mode, p->exposure_value, p->sensor_gain);
    camera_set_gain_ceiling(p->gain_ceiling);
    camera_set_low_light(p->low_light_mode);
    ESP_LOGI(TAG, "Active profile applied: %s", profile_names[current_config.active_profile]);
}

void config_to_json(char* buffer, size_t bufsize) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifiSsid", current_config.wifi_ssid);
    cJSON_AddBoolToObject(root, "hasPassword", current_config.wifi_pass[0] != '\0');
    cJSON_AddNumberToObject(root, "burstCount", current_config.burst_count);
    cJSON_AddNumberToObject(root, "burstIntervalMs", current_config.burst_interval_ms);
    cJSON_AddNumberToObject(root, "clipIntervalMs", current_config.clip_interval_ms);
    cJSON_AddNumberToObject(root, "clipDurationSeconds", current_config.clip_duration_sec);
    cJSON_AddBoolToObject(root, "captureOnEvent", current_config.capture_on_event);
    cJSON_AddBoolToObject(root, "manualCapture", current_config.manual_capture);
    cJSON_AddStringToObject(root, "activeProfile", profile_names[current_config.active_profile]);

    cJSON* profiles = cJSON_CreateObject();
    for (int i = 0; i < 2; i++) {
        cJSON* p = cJSON_CreateObject();
        capture_profile_t* prof = &current_config.profiles[i];
        const char* fs_str = (prof->frame_size == FRAMESIZE_QQVGA) ? "QQVGA" :
                             (prof->frame_size == FRAMESIZE_QVGA) ? "QVGA" :
                             (prof->frame_size == FRAMESIZE_VGA) ? "VGA" : "SVGA";
        cJSON_AddStringToObject(p, "frameSize", fs_str);
        cJSON_AddNumberToObject(p, "jpegQuality", prof->jpeg_quality);
        cJSON_AddBoolToObject(p, "flashEnabled", prof->flash_enabled);
        cJSON_AddBoolToObject(p, "flashForClips", prof->flash_for_clips);
        cJSON_AddNumberToObject(p, "flashDurationMs", prof->flash_duration_ms);
        cJSON_AddStringToObject(p, "lowLightMode", prof->low_light_mode);
        cJSON_AddNumberToObject(p, "gainCeiling", prof->gain_ceiling);
        cJSON_AddStringToObject(p, "exposureMode", prof->exposure_mode);
        cJSON_AddNumberToObject(p, "exposureValue", prof->exposure_value);
        cJSON_AddNumberToObject(p, "sensorGain", prof->sensor_gain);
        cJSON_AddItemToObject(profiles, profile_names[i], p);
    }
    cJSON_AddItemToObject(root, "profiles", profiles);
    cJSON_AddStringToObject(root, "activeProfile", profile_names[current_config.active_profile]);

    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (json_str) {
        strncpy(buffer, json_str, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        free(json_str);
    }
}

bool config_validate_profile(const capture_profile_t* p) {
    if (!p) return false;
    if (p->frame_size != FRAMESIZE_QQVGA && p->frame_size != FRAMESIZE_QVGA &&
        p->frame_size != FRAMESIZE_VGA && p->frame_size != FRAMESIZE_SVGA) return false;
    if (p->jpeg_quality < 10 || p->jpeg_quality > 30) return false;
    if (p->flash_duration_ms < 50 || p->flash_duration_ms > 500) return false;
    if (strcmp(p->low_light_mode, "auto") != 0 && strcmp(p->low_light_mode, "night") != 0) return false;
    int gc = p->gain_ceiling;
    if (gc != 2 && gc != 4 && gc != 8 && gc != 16 && gc != 32) return false;
    if (strcmp(p->exposure_mode, "auto") != 0 && strcmp(p->exposure_mode, "manual") != 0) return false;
    if (p->exposure_value < 100 || p->exposure_value > 1200) return false;
    if (p->sensor_gain < 0 || p->sensor_gain > 30) return false;
    return true;
}