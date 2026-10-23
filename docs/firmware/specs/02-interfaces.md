# SPEC-02 — Contratos de Interface del Hub (v0.2)

Contratos entre nodo, hub, broker y backend. Es la fuente de verdad: el código
deriva de aquí, no al revés. Un cambio incompatible exige *change request*.

> **Estado:** v0.2 documenta lo que el firmware implementa hoy. La v0.1 describía
> un contrato aspiracional que nunca se implementó; el detalle de la brecha está
> en [Hallazgos](#hallazgos-v01--v02).

## HUB-IF-001: Frame ESP-NOW v1

Ver [Protocolo ESP-NOW](../../hardware/PROTOCOLO_ESPNOW.md) (versión 0x01,
CRC16-CCITT, little endian, anti-replay por `seq` monotónico).

El componente `firmware/hub/components/espnow_link/` lo implementa, pero **el
hub y el nodo aún no lo usan**: ambos hablan el payload crudo de 7 bytes
(HUB-IF-008). La migración es la tarea FW-002.

## HUB-IF-002: Payload ESP-NOW de 7 bytes (vigente)

Contrato **real** entre nodo y hub. Definido de forma idéntica en
`firmware/sensor-ultrasonic/main/main.c` y `firmware/hub/main/main.c`.

    typedef struct {
        uint8_t event_code[4];    // "PAIR", "ALRM", "HEAR"
        uint8_t seq;              // contador 0-255, anti-replay
        uint8_t battery_level;    // 0-100 %
        uint8_t distance_cm;      // distancia medida, 0 si no aplica
    } __attribute__((packed)) security_payload_t;

Ambos archivos declaran `_Static_assert(sizeof(security_payload_t) == 7)`, de
modo que una divergencia de tamaño rompe la compilación.

### ⚠️ El byte 6 tiene dos nombres

| Archivo | Línea | Declaración |
|---|---|---|
| `firmware/hub/main/main.c` | 105 | `uint8_t distance_cm;` |
| `firmware/sensor-ultrasonic/main/main.c` | 36 | `uint8_t reserved;` |

Es el **mismo byte** y el nodo escribe ahí la distancia
(`payload.reserved = distance_cm`), así que hoy funciona. Pero el nombre
`reserved` sugiere un campo libre: quien lo reutilice para otra cosa romperá la
telemetría de distancia **sin que el compilador lo detecte**, porque el
`_Static_assert` solo verifica el tamaño, no el significado.

**Acción (FW-001):** unificar el nombre a `distance_cm` en ambos archivos antes
de congelar el frame v1. No es un cambio de contrato: es corregir la
documentación del contrato en el código.

## HUB-IF-003: Topics MQTT (implementados)

| Uso | Topic | Dirección | QoS |
|---|---|---|---|
| Evento de nodo | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/event` | hub → broker | 1 |
| Estado del hub | `seguimiento360/hub/status` | hub → broker | 1 |
| Comando OTA | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/ota_cmd` | broker → hub | 1 |
| Config de nodo | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/node_cfg` | broker → hub | 1 |

`{mac}` es la MAC del hub en formato de 12 caracteres hexadecimales sin
separadores (por ejemplo `3cdc7562c2b0`), no la forma con dos puntos.

El topic de estado **no lleva prefijo de tenant**: es global al broker. Node-RED
se suscribe a `seguimiento360/hub/status` en consecuencia.

## HUB-IF-004: Payload del evento

Publicado por el hub en el topic de evento:

    {
      "mac": "a0:f2:62:f1:d0:f0",
      "node_name": "Sensor Garaje",
      "location": "Casa Principal",
      "room": "Garaje",
      "event": "ALRM",
      "seq": 42,
      "battery": 95,
      "distance_cm": 195,
      "tenant": "1",
      "site": "1"
    }

`node_name`, `location` y `room` los añade el hub desde su registro de nodos en
NVS; si el nodo no está vinculado, publica `"Nodo Desconocido"`,
`"Sin Ubicación"` y `"Sin Habitación"`.

### `tenant` y `site` van duplicados

El backend puede obtener `tid` y `sid` de dos sitios: del topic o del JSON. El
hub publica ambos. **Si divergen, el topic manda**: los flujos de Node-RED
parsean `msg.topic.split('/')` y usan las posiciones 2 y 4.

## HUB-IF-005: Códigos de evento

| Código | Significado |
|---|---|
| `PAIR` | solicitud de vinculación del nodo |
| `ALRM` | alarma por umbral de distancia |
| `HEAR` | latido periódico |
| `GAS`, `SMOKE`, `FIRE`, `PANIC`, `MOV`, `DOOR`, `SEIS`, `SOS`, `HUM` | reservados para otros tipos de nodo |

Node-RED considera críticos los que empiezan por `ALRM`, `GAS`, `SMOKE`,
`FIRE`, `PANIC`, `MOV`, `DOOR`, `SEIS`, `SOS` o `HUM`, y los notifica por Gotify.

## HUB-IF-006: Comandos al hub

    ota_cmd:  {"url":"https://.../firmware.bin"}
    node_cfg: {"mac":"...","name":"...","location":"...","room":"...","distance_threshold":N}

## HUB-IF-007: NVS (namespace s360hub)

    wifi_ssid:str, wifi_pass:str, serial:str, hub_id:u32, tenant_id:u32,
    site_id:u32, mqtt_user:str, mqtt_pass:str, alarm_state:u8,
    node_seq_<id>:u32 (anti-replay por nodo)

## Hallazgos v0.1 → v0.2

La v0.1 describía contratos que el firmware nunca implementó. Se conservan aquí
para trazabilidad; ninguno se cumple hoy.

| v0.1 decía | Realidad |
|---|---|
| `event: {"node_id","type","zone","state","seq"}` | El payload real es el de HUB-IF-004 |
| `status: .../hub/{hid}/status` | Es `seguimiento360/hub/status`, sin tenant |
| `command: .../command` | El hub escucha `ota_cmd` y `node_cfg` |
| `response: .../response` | No existe en el firmware |
| `hello: .../hello` | No existe; el estado se publica por LWT |

Estas diferencias son la brecha que cierra la tarea **FW-001** del
[Plan de arranque](../../desarrollo/PLAN-DE-ARRANQUE.md).

## Documentos relacionados

- [Protocolo ESP-NOW](../../hardware/PROTOCOLO_ESPNOW.md)
- [Requisitos](01-requisitos.md) · [Pruebas de aceptación](03-pruebas-aceptacion.md)
- [Flujos Node-RED](../../infraestructura/FLUJOS_NODERED.md)
