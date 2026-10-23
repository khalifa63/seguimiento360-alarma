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
Resultado: _pendiente_

## AT-004 (HUB-F-008)
Pasos: nodo publica heartbeat; esperar 3 periodos sin HB.
PASS: evento node_lost tras 3 HB perdidos.
Resultado: _pendiente_

## AT-005 (HUB-F-009)
Pasos: cortar alimentacion; mosquitto_sub en status.
PASS: {"status":"offline"} en <= keepalive + 5 s.
Resultado: _pendiente_

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
