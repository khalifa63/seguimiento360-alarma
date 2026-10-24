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

## AT-011 (HUB-F-013) — pairing del nodo
Pasos: borrar la NVS del nodo; reiniciarlo; observar consola y eventos.
PASS: el nodo pide PAIR, recibe el ACK, guarda la MAC y empieza a enviar.
      Tras reiniciar, usa la MAC de NVS sin repetir el descubrimiento.
Resultado: PASS (2026-10-23, nodo v2.1, hub 3c:dc:75:62:c2:b0).

  **Nodo de fabrica (NVS borrada con erase_region 0x9000 0x6000):**

      Sin hub en NVS (ESP_ERR_NOT_FOUND). Se usara la semilla
      3c:dc:75:62:c2:b1 para pedir el pairing.
      Solicitando emparejamiento (seq=0)
      ACK del hub recibido (seq=0)
      Nodo VINCULADO con el hub 3c:dc:75:62:c2:b0

  El pairing aprendio la MAC real. Detalle relevante: la semilla apunta al
  SoftAP del hub (...c2:b1) y el ACK llega desde su STA (...c2:b0). Son
  distintas, y el pairing corrige la semilla automaticamente.

  **Tras reiniciar, sin repetir el descubrimiento:**

      Hub vinculado en NVS: 3c:dc:75:62:c2:b0
      (sigue enviando eventos con normalidad)

  **Modo sin vincular:** si no llega el ACK, el nodo insiste cada 30 s y el
  LED parpadea. Verificado antes de implementar la semilla: los reintentos
  aparecen en consola con seq incremental.

  **Pipeline intacto:** tras el pairing, un evento publicado en el topic del
  hub llega a PostgreSQL (evento 72, ALRM 42 cm).

### Resumen de AT (2026-10-23)

| AT | Que verifica | Resultado |
|---|---|---|
| AT-001 | Boot a MQTT <= 10 s | **PASS** (4,85 s) |
| AT-002 | Armado/desarmado desde la PWA | **PASS** |
| AT-003 | Frame v1 de nodo a hub | **PASS** |
| AT-004 | `node_lost` tras 3 heartbeats | **PASS** |
| AT-005 | LWT offline al caer | **PASS** (5 s) |
| AT-006 | Persistencia del estado de alarma | **PASS** |
| AT-011 | Pairing del nodo con el hub | **PASS** |
| AT-012 | Recuperacion ante hub ausente | **PASS** |
| AT-013 | Presupuesto de ACK y RX->publish | **PASS** (ACK 14 ms, publish 46-92 ms) |
| AT-014 | Build reproducible desde clon limpio (CI) | **PASS** |
| AT-015 | OTA de punta a punta | **PASS** |
| AT-016 | Rollback ante imagen que no confirma | **PASS** |
| AT-017 | Firma de imagen: acepta firmada, rechaza sin firmar | **PASS** |
| AT-018 | OTA del nodo (con firma verificada) | **PASS** |
| AT-007 | Provisioning SoftAP + portal | pendiente |
| AT-009 | Operacion 24/7 sin reset 30 dias | pendiente (prueba de calendario) |
| AT-010 | Backoff exponencial con jitter | pendiente |

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

### Cuenta del hub

Durante las pruebas de AT-002 se creo un usuario temporal. Ya esta eliminado.

El tenant 2 "Casa Antonio" —el que contiene el hub fisico, su nodo y el
historico de eventos— pertenece ahora a la cuenta real del propietario. Se
eligio reasignarlo en lugar de borrarlo porque eliminarlo habria dejado el
hub sin usuario que pueda autenticar la API de armado.

### Nota sobre el login

El endpoint de login toma el tenant del primer registro que devuelve su
consulta, sin ORDER BY. Si un usuario es propietario de mas de un tenant,
el token puede traer uno arbitrario. Ocurrio durante las pruebas y costo un
rato de diagnostico. Conviene anadir un ORDER BY explicito.

### Prioridad sugerida

1. **API de armado/desarmado + integrar sm_alarm** — desbloquea AT-002 y AT-006
   de una vez, y es lo que la PWA espera.
2. **LWT** — cambio pequeno (una linea en mqtt_cfg) con impacto alto:
   sin el, el backend no puede distinguir un hub caido de uno en silencio.
3. **node_lost** — requiere una tarea de vigilancia y poder ajustar el
   intervalo de heartbeat por configuracion para no esperar 30 minutos.

## AT-013 (HUB-NF-002) — Presupuestos del enlace

