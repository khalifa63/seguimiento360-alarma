# ESP32-CAM Evidence — Firmware

Firmware para AI-Thinker ESP32-CAM que captura fotos, ráfagas y clips (secuencias JPEG), los guarda en microSD y los sube a un servidor Node.js aislado vía HTTPS. Diseñado como prototipo de evidencia visual para una alarma existente.

## Características

- **Captura**: imagen única, ráfaga (5 fotos por defecto), clip (10/30/60 s).
- **Almacenamiento**: microSD 1-bit como buffer (`/clips`, `/burst-<opId>`, `/diagnostics`).
- **Subida**: HTTP/1.0 + TLS (insecure en prototipo) con `X-Operation-Id` correlación.
- **Flash LED**: GPIO4 activo-alto, corte de seguridad 1.5 s, apagado antes de red.
- **Sensor**: OV2640 a 10 MHz, `fb_count=1`, `CAMERA_GRAB_LATEST`.
- **Exposición/Ganancia**: manual (unidades sensor) o auto con techo configurable.
- **Recuperación microSD**: reintentos 20/10/4/0.4 MHz, sonda escritura/lectura, sin formateo.
- **Perfiles**: Día (auto, flash off) y Noche (manual, flash on), selección manual.
- **Autenticación**: `X-Camera-Token` (cámara), `X-Web-Token`/`?token=` (navegador).

## Estructura

```
esp32cam-evidence/
├── .gitignore
├── tokens.h.example          # Plantilla de secretos (copiar a tokens.h)
├── tokens.h                  # Secretos reales — NO VERSIONAR
├── main/
│   ├── main.c                # Punto de entrada
│   ├── camera.c / .h         # Inicialización y captura
│   ├── sd_card.c / .h        # microSD, recuperación, sonda
│   ├── http_client.c / .h    # HTTPS cliente, subidas
│   ├── config.c / .h         # Configuración, perfiles, NVS
│   └── web_server.c / .h     # HTTP local diagnóstico (/health, /sd-status)
├── components/
│   └── espnow_link/          # (shared) enlace ESP-NOW v1
├── docs/
│   ├── README.md
│   ├── ARCHITECTURE.md
│   ├── FIRMWARE.md
│   ├── SERVER_API.md
│   ├── PROFILES.md
│   ├── USER_GUIDE.md
│   ├── TROUBLESHOOTING.md
│   ├── DEPLOYMENT.md
│   ├── SECURITY.md
│   ├── CHANGELOG.md
│   └── DOCS_INDEX.md
└── CMakeLists.txt
```

## Secretos

1. Copia `tokens.h.example` a `tokens.h`.
2. Edita `tokens.h` con tus valores reales (`CAMERA_TOKEN`, `WEB_TOKEN`).
3. `tokens.h` está en `.gitignore` — **nunca se versiona**.

## Compilación (ESP-IDF v5.x)

```bash
. $IDF_PATH/export.sh
cd firmware/esp32cam-evidence
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Flasheo seguro

```bash
# Identificar puerto por MAC conocida (ec:64:c9:ac:c6:b8)
esptool.py --port /dev/ttyUSB0 chip_id

# Flashear
idf.py -p /dev/ttyUSB0 flash monitor
```

## Documentación

Ver `docs/` para arquitectura, API, perfiles, usuario, troubleshooting, despliegue, seguridad y changelog.

## Licencia

Prototipo interno — uso autorizado dentro de Seguimiento 360.