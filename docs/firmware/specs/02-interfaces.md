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

## HUB-IF-012: Provisioning condicional (HUB-F-011)

El hub solo levanta su SoftAP de provisioning si le falta configuracion.

### Por que

Antes arrancaba siempre en `WIFI_MODE_APSTA`, de modo que un hub **ya
configurado y funcionando** mantenia su AP abierto. Cualquiera dentro del
alcance podia conectarse y reconfigurarlo. El portal se cierra a los 10
minutos sin actividad, pero eso no impide la reconfiguracion durante esa
ventana.

### Comportamiento

| Estado al arrancar | Modo de radio | Portal |
|---|---|---|
| Con credenciales en NVS | `WIFI_MODE_STA` | No se inicia |
| Sin credenciales | `WIFI_MODE_APSTA` | `http://192.168.4.1` |

Para reconfigurar un hub ya provisionado hay que borrar su configuracion
(boton de fabrica o `/factory_reset` estando cerca). Es intencionado: evita
que un equipo en produccion quede expuesto de forma permanente.

### Los dos sistemas de configuracion

Conviene tenerlo presente porque es una fuente de confusion:

- El **hub** usa `active_config` (NVS namespace `pairing`, clave
  `hub_config`) y el indicador `wifi_configured`. Es lo que decide el modo de
  radio.
- El componente **`sm_prov`** tiene su propia FSM con
  `nvs_manager_has_credentials()`, sobre las particiones `nvs_state` y
  `nvs_nodes`.

Hoy el arranque lo gobierna `active_config`. `sm_prov` no se invoca desde
`main.c`: su FSM esta implementada y sin usar. Unificarlos es trabajo
pendiente, anotado en FW-013.

### Claim code

El requisito HUB-F-011 pide vincular el hub con la cuenta mediante un claim
code. **No existe**: el portal actual recoge SSID, contrasena, tenant y site,
y el tenant se escribe a mano. Es lo que falta para desplegar hubs en serie
sin configurarlos uno a uno.

## HUB-IF-011: Vinculación del nodo (FW-008)

Cómo aprende un nodo a qué hub dirigirse, sin llevar la MAC compilada.

### Restricción de partida

ESP-NOW **no admite enviar a broadcast**: enviar a `FF:FF:FF:FF:FF:FF` con un
peer registrado provoca un fallo y reinicia el nodo (ver fallo 3 del enlace).
Por eso el nodo necesita saber a quién preguntar antes de poder descubrir nada.

### La MAC semilla

Un nodo de fábrica no tiene nada en NVS. Para el primer `PAIR` usa unas
constantes compiladas:

    HUB_MAC_SEED_0..5   (main.c del nodo)

**No son la MAC definitiva.** Son solo el destino de la primera pregunta. Una
vez que el hub contesta, su MAC real se guarda en NVS y la semilla deja de
usarse: si el hub cambia, el nodo lo aprende solo.

### Flujo de vinculación

    1. El nodo arranca y lee NVS (namespace s360node, clave hub_mac).

    2a. Si hay MAC  -> se registra el peer y se envían eventos con normalidad.
    2b. Si no hay   -> modo SIN VINCULAR:
                       - el LED parpadea
                       - no se envían eventos (no hay destino)
                       - se envía PAIR cada 30 s

    3. El hub recibe el PAIR. Si el nodo ya estaba registrado responde ACK;
       si no, lo registra en su node_registry y responde ACK.

    4. El ACK es un frame v1 de tipo ESPNOW_FT_ACK cuyo campo src_mac lleva
       la MAC del hub. El nodo la extrae, la guarda en NVS y queda vinculado.

### Aprendizaje del ACK

| Campo del ACK | Uso en el nodo |
|---|---|
| `frame_type` | Debe ser `ESPNOW_FT_ACK` |
| `src_mac` | **MAC del hub: se guarda en NVS** |
| `seq` | Se registra para trazabilidad |

El nodo solo escribe en NVS **si la MAC cambió**. El ACK llega en cada evento,
y escribir en cada uno desgastaría la flash.

Si la MAC del ACK difiere de la guardada, el nodo borra el peer anterior
(`esp_now_del_peer`) y registra el nuevo: es la ruta de sustitución de hub.

### Vigilancia del enlace

Aprender la MAC nueva exige que el ACK llegue. Si el hub se sustituye por otro,
el nodo sigue enviando a la MAC vieja y **nunca recibe ese ACK**: la ruta de
sustitución queda inalcanzable y el nodo envía eventos al vacío sin síntoma.

El nodo comprueba el enlace por su cuenta cada `LINK_CHECK_SEC` (10 s) enviando
un `PAIR`. Un ACK recibido pone el contador a cero. Al acumular `ACK_LOST_MAX`
comprobaciones sin respuesta (6, es decir **60 s**), el nodo borra la MAC de NVS
con `pairing_forget_hub_mac()` y vuelve al modo sin vincular, donde el LED
parpadea y sigue insistiendo. Cuando el hub reaparece se re-vincula solo.

Esto **no** usa el temporizador del heartbeat: `HEARTBEAT_INTERVAL_SEC` es de
10 minutos y la desvinculación habría tardado horas.

### Filtro de destinatario en el hub

ESP-NOW entrega al hub cualquier trama que su radio oiga, incluidas las de
nodos de hubs cercanos. `on_data_recv()` procesaba todas sin mirar el
destinatario, de modo que un nodo ajeno podía emparejarse con este hub.

El hub atiende una trama solo si `frame.dst_mac` es una MAC propia —STA o
SoftAP— según `mac_es_de_este_hub()`. Se aceptan ambas porque el nodo dirige el
`PAIR` al SoftAP y recibe los ACK desde la STA.

**Pendiente:** el ACK no lleva firma, así que un hub ajeno podría responder a
un `PAIR` y el nodo adoptaría su MAC. Cerrarlo requiere un token compartido en
el `PAIR`, que es un cambio de contrato y necesita change request.

### NVS del nodo

| Partición | Namespace | Clave | Contenido |
|---|---|---|---|
| `nvs` (por defecto) | `s360node` | `hub_mac` | 6 bytes, MAC del hub |

Una MAC de ceros se trata como no vinculada. Si NVS está corrupta o vacía, el
nodo vuelve al modo sin vincular sin intervención.

### Comportamiento observable

| Situación | LED | Consola | Eventos |
|---|---|---|---|
| Sin vincular | Parpadea | "Sin hub en NVS... semilla..." | No se envían |
| Vinculado | Apagado | "Hub vinculado en NVS: ..." | Sí |
| Hub cambiado | Apagado | "El hub ha cambiado..." | Sí, al nuevo |
| Hub ausente 60 s | Parpadea | "El hub no responde (6 comprobaciones sin ACK). Desvinculando..." | No se envían |
| Hub repuesto | Apagado | "Nodo VINCULADO con el hub ..." | Sí, se reanudan |

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