Los dos presupuestos no funcionales del enlace, medidos en el hub con trafico
real. La instrumentacion toma el instante de recepcion como primera sentencia
de `on_data_recv()` y lo pasa a `send_ack_v1()` y a
`publish_event_to_mqtt()`, que reportan la diferencia.

| Medida | Presupuesto | Medido | Resultado |
|---|---|---|---|
| RX -> ACK | <= 50 ms | 14 ms (constante, 5 muestras) | **PASS** |
| RX -> publish | <= 250 ms | 46-92 ms (14 muestras) | **PASS** |

### El presupuesto destapo una perdida de eventos

La primera medicion en rafaga dio un `RX->publish` de **10093 ms**, con
eventos posteriores que no se publicaban en absoluto:

```text
I (170870) HUB_S3_MAIN: RX->publish en 47 ms (presupuesto 250 ms)
W (220685) HUB_S3_MAIN: RX->publish en 10093 ms (presupuesto 250 ms SUPERADO)
I (221034) HUB_S3_MAIN: ESP-NOW RX (v1): Code=ALRM ... Seq=16
I (225755) HUB_S3_MAIN: ESP-NOW RX (v1): Code=ALRM ... Seq=17
```

`esp_mqtt_client_publish` es asincrono: con la cola de salida llena, bloquea
hasta encontrar hueco y, si no lo hay, devuelve `msg_id <= 0` y el evento se
pierde. El hub no configuraba `buffer.out_size`, asi que usaba el valor por
defecto.

Corregido en dos partes:

1. `buffer.out_size = 4096` en `esp_mqtt_client_config_t`, para que una rafaga
   de alarma no sature la cola.
2. Un contador `s_events_dropped`. Un evento que no se publica ahora se
   registra como error con el total acumulado, en vez de desaparecer.

Medicion tras el arreglo, con la misma rafaga:

```text
ALRM recibidos: 15 | publicados: 14 | latencia: min=46 max=92 ms | descartados: 0
```

El evento restante seguia en cola al cerrar la captura; no se perdio.

### Nota sobre la medicion

El cronometraje de `publish` termina al entregar el mensaje al cliente MQTT,
no al recibir el PUBACK del broker: esperar la confirmacion mediria otra cosa
(el viaje completo a Mosquitto) y bloquearia el callback de recepcion.

## AT-014 (FW-010) — Build reproducible desde un clon limpio

El CI compila hub y nodo en cada PR que toque firmware
(`.github/workflows/firmware.yml`, IDF v5.2.2). Para que sirva de algo, la
compilacion debe partir de lo mismo que vera GitHub: un checkout **sin**
`build/` ni `sdkconfig`, que estan ignorados.

La prueba se hace clonando el repo y compilando ahi, no en el arbol de
trabajo, porque el arbol local tiene un `sdkconfig` ya generado que enmascara
los errores de configuracion.

### Resultado: PASS, con dos fallos destapados

**1. `sdkconfig.defaults` no estaba versionado.** El `.gitignore` lo capturaba
con `**/sdkconfig.defaults`. Son archivos de configuracion del producto
—target, particiones de 16 MB, PSRAM, WSS, OTA— y sin ellos el CI compilaba
con los valores por defecto de IDF: flash de 2 MB, sin `partitions.csv`, sin
PSRAM ni soporte TLS. El CI habria validado algo distinto de lo que se
flashea. Corregido en `.gitignore`; `sdkconfig` y `sdkconfig.old` siguen
ignorados porque los genera `idf.py` en cada maquina.

**2. El target del nodo era el equivocado.** El `sdkconfig.defaults` del nodo
declaraba:

    CONFIG_IDF_TARGET="esp32"

pero el nodo es un **ESP32-S3** (USB nativo `303a:1001`). IDF tomaba la
declaracion como cierta y compilaba para el chip equivocado:

```text
-- IDF_TARGET is not set, guessed 'esp32' from sdkconfig '.../sdkconfig.defaults'
-- Building ESP-IDF components for target esp32
```

El build **terminaba sin error**, pero el binario no arranca en el nodo. No se
veia en el servidor porque el `sdkconfig` local se habia generado con el
target explicito y decia `esp32s3`; el fallo solo aparecia en un checkout sin
historial. Es exactamente el tipo de rotura silenciosa que justifica el CI.

Tras corregirlo:

```text
-- IDF_TARGET is not set, guessed 'esp32s3' from sdkconfig '.../sdkconfig.defaults'
-- Building ESP-IDF components for target esp32s3
Project build complete.
```

