#include "config.h"
#include "camera.h"
#include "sd_card.h"
#include "http_client.h"
#include "web_server.h"
#include "tokens.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_random.h"

static const char* TAG = "MAIN";

// Estado de operación
typedef enum {
    OP_IDLE,
    OP_IMAGE,
    OP_BURST,
    OP_CLIP
} op_kind_t;

static op_kind_t current_op = OP_IDLE;
static char current_operation_id[37] = "";
static uint32_t clip_deadline_ms = 0;
static uint16_t clip_frames_captured = 0;
static uint16_t burst_frames_captured = 0;

// Timer para clip
static TimerHandle_t clip_timer = NULL;
static TimerHandle_t poll_timer = NULL;

static void clip_timer_callback(TimerHandle_t xTimer) {
    clip_deadline_ms = 0;  // Señal de deadline alcanzado
}

static void poll_timer_callback(TimerHandle_t xTimer) {
    if (current_op != OP_IDLE) return;  // No poll durante operación activa

    char opid[37] = "";
    char cmd[32] = "";
    if (http_poll_command(opid, sizeof(opid), cmd, sizeof(cmd))) {
        ESP_LOGI(TAG, "Remote command: %s (op=%s)", cmd, opid);
        strncpy(current_operation_id, opid, sizeof(current_operation_id)-1);

        if (strcmp(cmd, "capture-image") == 0) {
            current_op = OP_IMAGE;
            xTaskCreate(capture_image_task, "cap_img", 4096, NULL, 5, NULL);
        } else if (strcmp(cmd, "capture-burst") == 0) {
            current_op = OP_BURST;
            xTaskCreate(capture_burst_task, "cap_burst", 4096, NULL, 5, NULL);
        } else if (strcmp(cmd, "capture-clip") == 0) {
            current_op = OP_CLIP;
            xTaskCreate(capture_clip_task, "cap_clip", 8192, NULL, 5, NULL);
        }
    }
}

static void capture_image_task(void* arg) {
    const app_config_t* cfg = config_get();
    capture_profile_t* prof = &cfg->profiles[cfg->active_profile];

    http_report_status(current_operation_id, "image", "capturing", "Capturando imagen", 0, 1, false);

    camera_fb_t* fb = camera_capture_fresh_frame(prof->flash_enabled, prof->flash_duration_ms);
    if (!fb) {
        http_report_status(current_operation_id, "image", "error", "Fallo captura", 0, 1, false);
        current_op = OP_IDLE;
        vTaskDelete(NULL);
        return;
    }

    http_report_status(current_operation_id, "image", "uploading", "Subiendo imagen", 1, 1, false);
    esp_err_t err = http_upload_frame(current_operation_id, fb);
    esp_camera_fb_return(fb);

    if (err == ESP_OK) {
        http_report_status(current_operation_id, "image", "complete", "Imagen recibida", 1, 1, true);
    } else {
        http_report_status(current_operation_id, "image", "error", "Fallo subida", 1, 1, false);
    }
    current_op = OP_IDLE;
    vTaskDelete(NULL);
}

