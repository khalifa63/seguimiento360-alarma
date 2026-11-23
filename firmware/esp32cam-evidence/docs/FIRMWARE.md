# Firmware ESP32-CAM — Detalle Técnico (ESP-IDF v5.x)

Versión: prototipo `esp32cam-evidence` con recuperación microSD y diagnóstico de sensor.

## Configuración de cámara (inicialización)

```c
camera_config_t c = {
    .ledc_channel = LEDC_CHANNEL_0,
    .ledc_timer   = LEDC_TIMER_0,
    .pin_d0 = 5, .pin_d1 = 18, .pin_d2 = 19, .pin_d3 = 21,
    .pin_d4 = 36, .pin_d5 = 39, .pin_d6 = 34, .pin_d7 = 35,
    .pin_xclk = 0, .pin_pclk = 22, .pin_vsync = 25, .pin_href = 23,
    .pin_sccb_sda = 26, .pin_sccb_scl = 27,
    .pin_pwdn = 32, .pin_reset = -1,
    .xclk_freq_hz = 10000000,      // 10 MHz — mejora ruido/bandas
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 14,
    .fb_count = 1
};
if (psramFound()) {
    c.frame_size = FRAMESIZE_SVGA;   // 800×600
    c.jpeg_quality = 12;
    c.fb_count = 1;
    c.grab_mode = CAMERA_GRAB_LATEST;
}
esp_camera_init(&c);
```

## Secuencia de flash y captura (`camera_capture_fresh_frame`)

```c
bool lit = flash_enabled;
uint32_t started = esp_timer_get_time() / 1000;

if (lit) {
    gpio_set_level(FLASH_PIN, 1);
    esp_timer_start_once(flash_safety_timer, 1500000); // 1.5 s corte
    ESP_LOGI(TAG, "FLASH_ON ms=%lu", started);
    vTaskDelay(pdMS_TO_TICKS(flash_duration_ms));
}

// Descarta 4 frames para convergencia AEC/AWB bajo iluminación
for (int i = 0; i < 4; i++) {
    camera_fb_t* discard = esp_camera_fb_get();
    if (!discard) { camera_flash_off(); return NULL; }
    esp_camera_fb_return(discard);
}

camera_fb_t* fb = esp_camera_fb_get();  // 5º frame = exposición real
camera_flash_off();

sensor_t* s = esp_camera_sensor_get();
ESP_LOGI(TAG, "FRAME_READY ms=%lu capture_ms=%lu flash=%d bytes=%u aec=%d gain=%d",
         ..., lit, fb ? fb->len : 0, s->status.aec_value, s->status.agc_gain);
return fb;
```

**Puntos clave:**
- LED apagado **antes** de cualquier operación de red.
- Corte de seguridad por `esp_timer` (1.5 s).
- 4 frames descartados para estabilización auto-exposición/balance.
- Log `FRAME_READY` con `aec_value` y `agc_gain` leídos del sensor.

## Aplicación de configuración de sensor (`camera_apply_config`)

```c
sensor_t* s = esp_camera_sensor_get();
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
    s->set_aec_value(s, exposure_value);   // unidades sensor, NO ms
    s->set_agc_gain(s, sensor_gain);       // índice 0–30
}
```

## Diagnóstico de registros OV2640 (`camera_log_sensor_registers`)

Leídos por I²C (banco 1 sensor, banco 0 DSP) tras configuración y captura:

```c
int pid = s->get_reg(s, 0x10A, 0xFF);
int com8 = s->get_reg(s, 0x113, 0xFF);
int r04 = s->get_reg(s, 0x104, 0xFF);
int aec = s->get_reg(s, 0x110, 0xFF);
int r45 = s->get_reg(s, 0x145, 0xFF);
int actual_exposure = ((r45 & 0x3F) << 10) | ((aec & 0xFF) << 2) | (r04 & 3);
```

JSON emitido por serial:
```json
{
  "stage": "configured|before-capture|after-capture",
  "ms": 12345,
  "pid": 38,
  "requestedExposure": 200,
  "requestedMode": "manual",
  "requestedGainIndex": 0,
  "actualExposure": 200,
  "aecEnabled": 0, "agcEnabled": 0, "aec2Enabled": 1,
  "rawCOM8": 224, "rawREG04": 40, "rawAEC": 25, "rawREG45": 0,
  "rawGAIN": 52, "rawCTRL0": 76, "rawCLKRC": 0, "rawPCLK": 8,
  "xclkHz": 10000000
}
```