El hub ya compilaba correctamente desde limpio, y su salida confirma que los
defaults se aplican: `flash_size 16MB` con `ota_data_initial.bin`, que solo
existen con `partitions.csv` activo.

### Fuera de alcance

`firmware/node_sim/` es un simulador legacy ("Compatible con HUB Fase 4") con
pines de ESP32 clasico. Su `sdkconfig.defaults` declara `esp32c3` mientras su
`sdkconfig` local dice `esp32`. Se versiono el archivo para no dejarlo
huerfano, pero el proyecto no entra en el CI ni se ha corregido: conviene
decidir si se archiva en `docs/_archivo/`.

## AT-015 (HUB-F-012) — OTA de punta a punta

Actualización completa por MQTT, verificada en hardware.

**Procedimiento.** Se publica la URL en el topic `ota_cmd` del hub y se
observa su consola. Antes hay que subir la imagen a
`~/iot-alarma/website/ota/` del servidor, que se sirve por
`https://seguimiento360.com/ota/` a través de Cloudflare Tunnel.

**Resultado: PASS.**

```text
I: Comando OTA recibido. Longitud: 49
I: URL de OTA validada: 'https://seguimiento360.com/ota/firmware_6.2.0.bin'
I: esp_https_ota: Writing to partition subtype 17 at offset 0x1a0000
I: Descarga 9% (101376/1124080 bytes)
   ... 19%, 29%, ... 99%
I: ✅ OTA exitoso. Reiniciando en 3 segundos...
I: boot: Loaded app from partition at offset 0x1a0000
I: cpu_start: App version:      6.2.0
I: ✅ MQTT_EVENT_CONNECTED
```

1,1 MB en ~28 s. La imagen va a la partición inactiva (`ota_1`) y el
bootloader conmuta. `HUB_ONLINE` confirma la versión nueva y la partición.

### Cuatro fallos que impedían que funcionara

**1. El rollback anulaba toda actualización.** Nadie llamaba a
`esp_ota_mark_app_valid_cancel_rollback()`. Con
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, cada imagen nueva arranca en
`PENDING_VERIFY` y el bootloader la revierte. Cualquier OTA se deshacía sola.

**2. La URL no se validaba.** Bastaba con que el payload del `ota_cmd`
contuviera `"http"` en cualquier parte, así que un comando podía apuntar a un
servidor ajeno y el hub lo instalaba. Ahora exige HTTPS y el origen
`S360_OTA_HOST`.

**3. Timeout de 10 s** para un binario que tarda ~28 s: cortaba la descarga a
mitad. Ahora 60 s con keep-alive.

**4. Versión incomparable.** ESP-IDF usaba `git describe` y producía
`v6.0.0-pre-ota-59-gbb71bbb-dirt`. Sin una versión legible no se puede decidir
a qué hubs actualizar. Fijada con `PROJECT_VER`.

Además, la versión y la partición activa viajan ahora en `HUB_ONLINE` y en
`/status`, y la descarga informa del progreso cada 10%.

### Nota de infraestructura

El `nginx` propio escucha en el puerto **8088**; el 80 lo ocupa otro
contenedor (`docker-nginx-1`, del sandbox de Dify). Consultar el puerto
equivocado devuelve 404 y parece que la ruta no existe. La ruta `/ota/` sí
está publicada, y por HTTPS público funciona sin tocar infraestructura.

## AT-016 (HUB-F-012) — Rollback ante imagen que no confirma

Comprueba que una actualización que se instala pero no llega a funcionar se
revierte sola.

**Procedimiento.** Se compila una imagen deliberadamente no confirmada: arranca
y hace algo visible, pero nunca llama a
`esp_ota_mark_app_valid_cancel_rollback()`. Se instala por OTA y se reinicia.

**Resultado: PASS.**

```text
I: boot: Loaded app from partition at offset 0x1a0000
I: cpu_start: App version:      6.3.0-roto
W: IMAGEN SIN CONFIRMAR: 6.3.0-roto
W: Particion: ota_1 | estado: 1          <- ESP_OTA_IMG_PENDING_VERIFY
W: sigo sin confirmar...
```

Tras reiniciar, el bootloader revirtió:

```text
I: cpu_start: App version:      6.1.0
```

### El rollback no usa temporizador

