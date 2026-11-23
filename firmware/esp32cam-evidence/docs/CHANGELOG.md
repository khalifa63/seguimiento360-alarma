# Changelog — Protocolo ESP32-CAM Evidence

Formato: [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/), versionado semántico aproximado.

## [2026-11-23] — 2.1.0 — Documentación completa y fixes UI
### Added
- Documentación técnica completa en `docs/` (README, ARCHITECTURE, FIRMWARE, SERVER_API, PROFILES, USER_GUIDE, TROUBLESHOOTING, DEPLOYMENT, SECURITY, CHANGELOG).
- Checkbox “Mostrar contraseña que estoy escribiendo” en configuración Wi‑Fi (`f-show-pass`).
- Recuperación microSD no destructiva: reintentos 20/10/4/0.4 MHz, sonda escritura/lectura, `POST /sd-recover`, `format_on_failure:false`.
- Perfiles Día/Noche con migración legacy → noche, selección manual, campos `PROFILE_FIELDS` definidos.
- Validación `step=1` en inputs ms para aceptar cualquier entero en rango.
- Cierre automático de panel de captura cuando evidencia fue eliminada tras operación completa.
- Endpoints diagnóstico cámara: `/sd-status`, `/sd-recover`, `sdReport()` extendido.
- Firmware ESP-IDF v5.x completo en `firmware/esp32cam-evidence/` con `tokens.h.example`, `.gitignore`, `sdkconfig.defaults`, `partitions.csv`.

### Changed
- UI: selectores independientes “Perfil activo” / “Perfil a editar”, indicador “Perfil guardado para próximas capturas”.
- `camera_capture_fresh_frame`: 4 frames descartados para convergencia AEC/AWB, flash apagado antes de red.
- `sd_card_init`: sin formateo, reintentos multi-frecuencia, log `SD_MOUNT_ATTEMPT`.
- `capture_clip_task`: verifica deadline antes de adquirir, sin catch-up; total real al finalizar.
- Servidor: operaciones activas no expiran a 15 s (`stale:true`), completitud por correlación `operationId`.
- Configuración: `readConfig()` migración atómica serializada; comando a cámara aplanado sin perfiles anidados.
- Arquitectura: migración de Arduino-ESP32 a ESP-IDF v5.x en `firmware/esp32cam-evidence/`.

### Fixed
- Bucle UI “esperando la carga visible de todas las evidencias” tras borrado de galería.
- `checkValidity()` bloqueaba `flashDurationMs=123` o `burstIntervalMs=550` → `step=1`.
- Clip `total=0` durante captura, `total=frames reales` al subir.
- MicroSD `clipCount` inicial correcto tras montaje (antes contaba 0).
- Subida ráfaga: archivos temporales eliminados solo tras éxito; fallidos conservados.
- Autenticación: comparación constante-tiempo SHA-256 para ambos tokens.

## [2026-11-22] — 2.0.0 — Perfiles Día/Noche y arquitectura aislada
### Added
- Perfiles Día y Noche independientes con 10 campos cada uno.
- Migración legacy automática (primer GET `/api/config`).
- Selección manual de perfil activo; sin cambio automático.
- UI PWA: galería con carga diferida, progreso captura, descarga ZIP clips, borrado.
- Configuración Wi‑Fi con visibilidad de contraseña controlable.
- Servidor Node.js aislado en `0.0.0.0:8787`, Cloudflare Tunnel a `camera.seguimiento360.com`.
- Autenticación dual: `X-Camera-Token` / `X-Web-Token` o `?token=`.
- Cola serializada de operaciones (comandos, config, estado cámara).
- Endpoints: `/health`, `/api/config`, `/api/evidence`, `/api/capture-command`, `/api/camera-status`, `/api/upload`, `/api/clips/:id`, `/api/clips/:id/:frame`, `DELETE` evidencia/clips.
- microSD: `/clips`, `/burst-<opId>`, `/diagnostics`, poda `MAX_LOCAL_CLIPS=100`, `MAX_SD_PERCENT=80%`.
- Firmware: `CAMERA_GRAB_LATEST`, `fb_count=1`, clock 10 MHz, flash GPIO4 corte 1.5 s.
- Captura: flash → espera 120 ms → descarta 4 frames → retiene 5º → flash off antes de red.
- Diagnóstico sensor: `logSensorRegisters()` lectura registros OV2640 I²C (banco 1/0).
- Pruebas automatizadas: imagen, ráfaga, clip, validación SHA-256, integridad config.

### Changed
- Arquitectura: cámara solo salidas HTTPS; servidor sin acceso entrante a cámara.
- Alarmas: pila `iot-alarma` intocable en `/home/antonio/iot-alarma/`.
- Tokens: archivos `camera.token` / `web.token` (chmod 600), nunca en repo.

## [2026-11-21] — 1.1.0 — Recuperación microSD y estabilidad
### Added
- `initSD()` con reintentos 20/10/4/0.4 MHz, `format_if_mount_failed=false`.
- `probeSD()` escritura/lectura verificada en `/diagnostics/probe-*.txt`.
- Endpoint `POST /sd-recover` autenticado para forzar recuperación.
- Logs serie `SD_MOUNT_ATTEMPT`, `SD_PROBE_OK`, `SD_OK`.

### Fixed
- Ráfaga fallaba “No se pudo preparar la ráfaga en SD” cuando tarjeta no montada.
- Clip `total=seconds` en lugar de frames → 409/hang.
- Off-by-one en deadline de clip y catch-up de frames.
- Burst upload espaciado inducido por subida secuencial → intervalo real medido ~1000/999 ms.

## [2026-11-20] — 1.0.0 — Prototipo base funcional
### Added
- Captura imagen, ráfaga (5), clip (10/30/60 s) con correlación `operationId`.
- microSD buffer + subida secuencial HTTP/1.0 + `X-Operation-Id`.
- Flash LED GPIO4: encendido → delay 120 ms → captura → apagado antes de red.
- Exposición/ganancia manual (unidades sensor) + auto con techo configurable.
- Servidor Node.js: evidencias, galería PWA básica, config JSON persistente.
- Autenticación tokens SHA-256 constante-tiempo.
- Despliegue: `restart_sync.py` (backup, sintaxis, PID :8787, cwd, detach, log).

### Known issues (resueltos en 1.1.0+)
- MicroSD no montada al arrancar → ráfaga/clip fallaban.
- Clip estado `recording` no soportado por servidor → cambiado a `capturing`.
- Flash quedaba encendido durante subida → ahora off inmediato post-captura.
- Polling 5 s → 1 s; `readString()` bloqueante → HTTPClient con timeouts.

## Convenciones de versión
- **MAJOR**: cambio rompedor en API, formato de evidencia, autenticación.
- **MINOR**: nueva funcionalidad compatible (perfiles, endpoints, diagnósticos).
- **PATCH**: correcciones, ajustes de parámetros, documentación.

## Próximos hitos planificados
- [ ] Cifrado microSD (AES-XTS) y firmware firmado.
- [ ] Contenedor MP4/WebM para clips (ffmpeg en servidor).
- [ ] Sincronización horaria (NTP) y timestamp en sensor.
- [ ] Integración eventos alarma → captura automática verificada.
- [ ] Rotación automática de tokens y certificados mTLS cámara↔servidor.
- [ ] Pruebas de carga y longevidad (72 h continuas).