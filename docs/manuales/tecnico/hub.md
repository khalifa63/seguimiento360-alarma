# Hub Seguimiento 360 — Manual tecnico

Datos de ingenieria del concentrador. Para la version divulgativa, ver
[usuario/hub.md](../usuario/hub.md).

## Que es

Puente entre los sensores y la nube. Recibe tramas ESP-NOW de los nodos, las
valida y publica el evento en MQTT sobre WSS. Tambien hace de portal de
configuracion y actualiza los nodos por el aire.

    [Nodos] --ESP-NOW--> [HUB] --MQTT/WSS--> [Mosquitto] --> [Node-RED] --> [PostgreSQL]
                            |
                            +--SoftAP--> [Portal de configuracion]

## Hardware

| Elemento | Valor |
|---|---|
| MCU | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | Si, en modo octal a 80 MHz |
| LED indicador | GPIO 48 |
| Consola | USB nativo (Espressif USB JTAG) |
| Alimentacion | 5 V por USB |

La consola solo funciona por el **USB nativo**. Con un adaptador USB-serie
externo no hay salida: es un fallo que costo tiempo al principio y conviene
recordarlo.

## Particiones

| Nombre | Tipo | Offset | Tamano | Uso |
|---|---|---|---|---|
| `nvs` | data nvs | 0x9000 | 0x6000 | configuracion (SSID, tenant, site, claim) |
| `nvs_state` | data nvs | 0xf000 | 0x4000 | estado de la alarma |
| `nvs_nodes` | data nvs | 0x13000 | 0x8000 | registro de nodos vinculados |
| `otadata` | data ota | 0x1b000 | 0x2000 | que particion arranca |
| `phy_init` | data phy | 0x1d000 | 0x1000 | calibracion de radio |
| `ota_0` | app | 0x20000 | 0x180000 | firmware A (1,5 MB) |
| `ota_1` | app | 0x1a0000 | 0x180000 | firmware B (1,5 MB) |

Las tres particiones NVS estan separadas a proposito: la configuracion se puede
borrar para reconfigurar el hub sin perder el registro de nodos ni el estado de
la alarma.

## Radio

| Parametro | Valor |
|---|---|
| Canal ESP-NOW | **5** (fijo, `S360_ESPNOW_CHANNEL`) |
| Interfaz de los peers | `ESP_IF_WIFI_STA` |
| Modo con configuracion | `WIFI_MODE_STA` |
| Modo sin configuracion | `WIFI_MODE_APSTA` |
| Red de configuracion | `S360-XXXX`, clave igual al nombre |

**El canal 5 es fijo y compartido.** La red WiFi de la casa debe estar en ese
canal: si el router esta en otro, el hub no puede usar ESP-NOW y WiFi a la vez
sin perder el enlace con los nodos.

Los peers se registran por la interfaz **STA**, no la AP. Cuando el hub ya esta
configurado va en modo STA puro, asi que una referencia a `ESP_IF_WIFI_AP`
fallaria. Fue un fallo real al introducir el portal condicional.

## Portal de configuracion

Solo se levanta **si falta configuracion**. Un hub ya configurado arranca en STA
sin SoftAP: asi no queda expuesto de forma permanente a que alguien lo
reconfigure.

| Aspecto | Valor |
|---|---|
| Direccion | `http://192.168.4.1` |
| Red | `S360-` + ultimos 4 de la MAC, en mayusculas |
| Clave | La misma que el nombre |
| Cierre por inactividad | 10 minutos |
| Formulario | SSID, contrasena, tenant, site, claim code |

Reconfigurar un hub ya provisionado exige borrar su configuracion
(`/factory_reset` o el boton, estando cerca).

## MQTT

| Aspecto | Valor |
|---|---|
| Broker | `wss://mqtt.seguimiento360.com:443` |
| Cliente | `hub_<mac sin separadores>` |
| Usuario | `s360_hub` |
| Topicos | `seguimiento360/tenant/<tid>/site/<sid>/hub/<mac>/...` |

Topicos que usa:

| Sufijo | Direccion | Uso |
|---|---|---|
| `event` | hub -> nube | eventos de los nodos |
| `ota_cmd` | nube -> hub | orden de actualizacion |
| `node_cfg` | nube -> hub | configuracion y OTA de nodos |
| `command` | nube -> hub | armar y desarmar |
| `seguimiento360/hub/status` | hub -> nube | `HUB_ONLINE` y testamento |

`HUB_ONLINE` incluye `mac`, `hub_name`, `version`, `partition`, `uptime_s` y
`reset_reason`. Los dos ultimos permiten comprobar la operacion a largo plazo
sin acceso a la consola.

## Presupuestos de latencia

Medidos con trafico real:

| Medida | Presupuesto | Medido |
|---|---|---|
| Recepcion -> ACK al nodo | <= 50 ms | 14 ms |
| Recepcion -> publicacion | <= 250 ms | 46-92 ms |

El ACK se envia **antes** de procesar el evento, para no gastar el presupuesto
en el parseo.

## Actualizacion por el aire

| Aspecto | Valor |
|---|---|
| Particion destino | la inactiva (`ota_0` / `ota_1`) |
| Verificacion | firma RSA-3072 obligatoria |
| Rollback | activo, se confirma al conectar MQTT |
| Version | `6.1.0`, fijada en `CMakeLists.txt` |

Detalles en `docs/firmware/ota.md`. Lo esencial: la firma cubre la app, no el
bootloader; y el rollback no usa temporizador, se decide en el arranque
siguiente.

## Robustez

| Mecanismo | Valor |
|---|---|
| Watchdog de tareas | 10 s, con reinicio (`TASK_WDT_PANIC`) |
| Reintento de conexion | espera creciente 1 s -> 60 s con jitter |
| Nodos perdidos | `NODE_LOST` tras 3 heartbeats de 600 s |
| Testamento MQTT | publica `HUB_OFFLINE` si se cae |

**Limitacion conocida:** el watchdog vigila las tareas idle. En un chip de dos
nucleos, una tarea colgada en uno solo no se detecta, porque el idle del otro
sigue alimentando el contador. Cerrarlo exige registrar tareas concretas con
`esp_task_wdt_add`.

## Diagnostico rapido

| Sintoma | Donde mirar |
|---|---|
| No arranca la consola | usar el USB nativo, no un adaptador |
| Los nodos no aparecen | canal del router distinto del 5 |
| Un nodo deja de responder | distancia, paredes, o el nodo perdio el pairing |
| No publica en la nube | credenciales MQTT o el tunel de Cloudflare |
| Se reinicia solo | mirar `reset_reason` en `HUB_ONLINE` |

## Que NO tiene

Conviene tenerlo claro para no buscarlo:

- No guarda los eventos en local: si se cae la nube, se pierden.
- No tiene bateria ni respaldo de energia.
- La firma no cubre el bootloader: con acceso fisico se puede reemplazar.