La imagen sin confirmar se quedó corriendo **indefinidamente** sin revertir.
El bootloader solo evalúa en el arranque siguiente. Esto tiene una
consecuencia que conviene tener presente: un firmware que arranca y se queda
vivo sin funcionar **no se revierte solo**; la reversión depende de que el
equipo se reinicie (watchdog, caída de tensión o reset).

`CONFIG_BOOTLOADER_APP_ROLLBACK_TIME=30` no existe en IDF v5.2: era una opción
inventada en el `sdkconfig.defaults` que nunca tuvo efecto.

### Un fallo de configuración que ocultaba todo esto

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` estaba en `sdkconfig.defaults` pero no
en el `sdkconfig` local, generado antes. Es decir: **el rollback estaba
desactivado en el binario real**, y el primer OTA pasó sin ninguna protección.
`sdkconfig.defaults` solo se lee al crear el `sdkconfig`; un archivo ya
existente conserva sus valores.

## AT-017 (HUB-F-012) — Firma de la imagen

Verifica que el hub solo instala firmware firmado con la clave del servidor.

**Diseño.** Se usa `SECURE_SIGNED_APPS_NO_SECURE_BOOT` con RSA-3072, y no
Secure Boot por hardware. Cubre el riesgo real —un servidor o un DNS
comprometidos sirviendo firmware ajeno— **sin quemar eFuses**, así que perder
la clave no deja el hub inservible. No protege contra acceso físico.

La clave privada vive en `~/seguimiento360-claves-ota/` del servidor (permisos
600) y **no está en el repo**: el `.gitignore` ya bloqueaba `*.pem`.

**Resultado: PASS** en ambas direcciones.

Binario sin firma:

```text
I: esp_image: Verifying image signature...
E: esp_image: Secure boot signature verification failed
W: esp_image: image valid, signature bad
E: ❌ Error al finalizar OTA: ESP_ERR_OTA_VALIDATE_FAILED
```

El hub descartó la imagen y siguió en la versión anterior.

Binario firmado:

```text
I: esp_image: Verifying image signature...
I: secure_boot_v2_rsa: Signature verified successfully!
I: ✅ OTA exitoso. Reiniciando en 3 segundos...
I: cpu_start: App version:      6.4.0
```

### Cómo firmar

```bash
espsecure.py generate_signing_key --version 2 --scheme rsa3072 ota_signing_key.pem
espsecure.py extract_public_key --version 2 --keyfile ota_signing_key.pem ota_signing_key.pub.pem
```

`extract_public_key` recibe el archivo de salida como **argumento posicional**,
no como `--public_key_file`.

La clave se pasa al build con `CONFIG_SECURE_BOOT_SIGNING_KEY`. Debe ser una
ruta absoluta si la clave vive fuera del proyecto.

### Pendiente

La firma cubre la **app**, no el bootloader. Un atacante con acceso físico
podría reemplazar el bootloader y saltarse la verificación. Cerrarlo exige
Secure Boot por hardware, que sí quema eFuses y por tanto es irreversible.

## AT-018 (FW-012) — OTA del nodo

Un nodo ya instalado no se podía actualizar sin ir a buscarlo con un cable:
usaba la tabla de particiones por defecto de IDF, sin `ota_0`/`ota_1`, y su
`main` no incluía `esp_https_ota`. Para un equipo en campo eso es quedarse sin
posibilidad de corregir un fallo.

**Procedimiento.** Se publica en `node_cfg` un JSON con la MAC del nodo y la
URL del firmware. El hub reenvía al nodo, por ESP-NOW, las credenciales WiFi
de la casa y esa URL. El nodo se asocia, descarga por HTTPS, verifica la firma
y escribe en la partición inactiva.

**Resultado: PASS.**

```text
HUB:  OTA solicitada para a0:f2:62:f1:d0:f0: https://.../node_2.0.0-signed.bin
HUB:  Config OTA enviada al nodo (SSID 'KHALIFA_R', clave de 8 bytes, URL ...)
NODO: Config recibida del hub: SSID 'KHALIFA_R', URL ...
NODO: AP 'KHALIFA_R' en canal 5, compatible con ESP-NOW.
NODO: Conectado a 'KHALIFA_R'
NODO: Descarga 19% (188416/987136)
      ... 39%, 59%, 79%, 99%
