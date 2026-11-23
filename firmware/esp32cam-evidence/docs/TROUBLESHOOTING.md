# Troubleshooting — Diagnóstico y Soluciones

## MicroSD

| Síntoma | Causa probable | Verificación / Solución |
|---------|----------------|--------------------------|
| `ready:false` en `/sd-status` | Tarjeta no montada, contactos sucios, tarjeta dañada | 1. `POST /sd-recover` (requiere token cámara). 2. Reinsertar tarjeta física. 3. Probar otra tarjeta. |
| `mount_attempts` > 1 | Reintentos de montaje | Normal si hubo corte de alimentación; `clock_khz` muestra frecuencia exitosa. |
| `write_read_verified:false` | Fallo sonda escritura/lectura | Tarjeta en solo-lectura, sectores dañados, o FS corrupto. Formatear externamente (FAT32) y restaurar. |
| “No se pudo preparar la ráfaga en SD” | `mkdir` falló en `/burst-<opId>/` | Verificar `ready:true` y espacio; `POST /sd-recover`. |
| Clips incompletos / frames faltantes | Escritura lenta, intervalo muy corto | Aumentar `clipIntervalMs` ≥ 1000 ms; microSD 1-bit ≈ 1 frame/s real. |
| `used_mb` crece sin límite | Poda no ejecutada | `MAX_LOCAL_CLIPS=100`, `MAX_SD_PERCENT=80%`; invocar `POST /free-space` o esperar nuevo clip. |

### Endpoints de diagnóstico microSD
- `GET /sd-status` (Web) → `sdReport()` completo.
- `POST /sd-recover` (Cámara, `X-Camera-Token`) → fuerza `initSD()+probeSD()`.
- `GET /health` → confirma servicio vivo.

## Wi‑Fi y conectividad

| Síntoma | Causa | Solución |
|---------|-------|----------|
| Cámara no aparece en red | SSID/pass incorrectos, AP mode activo | Verificar `data/camera-config.json`; credenciales son preferencia, no auto-aplicadas. Reiniciar cámara tras cambio. |
| `staConnected:false` | Fallo asociación | RSSI bajo, canal congestionado, MAC filtering. Probar AP `CAM-PROTOTYPE` para diagnóstico local. |
| Timeouts de subida | Latencia / túnel Cloudflare | Timeouts firmware: handshake 4 s, connect 4 s, read 4 s. Verificar túnel y DNS. |
| Servidor no recibe comandos | Token inválido, reloj desfasado | Comparar `camera.token` en cámara y servidor; tokens SHA-256 constante-tiempo. |

## Flash LED

| Síntoma | Causa | Solución |
|---------|-------|----------|
| Flash no enciende | GPIO4 defectuoso, corte seguridad | Verificar `FLASH_ON` en serie; `flashDurationMs` ≥ 50 ms; `esp_timer` 1.5 s corte. |
| Flash queda encendido | Fallo `flashOff()` | Corte seguridad por timer; reiniciar cámara. |
| Imagen oscura con flash | `flashDurationMs` muy corto, flashForClips=false | Aumentar `flashDurationMs` (50–500 ms); `flashForClips=true` solo si necesario. |
| Destello visible en clip | `flashForClips=true` accidental | Desactivar en perfil Noche salvo prueba expresa. |

## Exposición e imagen

| Síntoma | Causa | Ajuste |
|---------|-------|--------|
| Imagen muy oscura (noche) | Exposición baja, ganancia baja, flash off | Perfil Noche: `exposureMode=manual`, `exposureValue=200–300`, `sensorGain=4`, `flashEnabled=true`. |
| Imagen lavada / sobreexpuesta (día) | Exposición alta, ganancia alta | Perfil Día: `exposureMode=auto`, `gainCeiling=2`, `flashEnabled=false`. |
| Bandas horizontales / ruido | Ruido de alimentación, clock sensor, ganancia alta | Clock 10 MHz ya aplicado; bajar `gainCeiling`; condensadores 470 µF + 100 nF cerca de 5 V/GND. |
| Estela de movimiento (motion blur) | Exposición larga | Bajar `exposureValue` (100–200); aceptar más ruido. |
| Rostro oscuro contra fondo claro | Luz trasera, medición matriz | Iluminar rostro frontalmente; `lowLightMode=night` + flash; evitar ventana detrás. |

## Ráfaga y clip

| Síntoma | Causa | Solución |
|---------|-------|----------|
| Ráfaga entrega < `burstCount` | microSD llena, escritura lenta, intervalo corto | Verificar `sd-status`; `burstIntervalMs ≥ 1000`; limpiar clips antiguos. |
| Clip `frames=0` o `complete:false` | Deadline alcanzado sin frames, subida fallida | `clipIntervalMs ≥ 2000` para clips fiables; revisar `clip-status` en cámara. |
| Frames de clip desordenados | Nombres `f001.jpg`… lexicográficos | Manifiesto `/api/clips/:id` devuelve array ordenado; UI usa ese orden. |
| Intervalo real > solicitado | Escritura microSD + subida secuencial | Esperado: microSD 1-bit + HTTP secuencial ≈ 1–2 s/frame. |

## Servidor y UI

| Síntoma | Causa | Solución |
|---------|-------|----------|
| “Acceso protegido: abre el enlace autorizado” | Token web ausente/inválido | Usar `?token=<web-token>` o cabecera `X-Web-Token`. |
| Panel de captura no se cierra | Evidencia eliminada tras operación completa | Fix desplegado: detecta IDs ausentes y cierra panel. Recargar `Ctrl+F5`. |
| Contraseña Wi‑Fi visible | Checkbox “Mostrar contraseña” | Solo revela texto en campo; guardado sigue oculto. |
| Galera vacía tras borrado | Correcto | 91 elementos eliminados en última limpieza. |
| Configuración no guarda | Cambios sin guardar (`configDirty`), validación falla | Completar campos requeridos; `step=1` en ms permite cualquier entero. |

## Comandos de diagnóstico rápidos (SSH en servidor)

```bash
# Estado microSD y sonda
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/sd-status

# Configuración actual
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/api/config | jq .

# Evidencia
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/api/evidence | jq '.evidence | length'

# Operación activa
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/api/camera-status | jq .

# Logs de arranque cámara (serie)
tail -f /home/antonio/camera-evidence-prototype/prototype.log

# Último backup
ls -lt /home/antonio/camera-evidence-prototype/backups/ | head
```

## Logs de serie clave (cámara)

```
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

- `SD_MOUNT_ATTEMPT` repetido → contactos / tarjeta.
- `FRAME_READY capture_ms` ~700–800 ms típico.
- `aec`/`gain` confirman aplicación de exposición/ganancia.
- `SD_PROBE_OK` → escritura/lectura verificada.

## Restablecimiento seguro

1. **Solo configuración**: `POST /api/config` con valores conocidos.
2. **Solo evidencia**: `DELETE /api/evidence/...` o `DELETE /api/clips/...`.
3. **Firmware**: flashear versión conocida buena (`idf.py flash`).
4. **Servidor**: `systemctl restart camera-prototype` (o script `restart_sync.py`).
5. **MicroSD**: formatear externamente FAT32, conservar estructura `/clips`, `/diagnostics`.

> **Nunca** formatear desde firmware (`format_on_failure:false`). Respaldar `data/` antes de cualquier acción destructiva.