static void capture_burst_task(void* arg) {
    const app_config_t* cfg = config_get();
    capture_profile_t* prof = &cfg->profiles[cfg->active_profile];
    uint8_t total = cfg->burst_count;
    uint32_t interval = cfg->burst_interval_ms;

    // Crear directorio temporal en microSD
    char dir[64];
    snprintf(dir, sizeof(dir), "/sdcard/burst-%s", current_operation_id);
    if (!sd_card_is_ready() || mkdir(dir, 0755) != 0) {
        http_report_status(current_operation_id, "burst", "error", "SD no disponible", 0, total, false);
        current_op = OP_IDLE;
        vTaskDelete(NULL);
        return;
    }

    http_report_status(current_operation_id, "burst", "capturing", "Capturando ráfaga", 0, total, false);
    burst_frames_captured = 0;

    for (uint8_t i = 0; i < total; i++) {
        char path[96];
        snprintf(path, sizeof(path), "%s/%d.jpg", dir, i);
        if (!sd_card_save_frame(path, prof->flash_enabled, prof->flash_duration_ms)) {
            http_report_status(current_operation_id, "burst", "error", "Captura incompleta", i, total, false);
            current_op = OP_IDLE;
            vTaskDelete(NULL);
            return;
        }
        burst_frames_captured++;
        http_report_status(current_operation_id, "burst", "capturing", "Capturando ráfaga", burst_frames_captured, total, false);

        if (i < total - 1) vTaskDelay(pdMS_TO_TICKS(interval));
    }

    http_report_status(current_operation_id, "burst", "uploading", "Subiendo ráfaga", 0, total, false);
    uint8_t uploaded = 0;
    for (uint8_t i = 0; i < total; i++) {
        char path[96];
        snprintf(path, sizeof(path), "%s/%d.jpg", dir, i);
        FILE* f = fopen(path, "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t* buf = malloc(len);
        if (!buf) { fclose(f); continue; }
        fread(buf, 1, len, f); fclose(f);

        camera_fb_t fb = { .buf = buf, .len = len, .width = 0, .height = 0, .format = PIXFORMAT_JPEG };
        if (http_upload_frame(current_operation_id, &fb) == ESP_OK) {
            uploaded++;
            remove(path);
        }
        free(buf);
    }

    if (uploaded == total) rmdir(dir);
    else http_report_status(current_operation_id, "burst", "error", "Subida incompleta", uploaded, total, false);

    if (uploaded == total) {
        http_report_status(current_operation_id, "burst", "complete", "Ráfaga completada", total, total, true);
    }
    current_op = OP_IDLE;
    vTaskDelete(NULL);
}

static void capture_clip_task(void* arg) {
    const app_config_t* cfg = config_get();
    capture_profile_t* prof = &cfg->profiles[cfg->active_profile];
    uint8_t duration = cfg->clip_duration_sec;
    uint32_t interval = cfg->clip_interval_ms;

    // Directorio clip
    char clip_dir[64];
    uint64_t clip_id = ((uint64_t)esp_random() << 32) | esp_random();
    snprintf(clip_dir, sizeof(clip_dir), "/sdcard/clips/clip-%llu", (unsigned long long)clip_id);
    if (!sd_card_is_ready() || mkdir(clip_dir, 0755) != 0) {
        http_report_status(current_operation_id, "clip", "error", "SD no disponible", 0, 0, false);
        current_op = OP_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Metadata inicial
    char meta_path[128];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", clip_dir);
    FILE* mf = fopen(meta_path, "w");
    if (mf) {
        fprintf(mf, "{\"operationId\":\"%s\",\"complete\":false,\"intervalMs\":%u,\"frames\":[]}",
                current_operation_id, cfg->clip_interval_ms);
        fclose(mf);
    }

    clip_deadline_ms = (esp_timer_get_time() / 1000) + (duration * 1000);
    clip_frames_captured = 0;
    clip_timer = xTimerCreate("clip_timer", pdMS_TO_TICKS(duration * 1000), pdFALSE, NULL, clip_timer_callback);
    xTimerStart(clip_timer, 0);

    http_report_status(current_operation_id, "clip", "capturing", "Grabando clip", 0, 0, false);

    while (clip_deadline_ms && (esp_timer_get_time() / 1000) < clip_deadline_ms) {
        char path[128];
        snprintf(path, sizeof(path), "%s/f%03d.jpg", clip_dir, clip_frames_captured);
        if (!sd_card_save_frame(path, prof->flash_enabled && prof->flash_for_clips, prof->flash_duration_ms)) {
            break;
        }
        clip_frames_captured++;
        http_report_status(current_operation_id, "clip", "capturing", "Grabando clip", clip_frames_captured, 0, false);
        vTaskDelay(pdMS_TO_TICKS(cfg->clip_interval_ms));
    }

    xTimerStop(clip_timer, 0);
    xTimerDelete(clip_timer, 0);
    clip_timer = NULL;

    // Actualizar metadata completa
    char meta_path[128];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", clip_dir);
    FILE* mf = fopen(meta_path, "w");
    if (mf) {
        fprintf(mf, "{\"operationId\":\"%s\",\"complete\":true,\"intervalMs\":%u,\"frames\":[",
                current_operation_id, cfg->clip_interval_ms);
        for (int i = 0; i < clip_frames_captured; i++) {
            fprintf(mf, "%s\"f%03d.jpg\"", i ? "," : "", i);
        }
        fprintf(mf, "]}");
        fclose(mf);
    }

    // Subida
    http_report_status(current_operation_id, "clip", "uploading", "Subiendo clip", 0, clip_frames_captured, false);
    uint16_t uploaded = 0;
    for (int i = 0; i < clip_frames_captured; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/f%03d.jpg", clip_dir, i);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t* buf = malloc(len);
        if (!buf) { fclose(f); continue; }
        fread(buf, 1, len, f); fclose(f);
        camera_fb_t fb = { .buf = buf, .len = len, .width = 0, .height = 0, .format = PIXFORMAT_JPEG };
        if (http_upload_frame(current_operation_id, &fb) == ESP_OK) {
            uploaded++;
        }
        free(buf);
    }

    // Actualizar metadata final
    if (mf = fopen(meta_path, "w")) {
        fprintf(mf, "{\"operationId\":\"%s\",\"complete\":%s,\"intervalMs\":%u,\"frames\":[",
                current_operation_id, (uploaded == clip_frames_captured) ? "true" : "false", cfg->clip_interval_ms);
        for (int i = 0; i < clip_frames_captured; i++) {
            fprintf(mf, "%s\"f%03d.jpg\"", i ? "," : "", i);
        }
        fprintf(mf, "]}");
        fclose(mf);
    }

    if (uploaded == clip_frames_captured) {
        http_report_status(current_operation_id, "clip", "complete", "Clip completado", clip_frames_captured, clip_frames_captured, true);
    } else {
        http_report_status(current_operation_id, "clip", "error", "Subida incompleta", uploaded, clip_frames_captured, false);
    }

    current_op = OP_IDLE;
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-CAM Evidence starting...");

    // Inicialización base
    ESP_ERROR_CHECK(config_init());
    ESP_ERROR_CHECK(camera_init());
    ESP_ERROR_CHECK(sd_card_init());
    ESP_ERROR_CHECK(sd_card_probe());
    ESP_ERROR_CHECK(wifi_init_from_config());
    ESP_ERROR_CHECK(http_client_init());
    ESP_ERROR_CHECK(web_server_start());

    config_apply_active_profile();

    // Timer de polling remoto (1 Hz cuando idle)
    poll_timer = xTimerCreate("poll_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, poll_timer_callback);
    xTimerStart(poll_timer, 0);

    ESP_LOGI(TAG, "System ready. Active profile: %s",
             config_get()->active_profile == 0 ? "day" : "night");

    // Loop principal: mantener vivo, logs periódicos
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        const app_config_t* cfg = config_get();
        ESP_LOGI(TAG, "Heartbeat: profile=%s sd=%s wifi=%s op=%d",
                 cfg->active_profile == 0 ? "day" : "night",
                 sd_card_is_ready() ? "ready" : "fail",
                 "connected", current_op);
    }
}