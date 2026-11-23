# Arquitectura del Prototipo

## Visión general

```
┌──────────────────┐       Wi-Fi LAN       ┌─────────────────────┐
│  ESP32-CAM       │◄─────────────────────►│  Servidor Node.js   │
│  (cámara)        │   HTTPS saliente      │  (aislado :8787)    │
└────────┬─────────┘                       └──────────┬──────────┘
         │                                            │
         │ microSD                                    │ Cloudflare Tunnel
         ▼                                            ▼
┌──────────────────┐                       ┌─────────────────────┐
│  Almacenamiento  │                       │  Navegador (PWA)    │
│  local (JPEG)    │                       │  https://camera.    │
└──────────────────┘                       │  seguimiento360.com │
                                           └─────────────────────┘
```

- La cámara **nunca recibe conexiones entrantes**; solo salidas HTTPS al servidor.
- El servidor no tiene acceso a la red de la cámara; la cámara sí al servidor.
- La pila de alarma existente (`/home/antonio/iot-alarma/`) no se toca.

## Componentes de hardware

| Componente | Detalle |
|------------|---------|
| Placa | AI-Thinker ESP32-CAM (ESP32-D0WDQ6 rev 1.1) |
| Sensor | OV2640, clock 10 MHz |
| Flash | GPIO4, activo alto, corte de seguridad 1.5 s |
| microSD | SD_MMC 1-bit, `/clips`, `/burst-<opId>`, `/diagnostics` |
| Alimentación | 5 V regulado (boost MCP73871 + condensadores locales) |

## Firmware (ESP32-CAM)

### Inicialización
1. `Serial 115200`, `CAM_EVIDENCE_SYNC_V2`.
2. Cámara: `FRAMESIZE_SVGA` (800×600), JPEG quality 12, `fb_count=1`, `CAMERA_GRAB_LATEST`.
3. microSD: `initSD()` con reintentos a 20/10/4/0.4 MHz, **sin formateo**.
4. Wi‑Fi STA+AP; credenciales desde `Preferences` (`ssid`/`pass`).
5. Servidor HTTP local en puerto 80 (solo diagnóstico: `/health`, `/sd-status`, `/sd-recover`).

### Captura de imagen (`capture-image`)
- Flash: encendido → espera `flashDurationMs` (default 120 ms) → descarta 4 frames → retiene 5º → flash **apagado antes de red**.
- `esp_camera_fb_get()` con `CAMERA_GRAB_LATEST`.
- Subida `POST /api/upload` con `X-Operation-Id`.
- El servidor confirma completitud atómicamente al recibir el JPEG.

### Ráfaga (`capture-burst`)
- Crea directorio `/burst-<operationId>/` en microSD.
- Captura `burstCount` frames con intervalo `burstIntervalMs`.
- Guarda cada frame como `0.jpg`, `1.jpg`… en microSD.
- Sube secuencialmente; frames exitosos se borran de microSD, fallidos se conservan.

### Clip (`capture-clip`)
- Duración `clipDurationSeconds` ∈ {10,30,60}, intervalo `clipIntervalMs`.
- Crea `/clips/clip-<uint64>/` con `metadata.json` (`complete:false`).
- Captura frames hasta tiempo límite; **sin flash** salvo `flashForClips=true`.
- Sube frames; al terminar marca `complete:true` en metadata.

### Recuperación microSD
- `initSD()` reintenta montaje a 20/10/4/0.4 MHz, **`format_if_mount_failed=false`**.
- `probeSD()`: escritura/lectura verificada en `/diagnostics/probe-<rand>.txt`.
- Endpoint autenticado `POST /sd-recover` para forzar recuperación.
- `sdReport()` expone: `ready`, `total_mb`, `used_mb`, `clips`, `mount_attempts`, `clock_khz`, `write_read_verified`, `probe_path`, `error`, `format_on_failure:false`.

## Servidor (Node.js)

