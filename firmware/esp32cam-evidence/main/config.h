#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Perfiles de captura
typedef struct {
    framesize_t frame_size;
    int jpeg_quality;
    bool flash_enabled;
    bool flash_for_clips;
    uint32_t flash_duration_ms;
    const char* low_light_mode;   // "auto" | "night"
    int gain_ceiling;             // 2,4,8,16,32
    const char* exposure_mode;    // "auto" | "manual"
    int exposure_value;           // 100-1200 (unidades sensor)
    int sensor_gain;              // 0-30
} capture_profile_t;

// Configuración global (común a ambos perfiles)
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    uint8_t burst_count;          // 1-10
    uint32_t burst_interval_ms;   // 200-5000
    uint32_t clip_interval_ms;    // 200-5000
    uint8_t clip_duration_sec;    // 10,30,60
    bool capture_on_event;
    bool manual_capture;
    // Perfil activo
    int active_profile;           // 0 = day, 1 = night
    capture_profile_t profiles[2]; // [0]=day, [1]=night
} app_config_t;

// Inicializa NVS y carga configuración (con migración legacy -> day/night)
esp_err_t config_init(void);

// Obtiene puntero a configuración actual (rama principal)
const app_config_t* config_get(void);

// Actualiza configuración (parcial o completa) y persiste en NVS
esp_err_t config_update(const app_config_t* partial);

// Aplica perfil activo a cámara (llama camera_apply_config)
void config_apply_active_profile(void);

// Serializa configuración actual a JSON en buffer
void config_to_json(char* buffer, size_t bufsize);

// Valida campos de perfil (rangos, enums)
bool config_validate_profile(const capture_profile_t* p);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H