# Despliegue y Actualización Segura

## Principios

- **Cero tiempo de inactividad de la alarma**: `/home/antonio/iot-alarma/` intocable.
- **Respaldos atómicos** antes de cada cambio.
- **Verificación de identidad** del puerto USB antes de flashear.
- **Comprobación de salud** tras cada despliegue.

## Estructura de directorios (servidor)

```
/home/antonio/camera-evidence-prototype/
├── server.mjs                    # Servidor principal
├── public/
│   ├── index.html                # PWA shell
│   ├── app.js                    # Lógica UI
│   └── manifest.webmanifest      # PWA manifest
├── esp32cam-evidence-prototype/  # Firmware Arduino (legacy)
├── data/
│   ├── camera.token              # Secreto cámara (no versionar)
│   ├── web.token                 # Secreto web (no versionar)
│   ├── camera-config.json        # Config persistente
│   ├── camera-command.json       # Cola comandos
│   └── camera-status.json        # Estado operación
├── diagnostics/                  # Resultados de pruebas
├── backups/                      # Respaldos por despliegue
│   ├── YYYYMMDD-HHMMSS/          # Timestamp UTC
│   │   ├── server.mjs
│   │   ├── public/app.js
│   │   ├── public/index.html
│   │   ├── camera-config.json
│   │   └── esp32cam-.../before.ino
│   └── ...
├── build/                        # Artefactos compilados (no versionar)
├── build-sd-recovery/            # Build firmware recuperación SD
├── prototype.log                 # Log stdout/stderr servidor
├── prototype.pid                 # PID proceso servidor
└── restart_sync.py               # Script reinicio seguro
```

## Despliegue de servidor (Node.js)

### Script `restart_sync.py` (ya instalado)
```python
# Verifica sintaxis, localiza PID en :8787, confirma cwd == prototype,
# mata proceso, arranca detached con log a prototype.log
```

**Uso manual**:
```bash
cd /home/antonio/camera-evidence-prototype
python3 restart_sync.py
# Verifica: curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/health
```

### Despliegue paso a paso (ejemplo: actualización UI)

```bash
cd /home/antonio/camera-evidence-prototype

# 1. Backup atómico con timestamp
TS=$(date -u +%Y%m%d-%H%M%S)
mkdir -p backups/deploy-$TS
cp server.mjs public/app.js public/index.html data/camera-config.json backups/deploy-$TS/

# 2. Verificar sintaxis
node --check server.mjs
node --check public/app.js

# 3. Reemplazar archivos (atómico con mv)
cp nuevos/server.mjs server.mjs.new && mv server.mjs.new server.mjs
cp nuevos/app.js public/app.js.new && mv public/app.js.new public/app.js
cp nuevos/index.html public/index.html.new && mv public/index.html.new public/index.html

# 4. Reiniciar servicio
python3 restart_sync.py

# 5. Verificar salud
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/health
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/api/config | jq .activeProfile
```

## Despliegue de firmware (ESP32-CAM)

### Identificación segura del puerto USB

```bash
# Listar puertos serie con identificadores
python3 -m serial.tools.list_ports -v

# Buscar CH340/CH341 (VID:PID 1a86:7523)
# Verificar por-path: /dev/serial/by-path/pci-0000:00:14.0-usb-0:1:1.0-port0
# Confirmar MAC con esptool:
/home/antonio/.arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool --port /dev/ttyUSB0 chip_id
# Debe mostrar MAC ec:64:c9:ac:c6:b8 (cámara conocida)
```

> **Nunca** flashear sin confirmar MAC. El otro USB puede ser la alarma.

### Compilación

```bash
cd /home/antonio/camera-evidence-prototype
arduino-cli compile --fqbn esp32:esp32:esp32cam \
  --output-dir build esp32cam-evidence-prototype
# Sketch ~1.23 MB, globals ~61 KB
```

### Flasheo

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32cam \
  --upload-property upload.speed=115200 --input-dir build
```

- Verificar hash del binario escrito (esptool lo hace automático).
- Reset duro via RTS al final.

### Verificación post-flash

```bash
# Monitor serie (no mantener abierto durante pruebas de usuario)
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200 --filter direct

# Esperar:
# CAM_EVIDENCE_SYNC_V2
# SD_MOUNT_ATTEMPT attempt=1 khz=20000 format=0
# SD_OK MB=... used=... clips=... khz=20000
# WIFI_OK IP=192.168.10.10 RSSI=...
# HTTP_READY
```

### Verificación remota (desde servidor)

```bash
TOKEN=$(cat /home/antonio/camera-evidence-prototype/data/web.token)
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/health
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/sd-status | jq .
curl -H "X-Web-Token: $TOKEN" http://127.0.0.1:8787/api/config | jq '.activeProfile,.profiles.day.exposureValue'
```

## Despliegue de firmware con recuperación microSD (versión `sd-recovery`)

```bash
# Build específico
arduino-cli compile --fqbn esp32:esp32:esp32cam \
  --output-dir build-sd-recovery sd-recovery-firmware

