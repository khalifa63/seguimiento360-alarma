#include "camera.h"
#include "tokens.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <string.h>

static const char* TAG = "CAMERA";

#define FLASH_PIN 4
#define FLASH_SAFETY_US 1500000  // 1.5 s corte de seguridad

static esp_timer_handle_t flash_safety_timer = NULL;
static uint32_t flash_duration_ms = 120;
static bool flash_enabled = false;
static bool flash_for_clips = false;

static const char* exposure_mode = "auto";
static int exposure_value = 800;
static int sensor_gain = 4;
static int gain_ceiling = 16;
static const char* low_light_mode = "auto";

static framesize_t current_framesize = FRAMESIZE_SVGA;
static int current_quality = 12;

// Mapea gainCeiling (2,4,8,16,32) a constante ESP
static gainceiling_t map_gain_ceiling(int v) {
    if (v >= 32) return GAINCEILING_32X;
    if (v >= 16) return GAINCEILING_16X;
    if (v >= 8)  return GAINCEILING_8X;
    if (v >= 4)  return GAINCEILING_4X;
    return GAINCEILING_2X;
}

static void flash_safety_off(void* arg) {
    gpio_set_level(FLASH_PIN, 0);
    ESP_LOGW(TAG, "FLASH_SAFETY_CUTOFF");
}

esp_err_t camera_init(void) {
    // Timer de seguridad flash
    esp_timer_create_args_t timer_args = {
        .callback = flash_safety_off,
        .name = "flash-safety"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &flash_safety_timer));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FLASH_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(FLASH_PIN, 0);

    camera_config_t c = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = 5, .pin_d1 = 18, .pin_d2 = 19, .pin_d3 = 21,
        .pin_d4 = 36, .pin_d5 = 39, .pin_d3 = 34, .pin_d7 = 35,
        .pin_xclk = 0, .pin_pclk = 22, .pin_vsync = 25, .pin_href = 23,
        .pin_sccb_sda = 26, .pin_sccb_scl = 27,
        .pin_pwdn = 32, .pin_reset = -1,
        .xclk_freq_hz = 10000000,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 14,
        .fb_count = 1
    };

    if (psramFound()) {
        c.frame_size = FRAMESIZE_SVGA;
        c.jpeg_quality = 12;
        c.fb_count = 1;
        c.grab_mode = CAMERA_GRAB_LATEST;
    }

    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        current_framesize = c.frame_size;
        current_quality = c.jpeg_quality;
        s->set_framesize(s, current_framesize);
        s->set_quality(s, current_quality);
    }

    ESP_LOGI(TAG, "Camera initialized: %dx%d @ %d MHz",
             (current_framesize == FRAMESIZE_SVGA) ? 800 : 640,
             (current_framesize == FRAMESIZE_SVGA) ? 600 : 480,
             10);
    return ESP_OK;
}

void camera_flash_off(void) {
    gpio_set_level(FLASH_PIN, 0);
    if (flash_safety_timer) esp_timer_stop(flash_safety_timer);
}

camera_fb_t* camera_capture_fresh_frame(bool flash_enabled_param, uint32_t flash_duration_ms_param) {
    bool lit = flash_enabled_param;
    uint32_t started = esp_timer_get_time() / 1000;

    if (lit) {
        gpio_set_level(FLASH_PIN, 1);
        if (flash_safety_timer) esp_timer_start_once(flash_safety_timer, FLASH_SAFETY_US);
        ESP_LOGI(TAG, "FLASH_ON ms=%lu", started);
        vTaskDelay(pdMS_TO_TICKS(flash_duration_ms_param));
    }

    // Descarta 4 frames para convergencia AEC/AWB bajo iluminación
    for (int i = 0; i < 4; i++) {
        camera_fb_t* discard = esp_camera_fb_get();
        if (!discard) { camera_flash_off(); return NULL; }
        esp_camera_fb_return(discard);
    }

    camera_fb_t* fb = esp_camera_fb_get();
    camera_flash_off();

    sensor_t* s = esp_camera_sensor_get();
    uint32_t elapsed = (esp_timer_get_time() / 1000) - started;
    ESP_LOGI(TAG, "FRAME_READY ms=%lu capture_ms=%lu flash=%d bytes=%u aec=%d gain=%d",
             (esp_timer_get_time()/1000), elapsed, lit,
             fb ? (unsigned)fb->len : 0,
             s ? s->status.aec_value : 0,
             s ? s->status.agc_gain : 0);

    return fb;
}

