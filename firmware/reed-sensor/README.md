# Nodo REED — sensor de apertura para seguimiento360

Firmware ESP-IDF del **nodo REED** (peer del hub): Reed Switch Normalmente
Abierto entre **GPIO4 y GND** con pull-up interna (sin resistencia externa).
Envía el evento **`DOOR` por cada transición** (no por sondeo continuo) por
ESP-NOW al hub, con el frame v1 compartido.

## Comportamiento

| Evento | Cuándo | `value` |
|---|---|---|
| `DOOR` | Cada transición de estado (anti-rebote por muestreo de 100 ms, cambio estable en 2 muestras) | 1 = Cerrado (imán cerca), 0 = Abierto |
| `HEAR` | Cada 600 s (coincide con `NODE_HEARTBEAT_SEC` del hub) | estado actual |
| `PAIR` | Al arrancar y cada 30 s (chequeo de enlace / registro) | 0 |
| `PONG` | Solo al recibir un `PING` del hub (prueba de enlace del portal) | seq del ping |

- **Barrido de canales (2026-10-30):** el PAIR recorre los canales 1..13 con
  `pair_channel_sweep()` y el nodo queda en el canal que responde. El hub
  vive en el canal del router, que puede cambiar solo; si cambia, la
  vigilancia del enlace falla (60 s sin ACK), el nodo se desvincula y el
  barrido re-vincula sin intervención.
- **PONG (2026-10-30):** los frames `ESPNOW_FT_CMD` (ping del hub) se
  responden con `PONG`. Antes se ignoraban y el botón «Probar ESP-NOW» del
  portal fallaba aunque el enlace real funcionara (los HEAR/DOOR pasaban).

- El hub solo registra el `PAIR` con su **ventana de emparejamiento abierta**
  (botón BOOT del hub o ventana desde el portal; 120 s).
- Tras el primer **ACK** el nodo se considera confirmado y envía el estado
  pendiente si hubo cambios durante la vinculación.
- **Bootstrap**: si el nodo está virgen (NVS vacía), siembra la MAC SoftAP del
  hub (`HUB_MAC_SEED` en `main.c`) una sola vez — ESP-NOW no admite broadcast.
  En producto, el hub anunciará su MAC durante la ventana (mejora pendiente).

## Hardware

- ESP32 DevKit V1 (ESP32 clásico, `idf.py set-target esp32`).
- Reed Switch 28 mm N.O.: Terminal 1 → GPIO4, Terminal 2 → GND.
- LED integrado en GPIO2: pulso al transmitir, parpadeo lento sin enlace.

## Compilar y flashear (servidor)

```bash
. ~/esp-idf/export.sh
cd ~/seguimiento360-alarma/firmware/reed-sensor
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor    # salir con Ctrl+]
```

## Verificación

- Nodo: `Enviando: DOOR | value=1 | Seq=N` en cada transición.
- Hub: `ESP-NOW RX (v1): Code=DOOR` + `Evento publicado exitosamente`.
- MQTT: `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/event` con
  `{"event":"DOOR","value":...}`.
- PostgreSQL: fila en `events` (`event_type='DOOR'`, columna `value`) y
  `nodes.last_seen` actualizado.

## Referencias

- Contrato: `docs/firmware/specs/02-interfaces.md` (HUB-IF-001/002/005, v0.3).
- Protocolo y pairing: `docs/firmware/specs/02-interfaces.md` (sección
  HUB-IF-010) y `pairing.c` (FW-008).
- Semilla: `firmware/sensor-ultrasonic/main_backup/` (nodo ultrasónico v1).
