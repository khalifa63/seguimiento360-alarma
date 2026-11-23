#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa cliente HTTPS (usa tokens.h para HOST/CAMERA_TOKEN)
esp_err_t http_client_init(void);

// Sube un frame JPEG a /api/upload con X-Operation-Id
// Devuelve ESP_OK si 201, error en caso contrario
esp_err_t http_upload_frame(const char* operation_id, const camera_fb_t* fb);

// Reporta estado de operación a /api/camera-status
esp_err_t http_report_status(const char* operation_id, const char* kind,
                             const char* state, const char* message,
                             uint16_t current, uint16_t total, bool uploads_confirmed);

// Poll remoto: GET /api/capture-command con X-Camera-Token
// Devuelve true si hay comando nuevo; operation_id y command se llenan
bool http_poll_command(char* operation_id, size_t opid_size, char* command, size_t cmd_size);

// Inicialización WiFi + preferencias (ssid/pass desde config.c)
esp_err_t wifi_init_from_config(void);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_H