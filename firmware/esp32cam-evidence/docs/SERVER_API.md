# API del Servidor — Referencia Completa

Base URL local: `http://127.0.0.1:8787`
Base URL pública: `https://camera.seguimiento360.com`

## Autenticación

| Cliente | Cabecera / Query | Descripción |
|---------|------------------|-------------|
| Cámara  | `X-Camera-Token: <token>` | Comparación constante-tiempo SHA-256 contra `data/camera.token` |
| Navegador | `X-Web-Token: <token>` **o** `?token=<token>` | Comparación contra `data/web.token` |

> Los tokens son cadenas ≥32 chars. Nunca aparecen en logs ni repositorio.

## Endpoints

### `GET /health`
Respuesta: `200` `{ "ok": true, "service": "camera-evidence-prototype", "time": "ISO8601" }`

### `GET /api/config`
**Auth**: Web  
**Respuesta**: `200` `{ "ok": true, "config": { ... } }`

**Config object** (campos efectivos + metadatos):
```json
{
  "wifiSsid": "",
  "hasPassword": false,
  "burstCount": 5,
  "burstIntervalMs": 1000,
  "clipIntervalMs": 1000,
  "clipDurationSeconds": 30,
  "captureOnEvent": true,
  "manualCapture": true,
  "activeProfile": "day",
  "profiles": {
    "day": { "frameSize": "SVGA", "jpegQuality": 10, "flashEnabled": false, "flashForClips": false, "flashDurationMs": 120, "lowLightMode": "auto", "gainCeiling": 2, "exposureMode": "auto", "exposureValue": 200, "sensorGain": 4 },
    "night": { "frameSize": "SVGA", "jpegQuality": 10, "flashEnabled": true, "flashForClips": false, "flashDurationMs": 120, "lowLightMode": "auto", "gainCeiling": 8, "exposureMode": "manual", "exposureValue": 200, "sensorGain": 4 }
  }
}
```
- `wifiPassword` **nunca** se devuelve; solo `hasPassword`.
- Los 10 `PROFILE_FIELDS` aparecen bajo cada perfil y también aplanados en la raíz (efectivos del perfil activo).

### `POST /api/config`
**Auth**: Web  
**Body** (parcial o completo):
```json
{
  "activeProfile": "day|night",
  "profiles": {
    "day": { "frameSize": "SVGA", "jpegQuality": 12, ... },
    "night": { ... }
  },
  "wifiSsid": "MI_RED",
  "wifiPassword": "opcional",
  "burstCount": 5,
  "burstIntervalMs": 1000,
  "clipIntervalMs": 1000,
  "clipDurationSeconds": 30,
  "captureOnEvent": true,
  "manualCapture": true
}
```
- `profiles.day` y `profiles.night` deben ser objetos completos si se envían (se fusionan con claves válidas).
- `activeProfile` ∈ `["day","night"]`; error 400 si inválido.
- Campos globales y perfil inactivo se preservan.
- Validación/clamps idénticos a `readConfig()`.
- Escritura atómica (temp + rename) serializada con cola de operaciones.
- **Respuesta**: `200` `{ "ok": true, "config": { ...config actualizada... } }`

### `GET /api/evidence`
**Auth**: Web  
**Respuesta**: `200` `{ "ok": true, "evidence": [ ... ] }`

**Elementos**:
```json
// Imagen
{ "type": "image", "id": "uuid.jpg", "operationId": "uuid|null", "capturedAt": "ISO8601", "bytes": 12345, "image": "/api/evidence/uuid.jpg" }

// Clip
{ "type": "clip", "id": "clip-123", "operationId": "uuid|null", "complete": true, "capturedAt": "ISO8601", "bytes": 67890, "frames": 6, "intervalMs": 1000, "thumbnail": "/api/clips/clip-123/f001.jpg", "manifest": "/api/clips/clip-123" }
```
- Ordenados por `capturedAt` descendente (más reciente primero).
- Clips `complete:false` indican subida incompleta; controles de reproducción/borrado deshabilitados en UI.

### `POST /api/capture-command`
**Auth**: Web  
**Body**: `{ "command": "capture-image" | "capture-burst" | "capture-clip" }`  
**Respuesta**: `200` `{ "ok": true, "operationId": "uuid", "activeProfile": "day|night" }`  
**Conflicto**: `409` si hay operación activa no terminal → body incluye `{ "ok": false, "status": { ...estado actual... } }`  
- El comando lleva **configuración efectiva aplanada** del perfil activo (sin perfiles anidados).
- `operationId` generado por servidor; cámara lo usa en subidas y reportes.

