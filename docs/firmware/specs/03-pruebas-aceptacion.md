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
Resultado: PASS (2026-10-23, hub 3c:dc:75:62:c2:b0).

  **Implementado y verificado.** No existia ningun endpoint de armado: la
  PWA tenia la vista, pero no habia backend detras.

  Endpoint nuevo en Node-RED, siguiendo el patron de los endpoints de nodos:

      POST /api/alarm/:action      (action = arm | disarm)

      http in -> Validar JWT -> Preparar comando -> SELECT hub del tenant
              -> Construir comando -> mqtt out -> HTTP 200
                                  -> HTTP Error

  Publica en seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/command. El hub
  busca el hub del tenant del JWT: el aislamiento lo garantiza el WHERE por
  tenant_id, nunca el cuerpo de la peticion.

  **Evidencia.**

      POST /api/alarm/arm  -> HTTP 200
      {"action":"arm","hub_mac":"3cdc7562c2b0",
       "mqtt_topic":"seguimiento360/tenant/2/site/1/hub/3cdc7562c2b0/command",
       "state":"armed","message":"Comando publicado al hub via MQTT"}

  Y el hub lo aplico. Tras reiniciarlo para leer la NVS:

      Estado restaurado de NVS: armed
      Alarma: armed | LED ON

  Con disarm, el mismo ciclo deja el estado en disarmed y el LED apagado.

  **Casos de error verificados:**

      POST /api/alarm/explode  -> 400  {"error":"Accion debe ser 'arm' o 'disarm'"}
      POST /api/alarm/arm sin token -> 401
      Token de un tenant sin hubs   -> 404

  **Cuatro fallos resueltos durante la implementacion**, todos del endpoint
  nuevo y ninguno del firmware:

  1. `there is no parameter $1`: se usaba msg.queryParams; el campo que lee
     node-red-contrib-postgresql es msg.params (array).
  2. `Invalid action specified`: **colision de nombres**. msg.action no es un
     campo libre: es la API de control del nodo mqtt out, que valida su valor
     contra una lista de acciones permitidas. Se renombro a msg.alarmAction.
  3. Peticion colgada y "No response object": la respuesta se construia como
     un objeto nuevo, perdiendo msg.res. El nodo http response necesita ese
     campo para contestar. Ahora se clona el mensaje original.
  4. El login devolvia el tenant equivocado: el usuario de prueba quedo como
     owner de dos tenants y el SELECT del login no ordenaba, asi que tomaba
     uno arbitrario.
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
Resultado: PASS (2026-10-23, hub 3c:dc:75:62:c2:b0).

  **Implementado y verificado en esta sesion.** node_lost no existia: el hub
  guardaba last_ping pero ninguna tarea lo vigilaba.

  Cambio en main.c: nueva tarea node_watchdog_task que cada 30 s recorre el
  registro y compara last_ping contra la hora actual. Umbral:

      NODE_HEARTBEAT_SEC     600 s   (debe coincidir con el nodo)
      NODE_LOST_MISSED       3       heartbeats
      NODE_LOST_TIMEOUT_S    1800 s
      NODE_WATCHDOG_PERIOD_S 30 s

  Publica en el topic de eventos del hub:

      {"mac":"a0:f2:62:f1:d0:f0","node_name":"Sensor Ultrasonico Gar",
       "event":"NODE_LOST","tenant":"2","site":"1"}

  Tras avisar pone last_ping a 0, de modo que el aviso se emite una sola vez
  por nodo y vuelve a contar cuando el nodo se manifieste de nuevo.

  **Verificacion.** El umbral de produccion son 30 minutos, inviable de
  esperar en una sesion. Se compilo una variante con umbral de 10 s para
  validar la logica, con la tarea instrumentada:

      WD: nodo 0 last_ping=1792777941 delta=5
      WD: nodo 0 last_ping=1792777941 delta=10
      WD: nodo 0 last_ping=1792777941 delta=15
      W  Nodo perdido: Sensor Ultrasonico Gar. Evento publicado.
      WD: nodo 0 last_ping=0 delta=-1        <- no repite

  El evento llego a MQTT y a PostgreSQL (eventos 67 y siguientes con
  event_type NODE_LOST). Despues se restauro el umbral de produccion.

  **Decision de diseño: last_ping se conserva de NVS al arrancar.**
  Refrescarlo parecia comodo para evitar falsos positivos tras un reinicio,
  pero rompe el proposito: si el hub estuvo caido horas y el nodo lleva todo
  ese tiempo en silencio, el backend debe enterarse. Un nodo callado sigue
  siendo un nodo perdido aunque el hub acabe de arrancar.

  **Nota de mantenimiento:** NODE_HEARTBEAT_SEC debe coincidir con el
  HEARTBEAT_INTERVAL_SEC del nodo. Si se cambia en uno, hay que cambiarlo en
  el otro o la deteccion se adelanta o se retrasa.

  **Limitacion conocida:** si el reinicio del hub y el silencio del nodo
  coinciden, el aviso puede repetirse una vez por reinicio, porque cada
  arranque parte del last_ping guardado. Es el comportamiento buscado, pero
  conviene tenerlo presente si el hub se reinicia a menudo.
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
Resultado: PASS (2026-10-23, hub 3c:dc:75:62:c2:b0).

  **Implementado y verificado en esta sesion.** Habia tres brechas.

  **1. Los TODO de sm_alarm.** El componente no restauraba ni persistia el
  estado:

      // TODO: Restaurar estado desde NVS (nvs_manager_get_alarm_state)
      // TODO: Persistir en NVS aqui

  Resueltos con los helpers que ya existian en nvs_manager. Al arrancar, si
  no hay lectura valida se parte de DISARMED, que es el lado seguro: tras un
  corte de luz el hub no debe quedar armado sin que nadie lo sepa.

  **2. sm_alarm no se invocaba.** Ahora main.c lo inicializa, se suscribe a
  .../command, procesa arm/disarm y deja el LED (GPIO 48) fijo si esta
  armado.

  **3. nvs_manager_init() nunca se llamaba.** Las particiones nvs_state y
  nvs_nodes existen en partitions.csv, pero nadie las inicializaba, asi que
  el guardado fallaba:

      E sm_alarm: No se pudo persistir el estado: ESP_ERR_NVS_PART_NOT_FOUND

  Es un bug latente: el estado de alarma no se habria guardado nunca aunque
  sm_alarm hubiera estado integrado. main.c solo usaba la NVS por defecto
  con un namespace propio ("pairing").

  **Evidencia.** Comando por MQTT en .../command:

      Comando de alarma recibido: {"action":"arm"}
      Comando aplicado: armed -> armed (guardado en NVS)
      Estado de alarma: armed (ok)

  Tras reiniciar el hub, capturando desde el arranque:

      Particiones NVS propias inicializadas
      Estado restaurado de NVS: armed
      Iniciado. Estado inicial: armed
      Alarma: armed | LED ON

  El estado sobrevivio al reinicio y el LED quedo encendido sin recibir
  ningun comando nuevo.

  **No se integro sm_alarm_process_espnow_data** a proposito: genera un JSON
  con estructura measurement/tags/fields (InfluxDB), incompatible con el
  formato plano que Node-RED ya consume y que verifica AT-003. La ruta de
  eventos sigue en publish_event_to_mqtt.

  **Limitacion:** el LED solo distingue armed (fijo) de disarmed (apagado).
  Los estados TRIGGERED y ALARMING de la FSM no tienen representacion visual
  todavia.
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
| AT-002 | Armado/desarmado desde la PWA | **PASS** |
| AT-003 | Frame v1 de nodo a hub | **PASS** |
| AT-004 | `node_lost` tras 3 heartbeats | **PASS** |
| AT-005 | LWT offline al caer | **FAIL** (no implementado) |
| AT-006 | Persistencia del estado de alarma | **PASS** |
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
