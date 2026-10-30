# SPEC-02 — Contratos de Interface del Hub (v0.3)

Contratos entre nodo, hub, broker y backend. Es la fuente de verdad: el código
deriva de aquí, no al revés. Un cambio incompatible exige *change request*.

> **Estado:** v0.3 documenta lo que el firmware implementa hoy. La v0.1 describía
> un contrato aspiracional que nunca se implementó; el detalle de la brecha está
> en [Hallazgos](#hallazgos-v01--v02).
>
> **Cambios v0.3 (2026-10-29):** el byte 6 del payload v1 pasa de `distance_cm`
> a `value`, genérico, con semántica definida por `event_code` (HUB-IF-002); el
> JSON del evento publica `value` (HUB-IF-004); `DOOR` deja de estar reservado y
> pasa a describir el nodo REED de apertura (HUB-IF-005). **El frame en el aire
> no cambia**: siguen siendo 7 bytes de payload.

## HUB-IF-001: Frame ESP-NOW v1

Ver [Protocolo ESP-NOW](../../hardware/PROTOCOLO_ESPNOW.md) (versión 0x01,
CRC16-CCITT, little endian, anti-replay por `seq` monotónico).

El componente `firmware/hub/components/espnow_link/` lo implementa, pero **el
hub y el nodo aún no lo usan**: ambos hablan el payload crudo de 7 bytes
(HUB-IF-008). La migración es la tarea FW-002.

## HUB-IF-002: Payload ESP-NOW de 7 bytes (vigente)

Contrato **real** entre nodo y hub. La definición canónica vive en
`firmware/shared/espnow_protocol/include/espnow_protocol.h`
(`espnow_payload_v1_t`); el hub mantiene la misma estructura en
`firmware/hub/main/main.c` y el nodo REED en `firmware/reed-sensor/main/main.c`.

    typedef struct {
        uint8_t event_code[4];    // "PAIR", "DOOR", "HEAR", "ALRM", ...
        uint8_t seq;              // contador 0-255, anti-replay
        uint8_t battery_level;    // 0-100 %
        uint8_t value;            // dato del evento; semántica según event_code
    } __attribute__((packed)) security_payload_t;

Ambos lados declaran `_Static_assert(sizeof(...) == 7)`, de modo que una
divergencia de tamaño rompe la compilación. **El frame en el aire no cambió con
v0.3**: siguen siendo 7 bytes; lo que cambió es el nombre del byte 6.

### Semántica de `value` según `event_code`

| event_code | Nodo | `value` |
|---|---|---|
| `DOOR` | REED de apertura (`firmware/reed-sensor`) | 1 = Cerrado (imán cerca), 0 = Abierto |
| `ALRM`, `HEAR` | ultrasónico (legado) | distancia medida en cm |
| `PAIR` | todos | 0 |
| nuevos (`SMOKE`, `PANIC`, ...) | futuros nodos | su métrica propia, documentada aquí al darlos de alta |

### Historia del byte 6 (v0.2)

Hasta v0.2 el byte se llamaba `distance_cm` (antes `reserved` en el nodo), un
nombre que solo encajaba con el nodo ultrasónico y que no escala a los nuevos
tipos de nodo. La v0.3 lo renombra a `value`; el detalle histórico queda en el
historial git.

## HUB-IF-003: Topics MQTT (implementados)

| Uso | Topic | Dirección | QoS |
|---|---|---|---|
| Evento de nodo | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/event` | hub → broker | 1 |
| Estado del hub | `seguimiento360/hub/status` | hub → broker | 1 |
| Comando OTA | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/ota_cmd` | broker → hub | 1 |
| Config de nodo | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/node_cfg` | broker → hub | 1 |
| Estado de emparejamiento | `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/pairing` | hub → broker | 1 |

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
      "value": 195,
      "tenant": "1",
      "site": "1"
    }

`value` es el byte 6 del payload (HUB-IF-002, v0.3): para `DOOR` es el estado
de la puerta (0 = Abierto, 1 = Cerrado); para el ultrasónico, la distancia en
cm (legado). Nota de transición: la API de eventos del backend lo expone
transitoriamente con la clave histórica `distance_cm` mientras la UI termina
su migración; el JSON del hub ya usa `value`.

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
| `DOOR` | apertura/cierre del nodo REED: `value` 1 = Cerrado, 0 = Abierto |
| `ALRM` | alarma por umbral de distancia (nodo ultrasónico) |
| `HEAR` | latido periódico |
| `GAS`, `SMOKE`, `FIRE`, `PANIC`, `MOV`, `SEIS`, `SOS`, `HUM` | reservados para otros tipos de nodo |

Node-RED clasifica la gravedad para la notificación por Gotify: **crítica** para
`ALRM`, `GAS`, `SMOKE`, `FIRE`, `PANIC`, `SOS` y `SEIS`; **media** para `DOOR`,
`MOV` y `HUM` (v0.3 documenta lo que el flujo en vivo implementa).

## HUB-IF-006: Comandos al hub

    ota_cmd:  {"url":"https://.../firmware.bin"}
    node_cfg: {"mac":"...","name":"...","location":"...","room":"...","distance_threshold":N}
    command:  {"action":"arm"} | {"action":"disarm"}
              {"action":"pairing"}                      abre la ventana 120 s
              {"action":"unpair","mac":"aa:bb:cc:dd:ee:ff"}  olvida un nodo

**Prueba de enlace (PING/PONG):** el botón «Probar ESP-NOW» del portal publica
en el topic `command`:

    {"command":"ping","mac":"aa:bb:cc:dd:ee:ff","tid":"<id-prueba>"}

El hub envía el ping por ESP-NOW al nodo: frame `ESPNOW_FT_CMD` cuyo payload
es un `espnow_payload_ping_t` (magia `0xE7`, 7 bytes: magic, nonce y tid). El
nodo responde con un `PONG` (frame `ESPNOW_FT_DATA_EVENT` normal,
`event_code="PONG"`, seq = la del ping). El hub mide el RTT y publica el
resultado en el topic `.../hub/<mac>/diag`:

    {"event":"PING_RESULT","mac":"...","ok":true,"rtt_ms":69,
     "attempts":1,"seq":4,"reason":"ok","tid":"<id-prueba>"}

- El hub reintenta el ping hasta 3 veces (~2 s cada una) antes de publicar
  `ok:false, reason:"sin_respuesta"`.
- El PONG se resuelve en el hub y **no** sube como evento: no es una lectura
  del sensor y no contamina el historial.
- El nodo debe responder **solo si el frame es `ESPNOW_FT_CMD`** (ver
  `on_data_recv` del reed). El nodo ultrasónico v1 no implementa esta
  respuesta: el portal le reportará `sin_respuesta`.

El topic `pairing` lo publica el hub para que el estado de la ventana se pueda
consultar sin estar al lado del equipo (ver HUB-IF-014):

    {"event":"PAIRING_open","seconds":120,"mac":"3cdc7562c2b0"}
    {"event":"PAIRING_closed","seconds":0,"mac":"3cdc7562c2b0"}

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

## HUB-IF-013: Identificadores de hub y nodo (CR-001)

El identificador de un hub **es su MAC**. Un nodo se identifica por la suya. No
hay numeros de serie inventados.

### Dos formas, un solo dato

La MAC aparece de dos maneras, y cual se usa depende del contexto:

| Contexto | Forma | Ejemplo |
|---|---|---|
| Topics MQTT | compacta, sin separadores | `3cdc7562c2b0` |
| Datos, payloads y consola | legible, con dos puntos | `3c:dc:75:62:c2:b0` |

La regla: **la forma compacta es solo para construir topics.** En todo lo demas
—lo que se guarda, lo que viaja entre dispositivos, lo que se lee— se usa la
forma legible.

El motivo de la compacta en topics es que los dos puntos son validos en MQTT
pero incomodos: parten el topic en niveles aparentes y complican las
suscripciones con comodines.

### Que usa cada sitio

| Sitio | Forma | Comprobado |
|---|---|---|
| `hubs.serial_number` | compacta | `3cdc7562c2b0`, 12 caracteres |
| `nodes.mac_address` | legible | `a0:f2:62:f1:d0:f0`, 17 caracteres |
| `events.hub_mac` | compacta | hereda el serial del hub |
| `events.node_mac` | legible | `a0:f2:62:f1:d0:f0` |
| Topic MQTT del hub | compacta | `.../hub/3cdc7562c2b0/...` |
| `HUB_ONLINE.mac` | legible | `3c:dc:75:62:c2:b0` |
| Frame ESP-NOW | 6 bytes en crudo | `src_mac` / `dst_mac` |

**Nota:** el hub guarda su serial en forma compacta porque es la misma columna
que usa para resolver los eventos. El nodo guarda la suya en forma legible
porque es la que intercambia con el hub por ESP-NOW y la que aparece en los
comandos `node_cfg`.

### Validacion

Cada forma se valida con su patron, y no se acepta otra:

    hubs.serial_number   ^[0-9a-f]{12}$                     (12 hex)
    nodes.mac_address    ^([0-9a-f]{2}:){5}[0-9a-f]{2}$     (17 con dos puntos)

El backend ya lo hace asi al registrar un nodo:

    if (!/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/.test(macAddress)) {
      msg.payload = { error: 'mac_address debe tener formato xx:xx:xx:xx:xx:xx' };
    }

Se normaliza a minusculas antes de comparar, de modo que una MAC escrita en
mayusculas se acepta y se guarda en la forma canonica.

### Normalizacion

Quien reciba una MAC de fuera (formulario, API) normaliza antes de validar:
pasa a minusculas y comprueba contra el patron de la forma que corresponda al
dato, segun la tabla anterior. Normalizar y validar en un solo sitio evita que
cada consumidor tenga que hacerlo.

### Por que la MAC y no otra cosa

- Ya es unica y viene grabada de fabrica.
- El firmware ya la reporta.
- Las tablas `hubs` y `nodes` ya la usan.
- Un serial grabado en NVS exigiria un paso de produccion que no existe; el
  claim code ataria la identidad del equipo a un codigo de un solo uso.

### Antes de cambiar flujos, leer

La copia del repo **puede no coincidir con lo desplegado**. En octubre de 2026
habia tres versiones distintas y analizar la equivocada llevo a conclusiones
falsas. Ver `backend/nodered-flows/README.md`.

## HUB-IF-014: Ventana de vinculacion (varios hubs)

El hub solo adopta nodos **desconocidos** mientras la ventana esta abierta.
Cerrada —el estado normal— ignora cualquier `PAIR` que no sea de un nodo ya
vinculado.

### Por que

Un nodo de fabrica usa una **semilla compilada** para su primer `PAIR` y acepta
el `ACK` de quien responda. En una casa con dos hubs, el nodo de la primera
planta podia acabar vinculado al de la planta baja: le bastaba con estar en
alcance. Y como todos los nodos comparten la semilla, cualquier hub respondia a
cualquier nodo nuevo.

### Como se abre

| Via | Cuando | Como |
|---|---|---|
| MQTT | el hub ya tiene red | `{"action":"pairing"}` al topic `command` |
| Boton fisico | el hub aun no tiene red | pulsar el BOOT (GPIO 0) |

Duracion: **120 s** (`PAIR_WINDOW_SEC`). Reabrir reinicia el plazo. El boton
abre en el **flanco** (alto a bajo), no mientras esta pulsado: si no, mantenerlo
pulsado reiniciaria la ventana sin parar.

### Como se sabe que esta abierta

El **LED parpadea lento** (700 ms, `LED_EMPAREJA_MS`), distinto del parpadeo
rapido de error (120 ms). Y el hub publica el estado en el topic `pairing`.

### Limite

No cierra el caso de un vecino vinculando a la vez. Reduce el problema de
"siempre" a "solo durante la ventana". Cerrarlo del todo exige un **token
compartido** en el `PAIR`, que necesita un paso de fabricacion que hoy no existe.

### Comando unpair

Olvida un nodo del registro del hub, para sustituir un sensor averiado sin ir
fisicamente:

    {"action":"unpair","mac":"a0:f2:62:f1:d0:f0"}

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

    HUB_MAC_SEED_0..5   (main.c del nodo; `firmware/reed-sensor` lo implementa)

**No son la MAC definitiva.** Son solo el destino de la primera pregunta. Una
vez que el hub contesta, su MAC real se guarda en NVS y la semilla deja de
usarse: si el hub cambia, el nodo lo aprende solo.

### Flujo de vinculación

    1. El nodo arranca y lee NVS (namespace s360node, clave hub_mac).

    2a. Si hay MAC  -> se registra el peer y se envían eventos con normalidad.
    2b. Si no hay   -> modo SIN VINCULAR:
                       - el LED parpadea
                       - no se envían eventos (no hay destino)
                       - PAIR cada 30 s **con barrido de canales**

#### Barrido de canales (HUB-IF-011, ampliación 2026-10-30)

El hub es estación de su router: su radio queda en el canal del router, que el
usuario no controla y que puede cambiar solo. Fijar el nodo en un canal único
rompe el enlace sin aviso (le pasó al prototipo REED el 2026-10-30). El
`firmware/reed-sensor` implementa el barrido:

- El PAIR se envía por los canales 1..13 con una espera corta en cada uno
  (`pair_channel_sweep()`).
- El canal correcto es el que responde el ACK: el nodo se queda en él.
- Si el router cambia de canal más adelante, la vigilancia del enlace falla
  (60 s sin ACK), el nodo se desvincula y el barrido vuelve a encontrar el
  canal nuevo. El usuario no tiene que hacer nada.

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

#### Respuesta al ping del hub (PONG, 2026-10-30)

El hub también pregunta al nodo en la dirección contraria (prueba de enlace
del portal, ver HUB-IF-006): envía un frame `ESPNOW_FT_CMD` con el payload de
ping. El nodo reed lo atiende en `on_data_recv()` y contesta con un
`PONG` (`send_frame("PONG", frame.seq)`). El hub usa el RTT para el
`PING_RESULT` que consume el portal.

Sin esta respuesta el hub declaraba `sin_respuesta` tras 3 intentos y el
portal mostraba error, **aunque el enlace real funcionara** (los HEAR/DOOR
seguían pasando): el ping es en la dirección hub→nodo, que no existía.

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