# Flashear (mismo puerto confirmado)
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32cam \
  --upload-property upload.speed=115200 --input-dir build-sd-recovery

# Verificar logs de arranque
python3 monitor-sd-usb0.py
# Debe mostrar:
# SD_MOUNT_ATTEMPT attempt=1 khz=20000 format=0
# SD_OK MB=30000 used=40 clips=32 khz=20000
# SD_PROBE_OK path=/diagnostics/probe-... bytes=25
```

## Compilación y flasheo (ESP-IDF v5.x — nuevo firmware)

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

## Pruebas de aceptación post-despliegue

```bash
# Suite automática (imagen, ráfaga, clip, integridad)
python3 full-post-sd-test.py
# Debe reportar: IMAGE_PASS, BURST_PASS 5, CLIP_PASS 2, RESTORED_AND_PRESERVED

# Verificación pública (a través de Cloudflare)
curl -H "X-Web-Token: $TOKEN" -H "User-Agent: Mozilla/5.0 CameraEvidenceAcceptanceTest/1.0" \
  https://camera.seguimiento360.com/health
curl -H "X-Web-Token: $TOKEN" -H "User-Agent: Mozilla/5.0 CameraEvidenceAcceptanceTest/1.0" \
  https://camera.seguimiento360.com/app.js?v=20261123-day-night-1 | sha256sum
# Comparar con hash local de public/app.js
```

## Rollback rápido

### Servidor
```bash
# Último backup
LAST=$(ls -t /home/antonio/camera-evidence-prototype/backups/deploy-* | head -1)
cp $LAST/server.mjs /home/antonio/camera-evidence-prototype/server.mjs
cp $LAST/public/app.js /home/antonio/camera-evidence-prototype/public/app.js
cp $LAST/public/index.html /home/antonio/camera-evidence-prototype/public/index.html
python3 restart_sync.py
```

### Firmware (Arduino legacy)
```bash
# Backup de firmware previo (guardado en backups/.../before.ino)
cp /home/antonio/camera-evidence-prototype/backups/sd-recovery-.../before.ino \
   /home/antonio/camera-evidence-prototype/esp32cam-evidence-prototype/esp32cam-evidence-prototype.ino
arduino-cli compile --fqbn esp32:esp32:esp32cam --output-dir build ...
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32cam --input-dir build
```

### Firmware (ESP-IDF v5.x — nuevo)
```bash
# Rollback a factory partition
esptool.py --port /dev/ttyUSB0 erase_region 0x13000 0x130000
# O flashear backup .bin conocido bueno
idf.py -p /dev/ttyUSB0 flash
```

### Configuración
```bash
# Restaurar config anterior
cp /home/antonio/camera-evidence-prototype/backups/deploy-.../camera-config.json \
   /home/antonio/camera-evidence-prototype/data/camera-config.json
# El servidor recarga en siguiente GET /api/config
```

## Checklist pre-despliegue

- [ ] Backup `data/` y `backups/deploy-<timestamp>/`
- [ ] Sintaxis `node --check` en `server.mjs` y `public/app.js`
- [ ] Puerto USB identificado por MAC `ec:64:c9:ac:c6:b8`
- [ ] No hay captura activa (`/api/camera-status` → `idle` o `complete`)
- [ ] Alarma verificada funcionando (`docker ps` en `iot-alarma`)
- [ ] Tokens disponibles en `data/*.token`

## Checklist post-despliegue

- [ ] `/health` → 200 OK
- [ ] `/sd-status` → `ready:true`, `write_read_verified:true`
- [ ] `/api/config` → `activeProfile` y perfiles coherentes
- [ ] Prueba imagen/ráfaga/clip → `*_PASS`
- [ ] Evidencia previa preservada (cuenta final ≥ inicial)
- [ ] Interfaz pública accesible en `https://camera.seguimiento360.com`
- [ ] Alarma intacta (`docker ps` en `iot-alarma`)

## Contacto y escalado

- Logs servidor: `/home/antonio/camera-evidence-prototype/prototype.log`
- Logs cámara: monitor serie 115200 (abrir solo para diagnóstico, cerrar tras uso)
- Diagnósticos guardados: `/home/antonio/camera-evidence-prototype/diagnostics/`
- Respaldos: `/home/antonio/camera-evidence-prototype/backups/`