# Nodo BOTÓN DE PÁNICO — pulsador para seguimiento360

Firmware ESP-IDF del **nodo botón de pánico** (peer del hub): pulsador **NA
entre GPIO4 y GND** con pull-up interna, según quedó validado en el prototipo
`pruebas/pulzador/` (ESP32-S3, GPIO4, debounce 50 ms). Cada **oprimida** envía
un evento `PANIC` (`DATA_EVENT`, `value=1`) por ESP-NOW al hub; el toggle
armado/desarmado lo lleva **Node-RED por contexto**, no el nodo (HUB-IF-015,
SPEC-02 v0.6):

    1.ª oprimida -> Node-RED despacha buzzer seconds=0xFFFF (bucle continuo)
    2.ª oprimida -> Node-RED despacha buzzer seconds=0     (silencio)

## Comportamiento

| Evento | Cuándo | `value` |
|---|---|---|
| `PANIC` | En cada oprimida del pulsador (flanco 1→0 estable) | 1 (siempre) |
| `HEAR` | Cada 600 s (coincide con `NODE_HEARTBEAT_SEC` del hub) | 0 (el botón no mide nada) |
| `PAIR` | Al arrancar y en cada chequeo de enlace (10 s vinculado, barrido 8 s sin vincular) | 0 |
| `PONG` | Solo al recibir un `PING` del hub (prueba de enlace del portal) | seq del ping |

- **Ruta de toggle propia:** el `PANIC` **no pasa** por el cooldown de 60 s
  del despacho de sirena de los `DOOR` (HUB-IF-005/015). El nodo no alterna
  nada: si alternara, una oprimida perdida desincronizaría el estado para
  siempre. Node-RED alterna por MAC del botón y además descarta `PANIC`
  repetidos con la misma `seq`.
- **Código en el aire:** el `event_code` del frame es de 4 bytes — el nodo
  envía `PANI` y el hub lo publica como `PANIC` en el JSON (SPEC-02 v0.6,
  HUB-IF-002/005).
- **Anti-rebote:** 50 ms de nivel estable (patrón del prototipo
  `pruebas/pulzador`), sondeo cada 10 ms por tarea, sin interrupciones.
- **Rate-limit propio:** máximo un `PANIC` cada 3 s; las oprimidas dentro
  de esa ventana se registran en el log pero no se envían.
- **Un envío por oprimida** (mismo patrón que el REED): el hub no ACKea
  eventos de datos —solo el `PAIR` del chequeo de enlace—, así que un
  reenvío sin ACK solo duplicaría. Los duplicados de entrega (MQTT QoS1)
  los cubre el dedup por `seq` de Node-RED (toggle, inserción y push).
- **Sin enlace no se encola:** una oprimida sin hub se registra en el log
  y no se envía. El pánico es una orden en tiempo real; encolarla sonaría
  cuando el enlace volviera, minutos después de la emergencia.
- **Barrido de canales:** igual que el REED — el `PAIR` recorre los canales
  1..13 con `pair_channel_sweep()` y el nodo queda en el canal que responde.
  Si el router cambia de canal, la vigilancia del enlace falla (60 s sin
  ACK), el nodo se desvincula y el barrido re-vincula sin intervención.
- **PONG:** los frames `ESPNOW_FT_CMD` del ping del hub (magia `0xE7`) se
  responden con `PONG`; el botón «Probar ESP-NOW» del portal funciona gratis
  (misma lección del REED y del buzzer).

## Hardware

- ESP32 DevKit **ESP32-S3** (`idf.py set-target esp32s3`), mismo objetivo
  del prototipo `pruebas/pulzador`.
- Pulsador NA: un terminal a **GPIO4**, el otro a **GND** (pull-up interna
  del ESP32; sin resistencia externa).
- **LED de estado: GPIO2** (LED integrado del DevKit, activo alto, como el
  REED). Parpadeo rápido mientras busca hub; destello breve en cada envío.
- Alimentación: **cargador de teléfono** (no el USB del PC). Un puerto USB
  débil provoca brownout en `phy_init` (misma lección del REED y del buzzer;
  el firmware limita la potencia TX a 10 dBm como mitigación adicional).
- **Alcance:** el hub debe estar dentro del alcance ESP-NOW de este nodo;
  ESP-NOW no atraviesa bien varias losas de concreto. Validar el enlace con
  «Probar ESP-NOW» del portal antes de montar el nodo en otro piso.

## Compilar y flashear (servidor)

```bash
. ~/esp-idf/export.sh
cd ~/seguimiento360-alarma/firmware/boton-panico
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # salir con Ctrl+]
```

Alimentarlo después por cargador de teléfono: el USB del PC es para
desarrollo, no para operación.

## Verificación

- Nodo: `PANIC enviado (seq=N, intento 1/3)` + `PANIC (seq=N) confirmado por
  el hub tras 1 intento(s)` al recibir el ACK.
- Hub: `ESP-NOW RX (v1): Code=PANIC` + `PANIC de <mac> (seq=N): boton de
  panico oprimido` + `Evento publicado exitosamente`.
- MQTT: `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/event` con
  `{"event":"PANIC","value":1}`.
- Node-RED: alterna el estado por MAC del botón (`panico_arm` en contexto
  global) y despacha a los buzzers activos del sitio `seconds:65535` (armar)
  o `seconds:0` (desarmar) por el topic `command`.
- Buzzer: `BUZZER bucle latched recibido` + `BUZZ value=1` al armar;
  `BUZZER paro recibido` + `BUZZ value=0` al desarmar.
- PostgreSQL: filas en `events` (`event_type='PANIC'` y `event_type='BUZZ'`)
  y `nodes.last_seen` del botón actualizado.

## Referencias

- Contrato: `docs/firmware/specs/02-interfaces.md` (HUB-IF-001/002/005/011/015,
  v0.6).
- Protocolo y pairing: `docs/firmware/specs/02-interfaces.md` (HUB-IF-010 /
  HUB-IF-011) y `docs/manuales/tecnico/protocolo-comun.md`.
- Nodo de referencia: `firmware/reed-sensor/` (pairing, PONG, HEAR,
  vigilancia de enlace, LED — base de este nodo).
- Prototipo del pulsador: `pruebas/pulzador/` (GPIO4, pull-up, debounce
  50 ms, ESP32-S3).