void camera_apply_config(void) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    if (s->status.framesize != current_framesize)
        s->set_framesize(s, current_framesize);
    s->set_quality(s, current_quality);

    // Base: auto exposición/ganancia + techo + AEC2 + AWB
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, map_gain_ceiling(gain_ceiling));
    s->set_aec2(s, 1);
    s->set_ae_level(s, strcmp(low_light_mode, "night") == 0 ? 1 : 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);

    // Sobrescritura manual
    if (strcmp(exposure_mode, "manual") == 0) {
        s->set_aec2(s, 0);
        s->set_exposure_ctrl(s, 0);
        s->set_gain_ctrl(s, 0);
        s->set_aec_value(s, exposure_value);
        s->set_agc_gain(s, sensor_gain);
    }

    ESP_LOGI(TAG, "Config applied: size=%d quality=%d exposure=%s value=%d gain=%d flash=%d",
             current_framesize, current_quality, exposure_mode, exposure_value, sensor_gain, flash_enabled);
}

void camera_log_sensor_registers(const char* stage) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s || s->id.PID != 0x26) return;

    int pid = s->get_reg(s, 0x10A, 0xFF);
    int com8 = s->get_reg(s, 0x113, 0xFF);
    int r04 = s->get_reg(s, 0x104, 0xFF);
    int aec = s->get_reg(s, 0x110, 0xFF);
    int r45 = s->get_reg(s, 0x145, 0xFF);
    int gain = s->get_reg(s, 0x100, 0xFF);
    int clk = s->get_reg(s, 0x111, 0xFF);
    int ctrl0 = s->get_reg(s, 0x0C2, 0xFF);
    int pclk = s->get_reg(s, 0x0D3, 0xFF);

    int actual_exposure = ((r45 & 0x3F) << 10) | ((aec & 0xFF) << 2) | (r04 & 3);

    ESP_LOGI(TAG, "SENSOR_DIAG {\"stage\":\"%s\",\"ms\":%lu,\"pid\":%d,\"requestedExposure\":%d,\"requestedMode\":\"%s\",\"requestedGainIndex\":%d,\"actualExposure\":%d,\"aecEnabled\":%d,\"agcEnabled\":%d,\"aec2Enabled\":%d,\"rawCOM8\":%d,\"rawREG04\":%d,\"rawAEC\":%d,\"rawREG45\":%d,\"rawGAIN\":%d,\"rawCTRL0\":%d,\"rawCLKRC\":%d,\"rawPCLK\":%d,\"xclkHz\":%lu}",
             stage, (unsigned long)(esp_timer_get_time()/1000), pid, exposure_value, exposure_mode, sensor_gain,
             actual_exposure,
             (com8 & 1), (com8 & 4), (ctrl0 & 0x40) == 0,
             com8, r04, aec, r45, gain, ctrl0, clk, pclk, 10000000UL);
}

// Setters para configuración remota
void camera_set_flash(bool enabled, bool for_clips, uint32_t duration_ms) {
    flash_enabled = enabled;
    flash_for_clips = for_clips;
    flash_duration_ms = duration_ms;
}

void camera_set_exposure(const char* mode, int value, int gain) {
    exposure_mode = mode;
    exposure_value = value;
    sensor_gain = gain;
}

void camera_set_gain_ceiling(int ceiling) { gain_ceiling = ceiling; }
void camera_set_low_light(const char* mode) { low_light_mode = mode; }
void camera_set_framesize(framesize_t fs) { current_framesize = fs; }
void camera_set_quality(int q) { current_quality = q; }