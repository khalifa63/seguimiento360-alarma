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

La documentación operativa del servidor registra pruebas anteriores de montaje y lectura/escritura de microSD, una foto, una ráfaga de cinco y un clip corto de dos fotogramas. Esas pruebas no demuestran estabilidad prolongada. La recompilación no reprodujo byte por byte el artefacto del 23 de noviembre: no se debe confundir correspondencia del código con reproducibilidad binaria.

### Validación del 24 de noviembre de 2026

- Se compiló el sketch publicable con la plantilla y con la configuración local, usando Arduino-ESP32 `3.3.11`.
- Antes de actualizar se confirmó la identidad de la placa, se respaldaron sus 4 MiB de flash y se comprobó que la aplicación leída coincidía byte por byte con el artefacto histórico del 23 de noviembre. La tabla de particiones también coincidía con la del candidato. El respaldo privado no forma parte del repositorio.
- Se flasheó la aplicación del commit `1da2d99` en `0x10000`, sin borrado general ni escritura de NVS, bootloader o tabla de particiones. La herramienta verificó el hash después de escribir. Tras reiniciar se observaron conexión Wi-Fi, inicio HTTP y recepción periódica de configuración y comandos.
- El usuario confirmó foto, visualización de las cinco imágenes de la ráfaga y funcionamiento del clip. El servidor confirmó la ráfaga completa (5/5) y el clip completo (30/30), con subidas confirmadas y sin comando pendiente tras el clip.

Se acepta esta base para continuar el desarrollo, no como una certificación de estabilidad o de seguridad. Quedan pendientes:

- La primera ráfaga permaneció unos cinco minutos sin actualizarse. Coincidió con un nuevo arranque observado al abrir el puerto serie y terminó después; no se ha determinado la causa del retraso ni si abrir el puerto provocó ese reinicio.
- La microSD falló al montar tras un arranque y pudo utilizarse en la ráfaga y el clip posteriores. No se formateó; falta diagnosticar la intermitencia.
- Las consultas HTTP directas desde el servidor agotaron su tiempo de espera, aunque la comunicación saliente de la cámara y las capturas por la interfaz funcionaron. El diagnóstico de red local sigue pendiente.
- El botón Informe cámara conserva las limitaciones descritas arriba. No se añadió `/cam-diag`.

Evita abrir el puerto serie durante pruebas funcionales: incluso sin solicitar reset explícito, el adaptador puede alterar las líneas de control. No reinicies ni envíes capturas de prueba cuando el usuario esté validando una operación.

1. Confirma que no haya una captura en curso y acuerda una ventana de mantenimiento. Identifica la placa por su MAC, no por el nombre del puerto; entrar al cargador serie puede interrumpir el servicio.
2. Respalda la flash, incluidas tabla de particiones y configuración persistente. Verifica tamaño y huella del respaldo y conserva un método de restauración. Trata el respaldo como secreto.
3. Flashea solo el artefacto preparado para ese equipo, sin borrado general. Si cambia la tabla de particiones, detente y verifica compatibilidad antes de escribir. No uses el binario compilado con la plantilla.
4. Verifica reconexión al servidor, captura individual, ráfaga, clip y estado de microSD. Comprueba la galería y anota por separado los límites de **Informe cámara**. Si no se recupera la sincronización, usa el respaldo en la ventana acordada.

## Límites de seguridad

Es un prototipo para red aislada: no valida el certificado TLS saliente, hay rutas HTTP locales sin autenticación y la microSD no está cifrada. No expongas la cámara directamente a Internet ni trates los archivos como evidencia con integridad forense. No guardes tokens, respaldos, registros privados ni datos de una instalación en el repositorio.
