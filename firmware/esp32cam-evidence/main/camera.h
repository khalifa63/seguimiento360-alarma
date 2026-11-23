#ifndef CAMERA_H
#define CAMERA_H

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa la cámara OV2640 con clock 10 MHz, SVGA si hay PSRAM, fb_count=1, GRAB_LATEST
esp_err_t camera_init(void);

// Captura un frame fresco con secuencia de flash:
// - flash on -> delay flashDurationMs -> descarta 4 frames -> retiene 5º -> flash off
// Devuelve frame listo para guardar/subir; NULL si falla.
camera_fb_t* camera_capture_fresh_frame(bool flash_enabled, uint32_t flash_duration_ms);

// Apaga flash LED y cancela timer de seguridad
void camera_flash_off(void);

// Aplica configuración de sensor (resolución, calidad, exposición, ganancia, flash, AWB, etc.)
void camera_apply_config(void);

// Lee registros OV2640 (banco 1 sensor, banco 0 DSP) y emite JSON por serial
void camera_log_sensor_registers(const char* stage);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_H