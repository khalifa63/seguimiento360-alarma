# Documentación del Prototipo ESP32-CAM Evidence

Este índice agrupa la documentación técnica y de usuario del prototipo aislado de evidencia visual. Los archivos se encuentran en la carpeta `docs/` del repositorio del prototipo.

## Contenido

| Archivo | Descripción |
|---------|-------------|
| [README.md](README.md) | Visión general, requisitos y puesta en marcha rápida. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Arquitectura de hardware, red, firmware y servidor. |
| [FIRMWARE.md](FIRMWARE.md) | Detalle del firmware: inicialización, captura, ráfaga, clip, microSD y recuperación. |
| [SERVER_API.md](SERVER_API.md) | Endpoints HTTP, autenticación, flujo de operaciones y evidencia. |
| [PROFILES.md](PROFILES.md) | Perfiles Día/Noche: campos, selección, migración y persistencia. |
| [USER_GUIDE.md](USER_GUIDE.md) | Uso de la galería PWA: captura, visualización, descarga y eliminación. |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Diagnóstico de microSD, Wi‑Fi, flash, exposición y problemas comunes. |
| [DEPLOYMENT.md](DEPLOYMENT.md) | Despliegue, respaldo, actualización y verificación sin afectar la alarma. |
| [SECURITY.md](SECURITY.md) | Tokens, HTTPS, aislamiento de red y manejo de secretos. |
| [CHANGELOG.md](CHANGELOG.md) | Historial de versiones y cambios relevantes. |

## Estado actual del prototipo (resumen)

- **Hardware**: AI-Thinker ESP32-CAM, microSD 1-bit, flash GPIO4 activo-alto.
- **Firmware**: Arduino-ESP32 3.3.11, sensor OV2640 a 10 MHz, `fb_count=1`, `CAMERA_GRAB_LATEST`.
- **Servidor**: Node.js aislado en `0.0.0.0:8787`, túnel Cloudflare a `https://camera.seguimiento360.com`.
- **Autenticación**: cámara `X-Camera-Token`, navegador `X-Web-Token` o `?token=`.
- **Perfiles**: Día (auto 300/0 flash off) y Noche (manual 200/4 flash on), selección manual.
- **Evidencia**: imagen, ráfaga (5 fotos) y clip (secuencia JPEG) con correlación por `operationId`.
- **microSD**: montaje con reintentos a 20/10/4/0.4 MHz, sondeo escritura/lectura, **sin formateo automático**.
- **UI**: galería PWA con carga diferida, progreso de captura, descarga y borrado; contraseña Wi‑Fi con visibilidad controlable.

> Esta documentación refleja el estado desplegado en `/home/antonio/camera-evidence-prototype` en el servidor `192.168.1.200` y la cámara `192.168.10.10`. No incluye secretos ni credenciales.