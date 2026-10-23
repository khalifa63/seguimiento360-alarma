# Firmware

Documentación de implementación y contratos del firmware. El código vive en `firmware/`; las specs y decisiones viven aquí.

| Documento | Qué contiene |
|---|---|
| [Hub ESP32-S3](hub.md) | Arquitectura M2, tareas, tópicos, build y validación |
| [Spec 00 · Gobierno SDD](specs/00-gobierno-sdd.md) | Reglas del proceso de diseño |
| [Spec 01 · Requisitos](specs/01-requisitos.md) | Requisitos funcionales y no funcionales |
| [Spec 02 · Interfaces](specs/02-interfaces.md) | Contratos entre firmware, hub y nube |
| [Spec 03 · Pruebas de aceptación](specs/03-pruebas-aceptacion.md) | Criterios AT-01 a AT-07 |

## Estructura del código

```text
firmware/
├── hub/                 # gateway ESP32-S3
├── node_base/           # nodo sensor base
├── sensor-ultrasonic/   # sensor ultrasónico
├── sensor-*             # otros nodos
├── shared/              # código compartido
└── node_sim/            # simulador de nodo
```

## Build

```bash
. $IDF_PATH/export.sh
cd firmware/hub
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

No se versionan `build/`, `sdkconfig` ni binarios de flasheo. Solo `sdkconfig.defaults`.
