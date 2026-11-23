#ifndef SD_CARD_H
#define SD_CARD_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa microSD con reintentos multi-frecuencia (20/10/4/0.4 MHz), sin formateo
esp_err_t sd_card_init(void);

// Sonda escritura/lectura verificada en /diagnostics/probe-<rand>.txt
bool sd_card_probe(void);

// Reporta estado completo como JSON (listo para serial o HTTP)
void sd_card_report_json(char* buffer, size_t bufsize);

// Recuperación forzada: reintenta montaje + sonda; usa antes de ráfaga/clip
esp_err_t sd_card_recover(void);

// Guarda frame JPEG en ruta dada (usa camera_capture_fresh_frame internamente)
bool sd_card_save_frame(const char* path, bool flash_enabled, uint32_t flash_duration_ms);

// Cuenta clips locales en /clips
uint16_t sd_card_count_clips(void);

// Podar clips antiguos si supera MAX_LOCAL_CLIPS o MAX_SD_PERCENT
uint16_t sd_card_prune_old_clips(uint16_t target);

// Estado listo
bool sd_card_is_ready(void);

// Bytes totales/usados
uint64_t sd_card_total_bytes(void);
uint64_t sd_card_used_bytes(void);

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_H