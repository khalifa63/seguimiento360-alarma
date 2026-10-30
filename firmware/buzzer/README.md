# Nodo BUZZER — sirena de alarma para seguimiento360

Firmware ESP-IDF del **nodo buzzer** (peer del hub): buzzer **pasivo** en
**GPIO3** con PWM LEDC (2700 Hz, duty 90 %, `GPIO_DRIVE_CAP_3`), según quedó
validado en el prototipo `pruebas/buzzer/`. Suena la duración que el hub le
manda por ESP-NOW (HUB-IF-015, SPEC-02 v0.4) y confirma con el evento `BUZZ`.

## Comportamiento

| Evento | Cuándo | `value` |
|---|---|---|
| `BUZZ` | Al arrancar la sirena tras un comando válido del hub (1..300 s) | 1 = sonando |
| `HEAR` | Cada 600 s (coincide con `NODE_HEARTBEAT_SEC` del hub) | 1 si está sonando, 0 si no |
| `PAIR` | Al arrancar y en cada chequeo de enlace (10 s vinculado, barrido 8 s sin vincular) | 0 |
| `PONG` | Solo al recibir un `PING` del hub (prueba de enlace del portal) | seq del ping |

- **Activación (HUB-IF-015):** el hub manda un frame `ESPNOW_FT_CMD` con
  `espnow_payload_buzzer_t` (magia `0xE8`, 20 bytes: magic, seconds u16,
  src_event[4], tid[13]). El nodo valida `seconds` en **1..300** y descarta
  fuera de rango (no deja la sirena pegada ni la apaga de golpe). Suena
  **continuos** los `seconds` pedidos: primero arranca el sonido, luego
  responde `BUZZ`.
- **Extensión:** si llega otro comando mientras suena, la duración se
  **extiende** (`deadline = max(deadline, ahora + seconds)`); un trigger
  repetido no corta una alarma más larga en curso.
- **Barrido de canales:** igual que el REED — el PAIR recorre los canales
  1..13 con `pair_channel_sweep()` y el nodo queda en el canal que responde.
  El hub vive en el canal del router, que puede cambiar solo; si cambia, la
  vigilancia del enlace falla (60 s sin ACK), el nodo se desvincula y el
  barrido re-vincula sin intervención.
- **PONG:** los frames `ESPNOW_FT_CMD` del ping del hub (magia `0xE7`) se
  responden con `PONG`; el botón «Probar ESP-NOW» del portal funciona gratis.
- **CMD con magia desconocida:** se ignora sin quebrar (versiones futuras
  del contrato).

## Hardware

- ESP32-C3 SuperMini (`idf.py set-target esp32c3`).
- Buzzer pasivo: Terminal + → **GPIO3**, Terminal − → GND. Conductor directo
  soportado por el C3 con `GPIO_DRIVE_CAP_3`; si el volumen es bajo,
  transistor NPN en base con resistencia de 1 kΩ y buzzer a 5 V.
- **LED de estado: GPIO8** (LED azul integrado del SuperMini, **activo bajo**
  — enciende con nivel 0). Cambio documentado respecto al REED (DevKit V1:
  GPIO2, activo alto): `set_led()` invierte la polaridad (`LED_ACTIVE_LOW 1`
  en `main.c`).
- Alimentación: **cargador de teléfono**. Un puerto USB débil provoca
  brownout en `phy_init` (misma lección del REED; el firmware limita la
  potencia TX a 10 dBm como mitigación adicional).

## Compilar y flashear (servidor)

```bash
. ~/esp-idf/export.sh
cd ~/seguimiento360-alarma/firmware/buzzer
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # salir con Ctrl+]
```

## Verificación

- Nodo: `BUZZER recibido (seq=N): sirena 30 s, origen DOOR` + `Enviando: BUZZ | value=1 | Seq=M`.
- Hub: `ESP-NOW RX (v1): Code=BUZZ` + `Evento publicado exitosamente`.
- MQTT: `seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/event` con
  `{"event":"BUZZ","value":1}`.
- Node-RED: clasifica `BUZZ` como **media**; el despacho de alarmas busca
  nodos `actuador_buzzer` con `buzzer_seconds > 0` y publica el comando.
- PostgreSQL: fila en `events` (`event_type='BUZZ'`, `value=1`) y
  `nodes.last_seen` actualizado.

## Referencias

- Contrato: `docs/firmware/specs/02-interfaces.md` (HUB-IF-001/002/005/015,
  v0.4).
- Protocolo y pairing: `docs/firmware/specs/02-interfaces.md` (sección
  HUB-IF-010 / HUB-IF-011) y `docs/manuales/tecnico/protocolo-comun.md` v0.4.
- Nodo de referencia: `firmware/reed-sensor/` (pairing, PONG, HEAR,
  vigilancia de enlace, LED).
- Prototipo de audio: `pruebas/buzzer/` (LEDC 2700 Hz, duty 90 %, CAP_3).
