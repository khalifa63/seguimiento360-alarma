# ESP32-CAM Evidence

Guía de la base Arduino del prototipo aislado de evidencia visual, su compilación y las comprobaciones que deben hacerse antes de actualizar una cámara activa.

## Base del proyecto

- Placa AI-Thinker ESP32-CAM, sensor OV2640 y microSD en modo de un bit.
- [Sketch Arduino de recuperación](../../firmware/esp32cam-evidence/arduino/sd-recovery-firmware/sd-recovery-firmware.ino), basado en la fuente del 23 de noviembre de 2026. La fuente original local coincidía byte por byte con la del servidor (SHA-256 `676ce40c1f2da00918b86dc61dd1c2bd142262a6913bc10c9dfa879686d32bd0`). La copia versionada separa cuatro valores de despliegue sin cambiar la lógica.
- El árbol ESP-IDF del mismo directorio es experimental: se conserva como referencia, no sustituye al Arduino ni está validado para flasheo.

## Cámara, servidor e interfaz

La cámara captura imágenes, ráfagas y clips como secuencias JPEG. El servidor Node.js del prototipo gestiona la configuración, los comandos y la galería web. Su implementación y la interfaz desplegada no se incluyen en esta base. El sketch conserva las consultas a `GET /api/capture-command` y los reportes a `POST /api/camera-status`; no se cambia la sincronización ni el servidor de la alarma. La microSD permite almacenar secuencias y comprobar montaje, escritura y lectura sin formateo automático.

El botón **Informe cámara** pertenece a la interfaz y llama a `GET /api/cam-report` en el servidor. El sketch no implementa `/cam-diag`, la ruta que el informe intenta consultar. Una descarga correcta del informe no confirma que la cámara haya respondido ni identifica el firmware instalado. Si el diagnóstico o la microSD aparecen vacíos, no los interpretes como un resultado de salud.

## Compilación sin flashear

Usa Arduino CLI con el core Arduino-ESP32 `3.3.11`. Desde la raíz del repositorio, en Bash:

```bash
arduino-cli core install esp32:esp32@3.3.11
cd firmware/esp32cam-evidence/arduino/sd-recovery-firmware
cp -n deployment_config.h.example deployment_config.h
arduino-cli compile --fqbn esp32:esp32:esp32cam \
  --build-property build.code_debug=3 \
  --build-property build.warn_data_percentage=75 .
```

La plantilla basta para probar la compilación, no para desplegar. Para un entorno autorizado, configura localmente el host, los dos tokens y el enlace de galería. El archivo real se ignora en Git; no compartas binarios compilados con valores reales. La conexión Wi‑Fi se guarda en las preferencias de la placa, separada de esta cabecera.

## Validación y mantenimiento

La documentación operativa del servidor registra pruebas anteriores de montaje y lectura/escritura de microSD, una foto, una ráfaga de cinco y un clip corto de dos fotogramas. Esas pruebas no demuestran estabilidad prolongada ni validan directamente un binario recompilado. La primera recompilación de la fuente histórica generó un binario distinto del artefacto etiquetado como flasheado el 23 de noviembre; la coincidencia exacta con la flash no se da por supuesta. El 24 de noviembre se compiló también el sketch publicable con la plantilla y con la configuración local; la tabla de particiones generada coincide con la del artefacto histórico, pero aún debe cotejarse con la placa antes de actualizarla.

1. Confirma que no haya una captura en curso y acuerda una ventana de mantenimiento. Identifica la placa por su MAC, no por el nombre del puerto; entrar al cargador serie puede interrumpir el servicio.
2. Respalda la flash, incluidas tabla de particiones y configuración persistente. Verifica tamaño y huella del respaldo y conserva un método de restauración. Trata el respaldo como secreto.
3. Flashea solo el artefacto preparado para ese equipo, sin borrado general. Si cambia la tabla de particiones, detente y verifica compatibilidad antes de escribir. No uses el binario compilado con la plantilla.
4. Verifica reconexión al servidor, captura individual, ráfaga, clip y estado de microSD. Comprueba la galería y anota por separado los límites de **Informe cámara**. Si no se recupera la sincronización, usa el respaldo en la ventana acordada.

## Límites de seguridad

Es un prototipo para red aislada: no valida el certificado TLS saliente, hay rutas HTTP locales sin autenticación y la microSD no está cifrada. No expongas la cámara directamente a Internet ni trates los archivos como evidencia con integridad forense. No guardes tokens, respaldos, registros privados ni datos de una instalación en el repositorio.