### Endpoints principales
| Método | Ruta | Autenticación | Descripción |
|--------|------|---------------|-------------|
| GET | `/health` | Web | Estado del servicio |
| GET | `/api/config` | Web | Configuración actual (perfiles + comunes) |
| POST | `/api/config` | Web | Actualiza perfiles Día/Noche y ajustes comunes |
| GET | `/api/evidence` | Web | Lista evidencia (imágenes + clips) |
| POST | `/api/capture-command` | Web | Encola `capture-image|burst|clip` |
| GET | `/api/camera-status` | Web | Estado de operación (polling) |
| POST | `/api/camera-status` | Cámara | Cámara reporta progreso |
| POST | `/api/upload` | Cámara | Sube JPEG individual |
| GET | `/api/clips/:id` | Web | Manifiesto de clip (frames + intervalo) |
| GET | `/api/clips/:id/:frame` | Web | Descarga frame JPEG |
| DELETE | `/api/evidence/:id.jpg` | Web | Borra imagen |
| DELETE | `/api/clips/:id` | Web | Borra clip completo |
| POST | `/sd-recover` | Cámara | Fuerza recuperación microSD |

### Autenticación
- Cámara: cabecera `X-Camera-Token` (comparación constante-tiempo SHA-256).
- Navegador: `X-Web-Token` o `?token=` (igual comparación).
- Tokens en `data/camera.token` y `data/web.token` (archivos, no en repo).

### Operaciones y estados
```
queued → capturing → uploading → complete (uploadsConfirmed:true) → terminal
                              ↘ error (terminal)
```
- Operaciones activas **no expiran** a los 15 s; se marcan `stale:true` y la UI sigue visible.
- Completitud derivada de archivos correlacionados por `operationId`; clips requieren además `complete:true` en metadata.

### Persistencia
- `data/camera-config.json`: `{wifiSsid, wifiPassword, burstCount, burstIntervalMs, clipIntervalMs, clipDurationSeconds, captureOnEvent, manualCapture, activeProfile, profiles:{day:{...}, night:{...}}}`.
- `data/camera-command.json`: comando en cola (serializado, conflictos 409).
- `data/camera-status.json`: estado de operación actual.
- `data/evidence/*.jpg` + `*.jpg.json` (metadata: `operationId`, `capturedAt`).
- `data/clips/clip-*/metadata.json`: `{operationId, complete, intervalMs, frames:["f001.jpg",...]}`.

## Red y despliegue

- Cámara: Wi‑Fi `KHALIFA_R`, IP fija `192.168.10.10` (reserva DHCP).
- Servidor: `192.168.1.200`, proceso Node en `0.0.0.0:8787`, PID en `prototype.pid`, log en `prototype.log`.
- Túnel: Cloudflare `https://camera.seguimiento360.com` → `127.0.0.1:8787`.
- VLAN: servidor **no** alcanza a la cámara entrante; cámara **sí** sale a Internet.
- Alarma: `/home/antonio/iot-alarma/docker-compose.yml` — **intocable**.

## Perfiles Día / Noche

Campos por perfil (`PROFILE_FIELDS`):
`frameSize`, `jpegQuality`, `flashEnabled`, `flashForClips`, `flashDurationMs`,
`lowLightMode`, `gainCeiling`, `exposureMode`, `exposureValue`, `sensorGain`.

- **Globales** (comunes): `wifiSsid`, `wifiPassword`, `burstCount`, `burstIntervalMs`,
  `clipIntervalMs`, `clipDurationSeconds`, `captureOnEvent`, `manualCapture`.
- **Selección**: `activeProfile` ∈ {`day`,`night`} — **manual**, sin cambio automático.
- **Migración** (primer GET `/api/config` si legado): clona ajustes actuales a `night`, crea `day` con defaults conservadores, `activeProfile="night"`.
- **Comando a cámara**: aplanado a campos efectivos del perfil activo; **sin anidamiento** de perfiles.

## Seguridad operacional

- Tokens nunca en firmware ni logs.
- Contraseña Wi‑Fi guardada solo como preferencia; no se propaga a la cámara salvo reinicio manual.
- microSD: borrado selectivo de frames/burst subidos; clips se podan por `MAX_LOCAL_CLIPS=100` y `MAX_SD_PERCENT=80%`.
- HTTPS en túnel; servidor local HTTP (aislado). Validación de certificado desactivada en cámara (`setInsecure()`) por prototipo.

## Observabilidad

- Logs serie: `CAM_EVIDENCE_SYNC_V2`, `SD_MOUNT_ATTEMPT`, `SD_OK`, `SD_PROBE_OK`, `FLASH_ON/OFF`, `FRAME_READY`, `BURST_UPLOAD`, `HTTP_READY`.
- Endpoints diagnóstico: `/health`, `/sd-status`, `/sd-recover` (cámara), `/clip-status` (cámara).
- Carpeta `diagnostics/` con resultados de pruebas automatizadas y capturas de validación.