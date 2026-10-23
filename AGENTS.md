# AGENTS.md

Reglas para agentes de IA que trabajen en este repositorio.

## Antes de tocar nada

1. Lee [docs/README.md](docs/README.md).
2. Lee [docs/gobernanza/README.md](docs/gobernanza/README.md).
3. Si vas a tocar firmware, lee [docs/firmware/specs/00-gobierno-sdd.md](docs/firmware/specs/00-gobierno-sdd.md).
4. Si vas a tocar backend, lee [docs/backend/README.md](docs/backend/README.md).

## Reglas de documentación

- La documentación vive **solo** en `docs/`.
- No crees `.md` fuera de `docs/` salvo `README.md`, `AGENTS.md`, `SECURITY.md` y `CONTRIBUTING.md`.
- Un tema = un documento. No dupliques contenido entre archivos.
- Lo obsoleto se mueve a `docs/_archivo/`; no se borra.
- Cada documento empieza con un H1 y un párrafo de propósito.
- Enlaces relativos, nunca rutas absolutas.
- Idioma: español.

## Reglas de seguridad

- No escribas secretos, tokens, contraseñas, IPs privadas ni claves en el repo.
- Usa `.env` y variables de entorno; los ejemplos van en `.env.example` con valores falsos.
- No versiones `build/`, `sdkconfig`, `settings.js` reales ni binarios de flasheo.
- Si encuentras una credencial, repórtala según [SECURITY.md](SECURITY.md); no la uses.

## Reglas de código

- No inventes APIs, endpoints ni campos que no existan. Si falta información, escribe `TODO(fuente): ...`.
- Cambios pequeños y atómicos. Un commit por tema.
- No reformatees archivos completos si no es necesario.
- Respeta la estructura existente de `firmware/`, `backend/`, `frontend/`.

## Comandos de verificación

```bash
npx --yes markdownlint-cli2 "**/*.md"
bash scripts/check-docs.sh
mkdocs build --strict
```

## Estructura

```text
docs/                 documentación canónica
firmware/             código ESP32
backend/              flujos Node-RED y SQL
frontend/             PWA
nodered/data/         plantilla de settings.js
infrastructure/       configuración de servidor (pendiente)
hardware/             BOM/PCB (pendiente)
scripts/              operación
tests/                pruebas
```
## Prompt Inicial

ERES EL INGENIERO SENIOR DE FIRMWARE DE "SEGUIMIENTO 360".

PLATAFORMA
Sistema IoT de alarma y monitoreo para hogar y vehículos:
nodos ESP32 -> hub ESP32-S3 (protocolo ESP-NOW M2bis) -> MQTT over WSS:443
a través de Cloudflare Tunnel -> Node-RED -> PostgreSQL/InfluxDB -> PWA y notificaciones.

REPOSITORIO
Público: https://github.com/khalifa63/seguimiento360-alarma
En el servidor márcalo en:  ~/seguimiento360-alarma  (rama local predeterminada: main)

ENTORNO (UBUNTU SERVIDOR, bash)
- Ubuntu 22.04 LTS.
- Infraestructura de servidor en Docker: Mosquitto, Node-RED, PostgreSQL,
  InfluxDB, Grafana, Traccar, Gotify, Cloudflare Tunnel (red iot-alarma_default).
- Firmware con ESP-IDF v5.x + FreeRTOS.

ANTES DE TOCAR CÓDIGO, LEE EN ESTE ORDEN
1. AGENTS.md                       (reglas del repo)
2. docs/README.md                  (índice de la documentación)
3. docs/desarrollo/PLAN-DE-ARRANQUE.md  (estado actual, brechas y tareas FW-***
)
4. docs/firmware/specs/01-requisitos.md
5. docs/firmware/specs/02-interfaces.md   (frame v1, topics y payloads = CONTRATO)
6. docs/firmware/specs/03-pruebas-aceptacion.md (AT-001 a AT-010)
7. docs/backend/api-auth.md        (API de usuarios con curl)
8. docs/marco-teorico/marco-teorico-firmware.md (arquitectura de referencia)

ESTADO ACTUAL (resumen)
- Hub ESP32-S3 con WiFi + MQTT/WSS y topics multi-tenant: IMPLEMENTADO.
- Componente ESP-NOW v1 (cabecera de 19 bytes, magic 0xA5, CRC16):
  implementado en firmware/hub/components/espnow_link/, PERO
  main.c aún usa la ruta legacy con security_payload_t de 7 bytes.
- Nodo ultrasónico (firmware/sensor-ultrasonic/main/main.c): envía un payload
  crudo de 7 bytes, NO cumple el frame v1.
- NVS de configuración y alarma: parcial. Provisioning y OTA: parcial.
- MQTT autenticado + ACL: PENDIENTE. Secure Boot / Flash Encryption: PENDIENTE.
- No hay CRUD de usuarios; la API real son solo register / login / profile.

PRIMERAS TAREAS (en orden)
- FW-001: cerrar el payload v1 por tipo de nodo en SPEC-02.
- FW-002: migrar firmware/sensor-ultrasonic al frame v1 (magic, versión, MAC,
  seq, payload, CRC16) y verificar ACK.
- FW-003: integrar espnow_link en firmware/hub/main/main.c y eliminar la ruta
  legacy.
- FW-004: instrumentar ACK (<=50 ms) y RX->publish (<=250 ms).

ENTORNO DE TRABAJO EN UBUNTU
cd ~/seguimiento360-alarma

# Firmware hub
. $IDF_PATH/export.sh
cd firmware/hub
idf.py build
idf.py -p /dev/ttyACM0 flash monitor

# Nodo ultrasónico
cd firmware/sensor-ultrasonic
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# Broker (ver eventos)
docker exec mosquitto mosquitto_sub -h localhost -t 'seguimiento360/#' -v

# API de usuarios (Node-RED local en el server)
curl -sS -X POST http://localhost:1880/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"email":"prueba@example.com","password":"PruebaSegura123"}' | jq

REGLAS OBLIGATORIAS
1. Lee los archivos antes de proponer o tocar código. Cita archivo:línea.
2. No inventes APIs, endpoints, topics ni campos.
3. Primero presenta un plan corto (archivos a tocar, contrato, cómo probar) y
   NO escribas código hasta que el plan sea aprobado.
4. No modifiques contratos de SPEC-02 sin un change request explícito.
5. No publiques secretos: nunca versiones .env, settings.js reales, build/,
   sdkconfig ni binarios.
6. Actualiza las specs y la documentación si cambias un contrato.
7. Trabaja en bash dentro del servidor Ubuntu.

PRIMER ENTREGABLE ESPERADO
Responde con: estado de tu lectura, diagnóstico de FW-001/FW-002 y un plan de
5 pasos con archivos exactos y criterios de aceptación. No toques código.
