# SPEC-03 — Pruebas de Aceptacion del Hub (v0.1)

Formato: pasos | criterio PASS | traza | resultado (fecha, fw, PASS/FAIL)

## AT-001 (HUB-F-001/002/003, NF-001)
Pasos: flashear dev build; monitor ON; medir t desde power-on.
PASS: "WiFi OK" y "MQTT conectado" y "hello publicado" con t <= 10 s.
Resultado: _pendiente_

## AT-002 (HUB-F-004/005)
Pasos: PWA -> Control de Alarma -> ARMAR; observar monitor y LED.
PASS: monitor "COMANDO: arm"; LED GPIO48 ON; response "ok".
Resultado: _pendiente_

## AT-003 (HUB-F-006/007, NF-002)
Pasos: nodo dev publica frame event (boton); mosquitto_sub en /event.
PASS: hub loggea RX + ACK; /event recibe JSON con seq correcto.
Resultado: PASS (2026-10-23, hub 37bff1e, nodo v2.0).

  Nodo ultrasónico real -> hub -> MQTT -> Node-RED -> PostgreSQL.
  El hub registra "ESP-NOW RX (v1): Code=ALRM, MAC=a0:f2:62:f1:d0:f0" y
  "Evento publicado exitosamente". El JSON llega con seq, battery y
  distance_cm correctos.

  Latencia medida RX -> publicación MQTT: 25 ms (presupuesto: <= 250 ms).

  Evidencia: eventos 55 y anteriores en la tabla events, con
  node_name y distance_cm reales del sensor.

## AT-004 (HUB-F-008)
Pasos: nodo publica heartbeat; esperar 3 periodos sin HB.
PASS: evento node_lost tras 3 HB perdidos.
Resultado: _pendiente_

## AT-005 (HUB-F-009)
Pasos: cortar alimentacion; mosquitto_sub en status.
PASS: {"status":"offline"} en <= keepalive + 5 s.
Resultado: PARCIAL (2026-10-23).

  El hub publica su estado al conectar en seguimiento360/hub/status:

      {"event":"HUB_ONLINE","mac":"3c:dc:75:62:c2:b0","hub_name":""}

  Capturado con mosquitto_sub tras reiniciar el hub. Queda por verificar
  el mensaje offline del LWT al cortar la alimentación, y que hub_name no
  llegue vacío.

  Nota: el ACL concede "topic write" (no readwrite) sobre ese tópico, que
  es lo correcto para un LWT.

## AT-006 (HUB-F-010)
Pasos: armar via PWA; reiniciar hub.
PASS: tras reboot, LED refleja armed sin mediar command.
Resultado: _pendiente_

## AT-007 (HUB-F-011) — fase 5.2
PASS: hub sin config -> SoftAP + portal -> active.
Resultado: _pendiente_

## AT-008 (HUB-F-012) — fase 5.3
PASS: OTA firma valida actualiza; firma invalida rechaza.
Resultado: _pendiente_

## AT-009 (HUB-NF-003)
PASS: 30 dias con watchdog; uptime sin reset manual.
Resultado: _pendiente_

## AT-010 (HUB-NF-005)
Pasos: bloquear WiFi 3 min; restaurar.
PASS: reconexion con backoff (1,2,4,...60 s).
Resultado: _pendiente_


## Validación end-to-end (2026-10-23)

Primera vez que el ciclo completo funciona con hardware real, de extremo a
extremo y sin intervención manual.

    Sensor ultrasónico HC-SR04 (nodo ESP32-S3, a0:f2:62:f1:d0:f0)
      │  detecta un objeto a 16 cm
      ▼
    Frame ESP-NOW v1 (magic 0xA5, cabecera 19 B, payload 7 B, CRC16)
      │
      ▼
    Hub ESP32-S3 (3cdc7562c2b0) · valida CRC y publica en 25 ms
      │  MQTT/WSS · usuario s360_hub · topic tenant/2/site/1
      ▼
    Mosquitto · ACL multi-tenant
      │
      ▼
    Node-RED · usuario nodered
      ├──> PostgreSQL · tabla events (histórico)
      └──> Gotify · notificación push al móvil

**Resultado:** PASS. La notificación de alarma llega al dispositivo del usuario.

### Qué cubre esta validación

| Tramo | Estado |
|---|---|
| ESP-NOW nodo → hub (frame v1, CRC) | Validado |
| Hub → MQTT (ACL, aislamiento por tenant) | Validado |
| MQTT → Node-RED → PostgreSQL | Validado |
| Node-RED → Gotify (push) | Validado |
| Autenticación MQTT y ACL | Validado |

### Lo que sigue pendiente de AT

- AT-001 (tiempos de boot a MQTT)
- AT-002 (armado/desarmado desde la PWA)
- AT-004 (`node_lost` tras 3 heartbeats)
- AT-006 (persistencia del estado de alarma al reiniciar)
- AT-007 en adelante (provisioning y OTA)
