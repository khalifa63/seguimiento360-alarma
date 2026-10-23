# SPEC-03 — Pruebas de Aceptacion del Hub (v0.1)

Formato: pasos | criterio PASS | traza | resultado (fecha, fw, PASS/FAIL)

## AT-001 (HUB-F-001/002/003, NF-001)
Pasos: flashear dev build; monitor ON; medir t desde power-on.
PASS: "WiFi OK" y "MQTT conectado" y "hello publicado" con t <= 10 s.
Resultado: PASS (2026-10-23, hub 3c:dc:75:62:c2:b0, fw 37bff1e).

  Medido con reset por hardware y captura simultanea de la consola. Las marcas
  son del propio firmware (ms desde power-on):

      2112 ms  Tenant: 2 | Site: 1
      2695 ms  Obtenida IP: 192.168.10.12
      2705 ms  Cliente MQTT iniciado
      4844 ms  MQTT_EVENT_CONNECTED
      4851 ms  Publicado HUB_ONLINE

  Tiempo a MQTT conectado y estado publicado: 4,85 s.
  Presupuesto: <= 10 s. Cumple con margen 2x.

  El hub publica HUB_ONLINE en lugar de un topic "hello" (ver HUB-IF-003).

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
Resultado: FAIL (2026-10-23, hub 37bff1e).

  **node_lost no esta implementado.** El hub guarda la marca de actividad del
  nodo pero no la vigila:

      // main.c:702, al recibir un evento
      node_registry.nodes[node_idx].last_ping = get_unix_now();

  No existe ninguna tarea que compare last_ping contra la hora actual, ni
  codigo que publique un evento de nodo perdido. Las tareas creadas en main.c
  son portal_inactivity_task y las de wifi/mqtt; ninguna vigila nodos.

  Reproducido por inspeccion: no hay forma de que se emita node_lost.

  **Nota de viabilidad:** HEARTBEAT_INTERVAL_SEC es 600 (10 minutos), asi que
  la prueba exige 30 minutos de espera. Para validarla conviene poder bajar el
  intervalo por configuracion en lugar de recompilar.

  Coincide con 01-requisitos.md, que ya marca HUB-F-008 como pendiente.

## AT-005 (HUB-F-009)
Pasos: cortar alimentacion; mosquitto_sub en status.
PASS: {"status":"offline"} en <= keepalive + 5 s.
Resultado: FAIL (2026-10-23, hub 37bff1e).

  **El LWT no esta implementado.**

  La estructura esp_mqtt_client_config_t de main.c (linea 516) no define
  .session.last_will, y no hay ninguna referencia a LWT en el firmware:

      esp_mqtt_client_config_t mqtt_cfg = {
          .broker.address.uri = broker_uri,
          .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
          .credentials.client_id = client_id_str,
          .credentials.username = "s360_hub",
          .credentials.authentication.password = S360_MQTT_PASSWORD,
      };

  Reproducido: con mosquitto_sub escuchando en seguimiento360/hub/status y
  reiniciando el hub, solo llega HUB_ONLINE al reconectar. No hay mensaje
  offline en ningun momento.

  Lo que SI funciona: el hub publica su estado al conectar.

      seguimiento360/hub/status {"event":"HUB_ONLINE","mac":"3c:dc:75:62:c2:b0","hub_name":""}

  **Contradiccion documental:** 01-requisitos.md marca HUB-F-009 como
  "implementado", y 02-interfaces.md afirma que "el estado se publica por
  LWT". Ninguna de las dos cosas es cierta. Corregir ambas.

  **Trabajo pendiente:** anadir .session.last_will al mqtt_cfg con topic
  seguimiento360/hub/status, payload {"event":"HUB_OFFLINE",...} y QoS 1,
  retenido. El ACL ya concede "topic write" sobre ese topic, que es lo
  correcto para un LWT.

  Aparte: hub_name llega vacio porque active_config.hub_name no esta
  poblado. La tabla hubs de PostgreSQL si tiene el nombre ("Hub Entrada
  Principal").

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

### Resumen de AT (2026-10-23)

| AT | Que verifica | Resultado |
|---|---|---|
| AT-001 | Boot a MQTT <= 10 s | **PASS** (4,85 s) |
| AT-002 | Armado/desarmado desde la PWA | pendiente |
| AT-003 | Frame v1 de nodo a hub | **PASS** |
| AT-004 | `node_lost` tras 3 heartbeats | **FAIL** (no implementado) |
| AT-005 | LWT offline al caer | **FAIL** (no implementado) |
| AT-006 | Persistencia del estado de alarma | pendiente |
| AT-007+ | Provisioning y OTA | pendiente |

Tres de las pruebas revelaron que la funcionalidad **no existe**, aunque la
documentacion la daba por hecha:

- **HUB-F-009 (LWT):** 01-requisitos lo marcaba como "implementado" y
  02-interfaces afirmaba que el estado se publica por LWT. Ninguna era cierta.
  Corregido en 01-requisitos.
- **HUB-F-008 (node_lost):** ya estaba marcado como pendiente; la prueba
  confirma que sigue sin implementarse.
- **AT-002 y AT-006:** sin ejecutar todavia.
