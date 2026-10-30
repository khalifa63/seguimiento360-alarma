# Protocolo ESP-NOW — Manual tecnico

El enlace que comparten el hub y todos los nodos. Es el contrato mas delicado
del sistema: si cambia en un lado y no en el otro, los dispositivos dejan de
entenderse y el fallo no siempre es evidente.

Codigo compartido: `firmware/shared/espnow_protocol/`.

## Por que ESP-NOW

- **Latencia muy baja.** No hay asociacion ni handshake por paquete.
- **Sin pila de red.** El nodo no necesita WiFi ni IP, y gasta mucho menos.
- **Funciona sin router.** Los nodos hablan con el hub aunque no haya internet.

El precio: hay que gestionar a mano el direccionamiento, la integridad y la
recuperacion de errores.

## Formato de trama (v1)

    +--------+---------+------------+-------+-----------+-----------+-------------+---------+-------+
    | magic  | version | frame_type | seq   | src_mac   | dst_mac   | payload_len | payload | crc16 |
    | 1 B    | 1 B     | 1 B        | 2 B   | 6 B       | 6 B       | 2 B         | <=229 B | 2 B   |
    +--------+---------+------------+-------+-----------+-----------+-------------+---------+-------+

| Campo | Valor | Notas |
|---|---|---|
| `magic` | `0xA5` | descarta ruido |
| `version` | `0x01` | permite evolucionar el formato |
| `frame_type` | ver abajo | |
| `seq` | 0-65535 | anti-repeticion |
| `src_mac` / `dst_mac` | 6 B en crudo | |
| `payload_len` | longitud real | |
| `crc16` | CRC16-CCITT | polinomio `0x1021`, semilla `0xFFFF` |

Cabecera: **19 bytes**. Payload maximo: **229 bytes** (el limite de ESP-NOW son
250).

### Tipos de trama

| Valor | Nombre | Uso |
|---|---|---|
| `0x00` | `HELLO` | reservado |
| `0x01` | `DATA_EVENT` | eventos de nodo a hub |
| `0x02` | `ACK` | confirmacion del hub |
| `0x03` | `CMD` | configuracion del hub al nodo |
| `0x04` | `BEACON` | reservado |

### Payload de evento (7 bytes)

| offset | Campo | Valores |
|---|---|---|
| 0-3 | `event_code` | `"PAIR"`, `"DOOR"`, `"ALRM"`, `"HEAR"`, `"PONG"` |
| 4 | `seq` | contador |
| 5 | `battery_level` | 0-100 |
| 6 | `value` | semantica segun `event_code` (SPEC-02 v0.3) |

**El byte 6 cambio de nombre dos veces.** Nacio como `reserved` en el nodo y
el hub lo llamaba `distance_cm` (v0.2, telemetria del ultrasónico): nombrarlo
`reserved` invitaba a reutilizarlo y romper la telemetria sin aviso. Con el
nodo REED (v0.3, 2026-10-29) paso a llamarse **`value`**, generico, porque el
sensor de apertura no mide distancias: `DOOR` usa 1=Cerrado / 0=Abierto,
`ALRM` conserva la distancia en cm y `PONG` repite la seq del ping recibido.

### Payload de configuracion

Lo usa el hub para mandar al nodo las credenciales WiFi y la URL del OTA.

    magic (1) | ssid (33) | password (65) | ota_url (96)

El campo `magic` vale `0xC0` y sirve para descartar una trama de otro tipo que
llegue por error.

## Reglas que cuesta aprender

Todas estas costaron un fallo real:

**1. ESP-NOW no admite broadcast.** Enviar a `FF:FF:FF:FF:FF:FF` con un peer
registrado **reinicia el nodo**. Hay que conocer la MAC del destino antes de
poder preguntar nada. De ahi la semilla compilada para el primer `PAIR`.

**2. Hay que registrar el peer antes de enviarle.** Sin `esp_now_add_peer`, el
envio devuelve `ESP_ERR_ESPNOW_NOT_FOUND` (error 1). El hub registra a cada nodo
cuando lo vincula; el nodo registra al hub cuando aprende su MAC.

**3. El peer va por la interfaz que existe.** Los peers se registran con
`ESP_IF_WIFI_STA`. Con `ESP_IF_WIFI_AP`, un hub en modo STA puro falla.

**4. El campo `crc16` de la estructura no sirve para transmitir.** En
`espnow_frame_t` el payload ocupa 229 bytes, asi que ese campo queda al final
de la estructura, muy por detras de donde acaba el payload real. El CRC que se
transmite va **inmediatamente despues de los datos**, y `espnow_frame_size()`
calcula donde acaba segun el payload de cada trama.

Escribirlo en el campo de la estructura hace que nunca se transmita: el
receptor calcula el CRC sobre lo que llego y no coincide, asi que descarta
todas las tramas. Fue uno de los cinco fallos del enlace original.

**5. Las longitudes se leen con `offsetof`.** Se leia `payload_len` en el offset
15 en lugar del 17, y por eso ninguna trama validaba.

**6. El canal es el mismo en los dos lados.** ESP-NOW usa el canal de la
interfaz. Hub y nodos van en el **5**; si el router de casa esta en otro, el hub
no puede atender las dos cosas.

## API compartida

    espnow_build_frame()      construye y calcula el CRC
    espnow_validate_frame()   comprueba magic, version, longitudes y CRC
    espnow_crc16_ccitt()      CRC16-CCITT
    espnow_frame_size()       tamano real a transmitir segun el payload

La misma libreria la usan hub y nodos, y tambien el banco de pruebas de dos
hubs. Copiar el formato en lugar de reutilizarlo lleva a que las dos copias
diverjan.

## Rendimiento medido

| Medida | Valor |
|---|---|
| Recepcion -> ACK | 14 ms (presupuesto 50) |
| Recepcion -> publicacion MQTT | 46-92 ms (presupuesto 250) |
| Perdida de ACK con hub presente | ninguna en las pruebas |

## Cuando algo no funciona

| Sintoma | Mirar primero |
|---|---|
| Ninguna trama valida | `payload_len` con `offsetof`, no a mano |
| Trama valida pero sin CRC | el CRC va tras el payload, no en la estructura |
| `Error al enviar: 1` | el peer no esta registrado |
| El nodo se reinicia al enviar | se esta enviando a broadcast |
| Funciona y de pronto no | canal del router, o el hub cambio de modo |