> **Nota**: `actualExposure` coincide con lo solicitado en modo manual. La lectura de registros confirma aplicación en silicio.

## Captura de imagen, ráfaga y clip

### Imagen (`capture_image_task`)
1. `http_report_status(..., "capturing", ...)`
2. `camera_capture_fresh_frame(prof->flash_enabled, prof->flash_duration_ms)`
3. `http_upload_frame(operation_id, fb)` → `esp_camera_fb_return(fb)`
4. Servidor confirma completitud atómicamente al recibir JPEG (201).

### Ráfaga (`capture_burst_task`)
- Crea `/sdcard/burst-<operationId>/` en microSD.
- Captura `burst_count` frames con intervalo `burst_interval_ms`.
- Guarda cada frame como `0.jpg`, `1.jpg`… en microSD.
- Sube secuencialmente; frames exitosos se borran, fallidos se conservan.

### Clip (`capture_clip_task`)
- Duración `clip_duration_sec` ∈ {10,30,60}, intervalo `clip_interval_ms`.
- Crea `/sdcard/clips/clip-<uint64>/` con `metadata.json` (`complete:false`).
- Captura frames hasta deadline; **sin flash** salvo `flash_for_clips=true`.
- Sube frames; al terminar marca `complete:true` en metadata.

## Recuperación microSD (`sd_card_init` / `sd_card_probe` / `sd_card_recover`)

```c
// Reintentos 20/10/4/0.4 MHz, format_if_mount_failed=false
for (khz in {20000, 10000, 4000, 400}) {
    esp_vfs_fat_sdmmc_mount("/sdcard", ..., false, ...);
    if (ok) break;
}

// Sonda escritura/lectura verificada
FILE* f = fopen("/sdcard/diagnostics/probe-<rand>.txt", "w");
fwrite("CAM_SD_WRITE_READ_PROBE_V1\n", ...); fclose(f);
f = fopen(path, "r"); fread(...); fclose(f);
memcmp(buf, payload, len) == 0 → sd_probe_ok = true;

// Endpoint autenticado POST /sd-recover
// Responde sdReport() JSON con write_read_verified
```

**Principios:**
- **Sin formateo automático** (`format_if_mount_failed=false`).
- Reintentos multi-frecuencia y reset de bus en fallback.
- Sonda escritura/lectura en archivo nuevo único.
- `sdReport()` expone: `ready`, `total_mb`, `used_mb`, `clips`, `mount_attempts`, `clock_khz`, `write_read_verified`, `probe_path`, `error`, `format_on_failure:false`.

## Logs de diagnóstico (serial)

```
CAM_EVIDENCE_SYNC_V2
SD_MOUNT_ATTEMPT attempt=1 khz=20000 format=0
SD_OK MB=30000 used=40 clips=32 khz=20000
WIFI_OK IP=192.168.10.10 RSSI=-52
HTTP_READY
FLASH_ON ms=12345
FRAME_READY ms=12420 capture_ms=750 flash=1 bytes=28873 aec=300 gain=0
BURST_UPLOAD ms=12500 frame=1
...
SD_PROBE_OK path=/sdcard/diagnostics/probe-a9a8700e.txt bytes=25
```

## Compilación y flasheo (ESP-IDF v5.x)

```bash
. $IDF_PATH/export.sh
cd firmware/esp32cam-evidence
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# Identificar puerto por MAC conocida
esptool.py --port /dev/ttyUSB0 chip_id
# Debe mostrar MAC ec:64:c9:ac:c6:b8
```

- Sketch ≈ 1.2–1.3 MB, globals ≈ 60 KB.
- Particiones: factory + OTA0 + OTA1 + coredump + NVS.
- `sdkconfig.defaults` incluye: `CONFIG_ESP_CAMERA_XCLK_FREQ_HZ=10000000`, `CONFIG_ESP_CAMERA_GRAB_LATEST=y`, `CONFIG_SD_MMC_1BIT_MODE=y`.