### `GET /api/camera-status`
**Auth**: Web  
**Respuesta**: `200` `{ "ok": true, "status": { ... } }`

**Status object**:
```json
{
  "operationId": "uuid|null",
  "kind": "image|burst|clip",
  "state": "offline|idle|queued|capturing|uploading|complete|error",
  "activeProfile": "day|night",
  "message": "string",
  "current": 0,
  "total": 0,
  "uploaded": 0,
  "expectedUploads": 0,
  "uploadsConfirmed": false,
  "evidenceIds": ["uuid.jpg", "clip-xyz"],
  "startedAt": "ISO8601|null",
  "updatedAt": "ISO8601|null",
  "completedAt": "ISO8601|null",
  "stale": false
}
```
- `stale=true` si `updatedAt` > 15 s y no terminal; **la operación no expira**.
- `total=0` durante captura de clip (se desconoce frames totales hasta el final).

### `POST /api/camera-status` (solo cámara)
**Auth**: Cámara  
**Body**: estado parcial desde firmware  
**Respuesta**: `200` `{ "ok": true, "status": { ...reconciliado... } }`  
- Validación estricta: `operationId`, `kind`, `state` válidos, progreso no regresivo.
- Serializado; conflictos → `409`.

### `POST /api/upload` (solo cámara)
**Auth**: Cámara  
**Headers**: `X-Operation-Id`, `Content-Type: image/jpeg`  
**Body**: JPEG binario  
**Respuesta**: `201` `{ "ok": true, "id": "uuid.jpg", "operationId": "uuid" }`  
- Guarda JPEG en `data/evidence/` + sidecar `.json` con `operationId`, `capturedAt`.
- Actualiza contadores de operación; si completa → `uploadsConfirmed=true`.

### `GET /api/clips/:clipId`
**Auth**: Web  
**Respuesta**: `200` manifiesto del clip
```json
{
  "ok": true,
  "clipId": "clip-123",
  "operationId": "uuid|null",
  "frames": ["/api/clips/clip-123/f001.jpg", "/api/clips/clip-123/f002.jpg", ...],
  "intervalMs": 1000
}
```

### `GET /api/clips/:clipId/:frame`
**Auth**: Web  
**Respuesta**: `200` JPEG binario (`Content-Type: image/jpeg`, `Content-Length`).

### `DELETE /api/evidence/:id.jpg`
**Auth**: Web  
**Respuesta**: `200` `{ "ok": true }`  
- Borra JPEG y sidecar; si clip → borra directorio completo.

### `DELETE /api/clips/:clipId`
**Auth**: Web  
**Respuesta**: `200` `{ "ok": true }`  
- Borra directorio `/clips/clipId/` recursivamente.

### `POST /sd-recover` (solo cámara)
**Auth**: Cámara (`X-Camera-Token`)  
**Body**: `{}`  
**Respuesta**: `200` `sdReport()` si OK, `503` si falla  
- Bloqueado si `clipRecording || clipUploading` → `409`.

## Códigos de estado HTTP

| Código | Significado |
|--------|-------------|
| 200 | OK |
| 201 | Creado (subida JPEG) |
| 400 | JSON inválido, campos fuera de rango, `activeProfile` inválido |
| 401 | Token ausente o inválido |
| 403 | Prohibido (ruta no autenticada sin token) |
| 404 | Evidencia/clip no encontrado |
| 409 | Conflicto: operación activa, estado regresivo, IDs desfasados |
| 413 | Payload demasiado grande (>4 KB estado, >1 KB comando, >900 KB upload) |
| 500 | Error interno (inesperado) |
| 503 | Servicio no disponible (cámara ocupada, microSD no recuperable) |

## Formatos de identificadores

- `operationId`: UUID v4 (`[a-f0-9-]{36}`).
- `evidenceId` (imagen): `UUID.jpg` (`[a-f0-9-]{36}\.jpg`).
- `clipId`: `clip-<uint64>` (`clip-[0-9]{1,20}`).
- `frame`: `f001.jpg` … `f999.jpg` (`f\d{3}\.jpg`).

## Límites y colas

- `MAX_BYTES = 900 KB` por subida individual.
- Cola serializada: comandos, actualizaciones de config, reportes de cámara — **sin carrera**.
- Operaciones activas no expiran; `stale:true` tras 15 s sin actualización.
- Completitud: imágenes → al recibir JPEG; clips → `metadata.complete=true` + frames correlacionados.

## Seguridad

- Tokens comparados con `timingSafeEqual` sobre SHA-256.
- `Cache-Control: no-store` en todas las respuestas JSON.
- CSP en `/`: `default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'`.
- Contraseña Wi‑Fi **nunca** en respuestas; solo `hasPassword`.