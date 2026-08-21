# HUB-S3 Firmware — Hito M1: Fundación BSP y NVS

Firmware base del Hub ESP32-S3 del proyecto **Seguimiento 360**.

**Estado:** M1 completado (AT-05 cubierta).
**Specs aplicables:** A1 v0.2, A2 v0.2, A3 v0.1, A4 v0.2, M1-SPEC-IMPL v0.2.

## Estructura

~~text
firmware/hub/
├── CMakeLists.txt              # Raíz del proyecto
├── partitions.csv              # 7 particiones (3 NVS segregadas)
├── sdkconfig.defaults          # Config ESP32-S3 + PSRAM + WDT
├── README.md                   # Este archivo
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # app_main + tarea monitor M1
└── components/hub_core/
    ├── CMakeLists.txt
    ├── include/
    │   ├── bsp.h               # GPIO, ADC, ISR tamper, contratos §6
    │   └── nvs_manager.h       # 3 particiones NVS
    └── src/
        ├── bsp.c
        └── nvs_manager.c
~~

## Requisitos

- ESP-IDF v5.x (probado con v5.2+)
- Placa ESP32-S3-DevKitC-1 (o PCB HUB-S3 v1)
- Herramientas: idf.py, python3

## Build + Flash

~~bash
cd firmware/hub

# 1. Activar entorno ESP-IDF
. $IDF_PATH/export.sh

# 2. Configurar target (solo primera vez)
idf.py set-target esp32s3

# 3. Compilar
idf.py build

# 4. Flashear (ajusta puerto)
idf.py -p /dev/ttyUSB0 flash

# 5. Monitor
idf.py -p /dev/ttyUSB0 monitor
~~

## Validación de Aceptación — AT-05

### AT-05.1 — Integridad NVS (3 particiones)
**Esperado en el log:**
~~text
I (xxx) NVS_MGR: Partición 'nvs' inicializada
I (xxx) NVS_MGR: Partición 'nvs_state' inicializada
I (xxx) NVS_MGR: Partición 'nvs_nodes' inicializada
I (xxx) MAIN: [M1] Device ID persistente: 'HUB-S3-0001'
I (xxx) MAIN: [M1] Estado alarma persistente: 0 (0=disarmed)
~~
**Prueba de persistencia:** reinicia con el botón EN; los valores deben mantenerse.

### AT-05.2 — Power Loss (ADC)
Baja el voltaje de alimentación por debajo del umbral (POWER_LOSS_ADC_THRESHOLD en bsp.c).
~~text
W (xxx) BSP: Power loss! ADC raw=NNNN (< 2500)
E (xxx) MAIN: [M1] POWER LOSS inminente! Commit NVS de emergencia...
W (xxx) NVS_MGR: Emergency commit completado
~~

### AT-05.3 — Tamper físico (ISR)
Conecta momentáneamente GPIO2 a GND (simula apertura de carcasa).
~~text
W (xxx) MAIN: [M1] TAMPER EVENTO! type=0 ts=NNNN ms
~~

### AT-05.4 — Watchdog activo
La tarea t_m1_mon alimenta el WDT cada 500ms. Si se comentara
esp_task_wdt_reset(), el sistema debe resetearse por WDT en ~10s
(CONFIG_ESP_TASK_WDT_TIMEOUT_S=10).

### AT-05.5 — LED heartbeat
- 3 parpadeos rápidos (200ms) al inicio = boot OK
- 1 pulso cada 5s = tarea monitor viva

## Trazabilidad de requisitos (M1)

| ID | Requisito | Ubicación en código |
|---|---|---|
| HUB-F-010 | NVS persistente | nvs_manager.c |
| HUB-F-015 | Tamper físico | bsp.c (ISR) |
| HUB-F-020 | Factory reset | nvs_manager.c::factory_reset |
| HUB-F-030 | Watchdog | main.c::task_m1_monitor |
| HUB-NF-003 | Sin reset manual 30d | WDT + emergency commit |

## Siguiente hito

**M2 — ESP-NOW + MQTT:** componentes espnow_link y mqtt_link, tareas
t_espnow_rx (Core 0, Pri 24) y t_wifi_mqtt (Core 1, Pri 15).
Cierra AT-02 y AT-07.

---
*Generado desde M1-SPEC-IMPL v0.2 · Proyecto Seguimiento 360*
