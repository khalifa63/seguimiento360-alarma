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

## HUB-IF-009: Portal cautivo

El hub levanta un SoftAP para el aprovisionamiento. SSID y contraseña derivan
de la MAC, de modo que varios hubs en la misma casa son distinguibles.

| Elemento | Formato | Ejemplo |
|---|---|---|
| SSID | `S360-XXXX` | `S360-C2B0` |
| Contraseña | `S360-XXXX` | `S360-C2B0` |
| IP del portal | `192.168.4.1` | — |

`XXXX` son los cuatro últimos hex de la MAC en mayúsculas.

El portal guarda en NVS la red WiFi de destino, el broker MQTT, el `tenant_id`
y el `site_id`. El AP se cierra tras 10 minutos de inactividad.

## Hallazgos v0.1 → v0.2

La v0.1 describía contratos que el firmware nunca implementó. Se conservan aquí
para trazabilidad; ninguno se cumple hoy.

| v0.1 decía | Realidad |
|---|---|
| `event: {"node_id","type","zone","state","seq"}` | El payload real es el de HUB-IF-004 |
| `status: .../hub/{hid}/status` | Es `seguimiento360/hub/status`, sin tenant |
| `command: .../command` | El hub escucha `ota_cmd` y `node_cfg` |
| `response: .../response` | No existe en el firmware |
| `hello: .../hello` | No existe; el estado se publica en `seguimiento360/hub/status`, con LWT para el offline desde el 2026-10-23 |

Estas diferencias son la brecha que cierra la tarea **FW-001** del
[Plan de arranque](../../desarrollo/PLAN-DE-ARRANQUE.md).

## HUB-IF-010: Implementación del frame v1 (FW-001 a FW-003)

El contrato del frame vive en `firmware/shared/espnow_protocol/`, compartido
por el hub y los nodos. Antes solo existía dentro del componente del hub y el
nodo no podía incluirlo: de ahí que el mismo byte se llamara `distance_cm` en
un lado y `reserved` en el otro.

### API compartida

| Función | Uso |
|---|---|
| `espnow_build_frame()` | Construye cabecera, copia el payload y calcula el CRC |
| `espnow_validate_frame()` | Valida magic, versión, longitud y CRC |
| `espnow_crc16_ccitt()` | CRC16-CCITT (0x1021, semilla 0xFFFF) |
| `espnow_frame_size()` | Tamaño real del frame según su payload |

`espnow_payload_v1_t` es el payload de 7 bytes de HUB-IF-002.

### Disposición del frame

| Offset | Campo | Bytes |
|---:|---|---:|
| 0 | magic (`0xA5`) | 1 |
| 1 | version (`0x01`) | 1 |
| 2 | frame_type | 1 |
| 3 | seq | 2 |
| 5 | src_mac | 6 |
| 11 | dst_mac | 6 |
| 17 | payload_len | 2 |
| 19 | payload | N |
| 19+N | crc16 | 2 |

**El CRC va inmediatamente después del payload real**, no en el offset del
campo `crc16` del struct. Ese campo vive en el byte 234 porque la estructura
reserva `payload[215]`; si se escribe ahí, el CRC queda fuera del frame que se
transmite y el receptor recibe basura. Ver el fallo 5 más abajo.

### Cambios en cada lado

**Nodo** (`sensor-ultrasonic/main/main.c`): construye frames v1, valida el ACK
con `espnow_validate_frame()` y usa su MAC real (`esp_read_mac`) como
`src_mac`.

**Hub** (`hub/main/main.c`): valida el frame antes de procesarlo y envía el ACK
como frame v1 de tipo `ESPNOW_FT_ACK`. La lógica de negocio (pairing, eventos,
publicación MQTT) no cambió.

### Rendimiento medido

    ESP-NOW RX (v1)
    Evento publicado exitosamente     <- 25 ms después

El presupuesto de A7 para RX→publicación es ≤ 250 ms: se cumple con margen.

## Fallos del enlace ESP-NOW (2026-10-23)

El enlace no funcionaba y no era un solo problema: eran cinco, y cada uno
ocultaba al siguiente.

### Fallo 1 — El nodo enviaba el payload crudo

El nodo mandaba 7 bytes sin cabecera ni CRC. El hub comparaba el tamaño
recibido contra `sizeof(security_payload_t)` (7) y descartaba los 28 bytes
del frame v1 con `Paquete descartado: tamaño 28 != 7`.

### Fallo 2 — `router_channel` nunca se asignaba

    static uint8_t router_channel = 0;   /* única mención en todo el archivo */

Los peers se registraban con `channel = 0` mientras los nodos transmiten en el
5. Se corrigió con `#define S360_ESPNOW_CHANNEL 5`, que ahora es la única
fuente del canal.

### Fallo 3 — ESP-NOW no admite enviar a broadcast

El nodo enviaba a `FF:FF:FF:FF:FF:FF`. Eso **reiniciaba el nodo** en cuanto
había un evento que enviar: la consola se cortaba a media línea y el timestamp
volvía a cero.

ESP-NOW exige la MAC real del destino. El nodo usa ahora la MAC del SoftAP del
hub, que es la interfaz por la que el hub registra los peers
(`ifidx = ESP_IF_WIFI_AP`).

**Consecuencia:** el pairing (FW-008) deja de ser opcional. Hoy la MAC está
compilada en el nodo y debe venir de la NVS.

### Fallo 4 — `payload_len` leído en el offset equivocado

    memcpy(&payload_len, data + 15, ...);   /* el offset real es 17 */

El offset 15 cae dentro de `dst_mac`, así que se leía parte de la MAC como
longitud y salía `ESP_ERR_INVALID_SIZE`. Se corrigió usando
`offsetof(espnow_frame_t, payload_len)`, que no se rompe si cambia el struct.

### Fallo 5 — El CRC se escribía fuera del frame

El más sutil. `espnow_build_frame()` guardaba el CRC en `out->crc16`, que el
struct sitúa en el byte **234** por el `payload[215]`. Como solo se transmiten
los 28 primeros bytes, **el CRC nunca salía del chip** y el hub leía basura en
la posición 26: `ESP_ERR_INVALID_CRC`.

Se corrigió escribiéndolo justo después del payload real:

    const size_t crc_len = ESPNOW_HEADER_SIZE + payload_len;
    memcpy((uint8_t *)out + crc_len, &crc, sizeof(crc));

### Cómo diagnosticarlos

La consola del hub sale por UART0 **y** por USB-Serial-JTAG
(`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`). Con el adaptador CH343 la
consola no llegaba —las líneas UART no estaban cableadas— y el diagnóstico fue
a ciegas durante un buen rato. **Conectando el hub por su USB nativo los logs
aparecen de inmediato.**

## Documentos relacionados


- [Protocolo ESP-NOW](../../hardware/PROTOCOLO_ESPNOW.md)
- [Requisitos](01-requisitos.md) · [Pruebas de aceptación](03-pruebas-aceptacion.md)
- [Flujos Node-RED](../../infraestructura/FLUJOS_NODERED.md)
