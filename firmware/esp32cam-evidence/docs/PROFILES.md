# Perfiles Día / Noche — Especificación y Uso

## Concepto

Dos conjuntos independientes de ajustes de captura (`PROFILE_FIELDS`) que comparten parámetros globales. La selección es **manual** — no hay cambio automático por horario, luz ni sensor.

## Campos por perfil (`PROFILE_FIELDS`)

| Campo | Tipo | Rango / Valores | Descripción |
|-------|------|-----------------|-------------|
| `frameSize` | string | `QQVGA`, `QVGA`, `VGA`, `SVGA` | Resolución del sensor |
| `jpegQuality` | int | 10–30 | Calidad JPEG (menor = mejor) |
| `flashEnabled` | bool | `true`/`false` | Flash en imagen y ráfaga |
| `flashForClips` | bool | `true`/`false` | Permitir flash también en clips |
| `flashDurationMs` | int | 50–500 | Estabilización inicial del LED (ms) |
| `lowLightMode` | string | `auto`, `night` | Compensación de poca luz del sensor |
| `gainCeiling` | int | 2, 4, 8, 16, 32 | Techo de ganancia automática (×) |
| `exposureMode` | string | `auto`, `manual` | Modo de exposición |
| `exposureValue` | int | 100–1200 | Valor de exposición manual (unidades sensor) |
| `sensorGain` | int | 0–30 | Índice de ganancia manual (driver) |

## Campos globales (comunes a ambos perfiles)

| Campo | Tipo | Rango / Valores |
|-------|------|-----------------|
| `wifiSsid` | string | ≤32 chars |
| `wifiPassword` | string | ≤64 chars (solo preferencia) |
| `burstCount` | int | 1–10 |
| `burstIntervalMs` | int | 200–5000 |
| `clipIntervalMs` | int | 200–5000 |
| `clipDurationSeconds` | int | 10, 30, 60 |
| `captureOnEvent` | bool | preferencia |
| `manualCapture` | bool | preferencia |

## Estado persistente (NVS / `config.c`)

```json
{
  "wifiSsid": "MI_RED",
  "wifiPassword": "********",
  "burstCount": 5,
  "burstIntervalMs": 1000,
  "clipIntervalMs": 1000,
  "clipDurationSeconds": 30,
  "captureOnEvent": true,
  "manualCapture": true,
  "activeProfile": "night",
  "profiles": {
    "day":  { "frameSize": "SVGA", "jpegQuality": 10, "flashEnabled": false, "flashForClips": false, "flashDurationMs": 120, "lowLightMode": "auto", "gainCeiling": 2, "exposureMode": "auto", "exposureValue": 200, "sensorGain": 4 },
    "night": { "frameSize": "SVGA", "jpegQuality": 10, "flashEnabled": true, "flashForClips": false, "flashDurationMs": 120, "lowLightMode": "auto", "gainCeiling": 8, "exposureMode": "manual", "exposureValue": 200, "sensorGain": 4 }
  }
}
```

## Migración desde configuración legacy (primer arranque / `config_init`)

Si NVS no contiene `profiles` + `activeProfile`:

1. **Noche** clona los ajustes de captura válidos actuales (preserva exposición/ganancia probadas).
2. **Día** crea defaults conservadores:
   - `exposureMode: "auto"`, `exposureValue: 200`, `sensorGain: 4`
   - `gainCeiling: 2`, `lowLightMode: "auto"`, `flashEnabled: false`, `flashForClips: false`
   - Resolución/calidad desde config actual.
3. `activeProfile = "night"` (no cambia comportamiento silenciosamente).
4. Persistencia atómica serializada.

## Selección de perfil activo

- **UI**: dos selectores independientes:
  - “Perfil activo para próximas capturas” → `activeProfile`.
  - “Perfil que estás editando” → borrador visual.
- **Guardado**: un único POST/actualización con `activeProfile` + `profiles` completo + globales.
- **Cámara**: recibe solo campos aplanados del perfil activo; **sin anidamiento**.

## Comportamiento en comandos

| Comando | Config enviada a cámara |
|---------|-------------------------|
| `capture-image` | 10 `PROFILE_FIELDS` del perfil activo + globales comunes |
| `capture-burst` | Idem + `burstCount`, `burstIntervalMs` |
| `capture-clip` | Idem + `clipIntervalMs`, `clipDurationSeconds` |

> El firmware **no conoce** la existencia de perfiles; aplica lo que recibe.

## Validación

- `frameSize` ∈ lista; `jpegQuality` 10–30; `flashDurationMs` 50–500.
- `gainCeiling` ∈ {2,4,8,16,32}; `exposureValue` 100–1200; `sensorGain` 0–30.
- `lowLightMode` ∈ {auto,night}; `exposureMode` ∈ {auto,manual}.
- `activeProfile` ∈ {day,night}; `profiles.day/night` deben ser objetos.
- Error si falla; campos inválidos no guardan nada.

## Persistencia y reinicio

- Escritura atómica en NVS (`config_persist`).
- Reinicio: `config_init()` reaplica migración si falta estructura.
- Reinicio de cámara: `http_poll_command()` trae comando con config efectiva actual.

## Estado actual desplegado (resumen)

| | **Día** | **Noche** |
|---|---------|-----------|
| `frameSize` | SVGA | SVGA |
| `jpegQuality` | 10 | 10 |
| `flashEnabled` | false | true |
| `flashForClips` | false | false |
| `flashDurationMs` | 120 | 120 |
| `lowLightMode` | auto | auto |
| `gainCeiling` | 2 | 8 |
| `exposureMode` | auto | manual |
| `exposureValue` | 200 | 200 |
| `sensorGain` | 4 | 4 |

- `activeProfile`: **day** (tras pruebas diurnas).
- Globales: `burstCount=5`, `burstIntervalMs=1000`, `clipIntervalMs=1000`, `clipDurationSeconds=30`.

## UI — Detalles de interacción

- Checkbox “Mostrar contraseña que estoy escribiendo” junto al campo Wi‑Fi.
- Edición de un perfil **no** lo activa; requiere Guardar + selector activo.
- Indicador “Perfil guardado para próximas capturas: Día/Noche” bajo botones de captura.
- Captura bloqueada si hay cambios sin guardar (`configDirty`) o guardando (`configSaving`).

## Buenas prácticas

1. Probar cada perfil en su condición real (día/noche) antes de confiar.
2. No confiar en exposición automática para evidencia crítica; usar manual probada.
3. Mantener `flashForClips=false` salvo prueba explícita; clips suelen ser nocturnos.
4. Verificar `sd-status` antes de ráfagas/clips importantes.
5. Respaldar configuración antes de cambios mayores (`config_to_json` → backup).