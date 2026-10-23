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
Resultado: PASS (2026-10-23, hub 3c:dc:75:62:c2:b0).

  **Implementado y verificado en esta misma sesion.** El LWT no existia.

  Cambio aplicado en main.c, dentro de start_mqtt_client():

      .session.last_will = {
          .topic   = "seguimiento360/hub/status",
          .msg     = s_lwt_payload,
          .msg_len = 0,          /* 0 = strlen */
          .qos     = 1,
          .retain  = 1,
      },

  El payload se construye antes, en un buffer estatico (ESP-IDF guarda el
  puntero, no copia el string, asi que debe sobrevivir al arranque):

      {"event":"HUB_OFFLINE","mac":"3c:dc:75:62:c2:b0"}

  El topic es el mismo que ya escucha Node-RED ("Sub HUB_ONLINE"), asi que
  el backend recibe ambos estados sin cambios.

  **Evidencia**, con marcas de tiempo de recepcion:

      12:41:36  reinicio del hub
      12:41:41  {"event":"HUB_OFFLINE","mac":"3c:dc:75:62:c2:b0"}
      12:41:41  {"event":"HUB_ONLINE","mac":"3c:dc:75:62:c2:b0","hub_name":""}

  Deteccion en 5 s. Presupuesto: keepalive (120 s por defecto) + 5 s.

  El "retain: 1" hace que el mensaje lo reciba tambien quien se suscriba
  despues del suceso, que es lo que necesita el backend si estuvo caido.

  **Pendiente menor:** hub_name llega vacio. La tabla hubs de PostgreSQL si
  tiene el nombre ("Hub Entrada Principal"), pero active_config.hub_name no
  esta poblado en el firmware. Mismo caso en HUB_ONLINE.

## AT-006 (HUB-F-010)
Pasos: armar via PWA; reiniciar hub.
PASS: tras reboot, LED refleja armed sin mediar command.
Resultado: FAIL (2026-10-23, hub 37bff1e).

  **La funcionalidad no esta conectada de extremo a extremo.**

  1. El componente sm_alarm SI esta compilado (aparece en REQUIRES de
     main/CMakeLists.txt) y expone una API completa:

         esp_err_t sm_alarm_init(sm_alarm_publish_cb_t publish_cb);
         esp_err_t sm_alarm_process_cmd(sm_alarm_cmd_t cmd);
         esp_err_t sm_alarm_notify_event(sm_alarm_event_t evt);
         esp_err_t sm_alarm_tick(void);

  2. Pero main.c **no lo invoca en ningun sitio**: cero referencias a
     sm_alarm, alarm_fsm o ALARM_ en todo el archivo. Es codigo muerto.

  3. No existe endpoint de armado/desarmado. Los http in de Node-RED son:

         post /api/auth/login          post /api/nodes
         post /api/auth/register       put  /api/nodes/:id
         post /api/register            delete /api/nodes/:id
         post /api/ota/trigger         get  /api/events
         get  /api/nodes               get  /api/overview
         get  /api/auth/nodes          get  /api/auth/profile

     Ninguno arma ni desarma.

  4. El LED de indicacion (GPIO 48) solo se usa para parpadeos
     (blink_indicator); no refleja ningun estado de alarma.

  5. La tabla alarm_state de PostgreSQL tiene un unico registro del
     2026-08-08, apuntando al hub 10 en estado "disarmed". Es un resto
     huerfano: nada lo actualiza hoy.

  La PWA menciona una vista de control de alarma (fase 3.2.3b del
  RESUMEN_FASES), pero no hay backend detras que la sostenga.

  **Trabajo pendiente:** integrar sm_alarm en main.c, exponer los endpoints
  de armado/desarmado en Node-RED y publicar el comando por MQTT.

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
| AT-002 | Armado/desarmado desde la PWA | **FAIL** (no existe la API) |
| AT-003 | Frame v1 de nodo a hub | **PASS** |
| AT-004 | `node_lost` tras 3 heartbeats | **FAIL** (no implementado) |
| AT-005 | LWT offline al caer | **FAIL** (no implementado) |
| AT-006 | Persistencia del estado de alarma | **FAIL** (no conectado) |
| AT-007+ | Provisioning y OTA | pendiente |

Cuatro de las cinco pruebas ejecutadas revelaron que la funcionalidad **no
existe**, aunque la documentacion la daba por hecha en varios casos:

- **HUB-F-009 (LWT):** 01-requisitos lo marcaba como "implementado" y
  02-interfaces afirmaba que el estado se publica por LWT. Ninguna era cierta.
  Corregido en 01-requisitos.
- **HUB-F-008 (node_lost):** ya estaba marcado como pendiente; la prueba
  confirma que sigue sin implementarse.
- **HUB-F-010 (armado/desarmado):** el componente sm_alarm existe, compila y
  no se usa. No hay endpoint que lo alimente. Es codigo muerto.
- **AT-002** depende de AT-006: sin API de armado no hay nada que probar.

Solo AT-001 y AT-003 pasan. Conviene revisar el resto de la matriz de
trazabilidad de 01-requisitos con el mismo criterio: una afirmacion
"implementado" sin una AT que lo respalde no es evidencia.

### Prioridad sugerida

1. **API de armado/desarmado + integrar sm_alarm** — desbloquea AT-002 y AT-006
   de una vez, y es lo que la PWA espera.
2. **LWT** — cambio pequeno (una linea en mqtt_cfg) con impacto alto:
   sin el, el backend no puede distinguir un hub caido de uno en silencio.
3. **node_lost** — requiere una tarea de vigilancia y poder ajustar el
   intervalo de heartbeat por configuracion para no esperar 30 minutos.