NODO: OTA aplicado. Reiniciando en 3 s...
BOOT: Loaded app from partition at offset 0x120000
BOOT: cpu_start: App version:      2.0.0-ota
NODO: Imagen 2.0.0-ota confirmada: rollback cancelado.
```

987 KB en ~24 s, con la misma firma RSA-3072 y el mismo rollback que el hub.

### Tabla de particiones

    nvs       0x9000   0x6000
    otadata   0xf000   0x2000
    phy_init  0x11000  0x1000
    ota_0     0x20000  0x100000
    ota_1     0x120000 0x100000

La `nvs` **conserva su offset**: es donde el nodo guarda la MAC del hub. Al
moverla habría que re-emparejar cada nodo ya instalado. Verificado que tras
flashear la tabla nueva el nodo siguió vinculado.

Las apps van en `0x20000` y `0x120000` porque las particiones de tipo app deben
empezar en un múltiplo de `0x10000`. `gen_esp32part` rechaza lo demás.

### Por qué la imagen no viaja por ESP-NOW

Es la pregunta obvia, y la respuesta está en los números: el payload útil de un
frame son 229 bytes, así que 700 KB son **3075 fragmentos**. ESP-NOW no tiene
retransmisión por fragmento, de modo que con una pérdida del 0,01% por
fragmento la probabilidad de recibir el binario entero es del 74%; con 0,1%,
del 4,6%. Haría falta un protocolo de fragmentación con ACK y reenvío. Darle
WiFi al nodo es bastante más simple, y ya tenía `esp_wifi` para ESP-NOW.

### Dos fallos encontrados al probarlo

**1. Faltaba la interfaz de red de la STA.** El nodo llamaba a
`esp_netif_init()` pero nunca a `esp_netif_create_default_wifi_sta()`. No se
notaba porque hasta ahora el radio solo servía para ESP-NOW, que no necesita
interfaz de red. `esp_wifi_connect()` fallaba siempre con `ESP_ERR_TIMEOUT`.

**2. El comando OTA no llegaba al handler.** Estaba dentro del bloque que el
handler de `node_cfg` exige para la configuración normal (nombre, ubicación,
habitación y umbral). Un comando de actualización solo trae MAC y URL, así que
nunca se alcanzaba.

### Comprobación del canal

El nodo se asocia a un AP, lo que mueve el radio al canal de ese AP y
rompería ESP-NOW si no coincidiera. El nodo **escanea primero y aborta si el
AP no está en el canal de ESP-NOW**:

```text
OTA_NODE: AP 'KHALIFA_R' en canal 5, compatible con ESP-NOW.
```

### Firma y rollback del nodo

Mismo esquema que el hub: `SECURE_SIGNED_APPS_NO_SECURE_BOOT` con RSA-3072 y
la misma clave. Una imagen sin firma válida se rechaza con
`ESP_ERR_OTA_VALIDATE_FAILED`. La confirmación se hace al arrancar, después de
inicializar sensor, LED y NVS.

### Verificacion de la firma

Probada en las dos direcciones, igual que en el hub.

Imagen firmada con **otra clave** (era la prueba concluyente):

```text
NODO:   clave[0] sha256=444dfa588ecaf9cb889abe4d124c6f59...
NODO:   Conectado a 'KHALIFA_R'
NODO:   Descarga 99% (977920/987136)
esp_image: Secure boot signature verification failed
esp_image: image valid, signature bad
NODO:   OTA rechazado: ESP_ERR_OTA_VALIDATE_FAILED
BOOT:   cpu_start: App version:      1.0.0-ota     <- sigue en la anterior
```

Imagen firmada con la clave propia:

```text
esp_image: Verifying image signature...
secure_boot_v2_rsa: Signature verified successfully!
NODO:   OTA aplicado. Reiniciando en 3 s...
BOOT:   cpu_start: App version:      2.0.0-ota
```

Para el diagnostico, el nodo registra con que clave va a verificar. Con
`SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` no hay clave compilada: la
verificacion usa los digests de la **app en ejecucion**, y si esta no
estuviera firmada `esp_secure_boot_init_checks()` aborta al arrancar.

### Una prueba que engano

La primera version de esta AT daba la firma del nodo por no verificada. El
motivo fue un error de metodo, no del firmware: el archivo publicado como
`node_2.0.0-unsigned.bin` era en realidad la salida **ya firmada** (el
`-unsigned.bin` es el artefacto intermedio que el build firma despues). El
nodo decia `Signature verified successfully!` porque la imagen era legitima.

De ahi salio una teoria equivocada sobre claves no embebidas que costo varias
rondas. La prueba que resolvio el asunto no depende de interpretar binarios ni
de como se llame un archivo: **firmar con otra clave y comprobar que se
rechaza**.
