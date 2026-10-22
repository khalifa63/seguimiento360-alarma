# CAPÍTULO 6: DISEÑO DE SOFTWARE

## 6.1 Firmware de Dispositivos

### 6.1.1 Entorno y arquitectura general

El firmware se desarrolla con ESP-IDF v5.x sobre FreeRTOS, usando un modelo de tareas especializadas que se comunican por colas y grupos de eventos. En el hub ESP32-S3 conviven las tareas: recepcion ESP-NOW, maquina de estados de alarma, cliente MQTT, gestion WiFi, provisioning, OTA y housekeeping (watchdog, heartbeats, persistencia y store-and-forward). En los nodos sensores ESP32-C3 el diseño es minimalista: despertar por interrupcion, leer sensor, transmitir trama con reintentos y retornar a sueno profundo. La configuracion (identidad del inquilino, credenciales MQTT, WiFi, estado de la alarma) se persiste en NVS para sobrevivir reinicios y cortes de energia.

### 6.1.2 Maquina de estados finitos de la alarma

La logica central del hub se modela como una maquina de estados finitos con los siguientes estados y transiciones:

    [DESARMADO]        --armar_total-->        [ARMADO_TOTAL]
    [DESARMADO]        --armar_parcial-->      [ARMADO_PARCIAL]
    [ARMADO_TOTAL]     --sensor_critico-->     [DISPARADA]
    [ARMADO_PARCIAL]   --sensor_perimetral-->  [DISPARADA]
    [DISPARADA]        --activar_sirena+notif--> [ESPERA_ACK]
    [ESPERA_ACK]       --ack_usuario/timeout--> [DESARMADO]
    [ARMADO_TOTAL/PARCIAL] --desarmar-->       [DESARMADO]
    [cualquier estado] --fallo_sensor-->       [MODO_DEGRADADO]

En ARMADO_TOTAL cualquier sensor critico dispara la alarma; en ARMADO_PARCIAL (modo en casa) solo los sensores perimetrales disparan, permitiendo movimiento interior. En DISPARADA el hub activa la sirena local de inmediato (autonomia sin internet) y publica el evento a la nube. ESPERA_ACK mantiene la sirena hasta reconocimiento del usuario desde la PWA o un tiempo limite configurable. MODO_DEGRADADO se alcanza cuando un sensor deja de reportar heartbeats: el sistema continua armado con los sensores restantes y genera una advertencia. Cada transicion queda registrada en el log local y en el servidor.

### 6.1.3 Protocolo ESP-NOW

Se define un protocolo binario compacto y versionado para el enlace radio, en lugar de JSON, por eficiencia y determinismo:

    Byte 0      MAGIC (0xA5)
    Byte 1      VERSION (0x01)
    Byte 2      TYPE (HELLO, EVENT, HEARTBEAT, CMD, ACK)
    Byte 3      SEQ (deduplicacion)
    Bytes 4-9   MAC origen
    Bytes 10-N  PAYLOAD segun tipo
    Byte N+1    CRC8

Tipos de mensaje: HELLO para solicitud de union de un nodo nuevo; EVENT para eventos de sensor (apertura, movimiento, humo, agua, panico) con nivel de bateria y RSSI; HEARTBEAT periodico configurable; CMD para comandos del hub (activar sirena, cambiar parametros); y ACK de confirmacion. La entrega confiable se logra con reintentos y backoff exponencial hasta recibir ACK, y deduplicacion por SEQ en el receptor. La confidencialidad del enlace se garantiza con el cifrado nativo de ESP-NOW mediante claves PMK/LMK, donde la LMK se deriva de un secreto por inquilino entregado en el provisioning, de modo que los nodos de un cliente no pueden ser leidos por equipos de otro cliente.

### 6.1.4 Cliente MQTT

El hub y el rastreador vehicular se conectan al broker mediante MQTT sobre WebSockets con TLS (WSS, puerto 443) hacia mqtt.seguimiento360.com, aprovechando la terminacion TLS de Cloudflare sin puertos abiertos. Cada dispositivo autentica con credenciales unicas (usuario device_<serial> y contrasena aleatoria) generadas en el backend durante la vinculacion. Se usa QoS 1 para eventos de alarma y QoS 0 para telemetria periodica. Se configura testamento (Last Will and Testament) en el topico de estado para que el broker publique offline ante una desconexion abrupta. Los payloads son JSON en Fase 1, con campos comunes:

    {
      "event_type": "ALERT",
      "sensor_type": "MAGNETIC",
      "state": "OPEN",
      "severity": "CRITICAL",
      "battery": 87,
      "rssi": -45,
      "ts": 1738742400
    }

El modulo de store-and-forward mantiene un buffer circular en NVS con los eventos no entregados durante caidas de conexion y los reenvia en orden al restablecerse el enlace, garantizando que ninguna alarma se pierca.

### 6.1.5 Provisioning

El dispositivo nuevo arranca en modo fabrica y expone un canal de configuracion temporal (BLE con protocolo protocomm de ESP-IDF, con alternativa SoftAP). Desde la PWA o la herramienta del instalador se envian: credenciales WiFi del sitio y codigo de activacion de un solo uso. El dispositivo consulta al backend el endpoint de provision con su numero de serie y el codigo; el backend valida que la serie exista, este disponible y el codigo sea valido, y responde con tenant_id, sitio asignado y credenciales MQTT. El dispositivo persiste todo en NVS, reinicia en modo normal y reporta su primer heartbeat. El mismo flujo permite revincular un dispositivo liberado por otro cliente.

### 6.1.6 Actualizaciones OTA

El firmware usa tabla de particiones con dos ranuras de aplicacion (ota_0 y ota_1) mas NVS. El dispositivo consulta periodicamente un manifiesto JSON publicado en el servidor OTA con su tipo de dispositivo y version actual; si existe una version superior, descarga el binario por HTTPS, lo escribe en la ranura inactiva, verifica hash y firma criptografica (firmware firmado con clave del operador), marca la nueva ranura como booteable y reinicia. Se habilita el mecanismo de rollback del bootloader: si la nueva version no confirma arranque saludable en un numero limitado de intentos, se revierte automaticamente a la version anterior. Las actualizaciones se programan con jitter aleatorio para evitar descargas masivas simultaneas.

## 6.2 Backend (Node-RED)

### 6.2.1 API REST

Node-RED expone la API REST consumida por la PWA mediante nodos http-in/http-response, con middleware de autenticacion JWT implementado en nodos funcion: todo pedido valido porta un token firmado que incluye user_id, tenant_id y rol, con expiracion de 24 horas y renovacion. Los grupos de endpoints son: autenticacion (registro, login, refresh, perfil); inquilinos y sitios (CRUD); dispositivos (listado, vinculacion por serie o QR, desvinculacion, configuracion); alarmas (armar, desarmar, historial); y administracion global (metricas, tenants, OTA), restringido por rol al super-administrador. El acceso a datos se filtra siempre por el tenant del token, garantizando aislamiento entre clientes.

### 6.2.2 Motor de reglas de alarma

El flujo principal suscribe al arbol de tópicos seguimiento360/#, decodifica los payloads y consulta el estado de armado del hub correspondiente. Si el estado es armado y la severidad es critica, el motor: publica el comando de sirena al hub, persiste el evento en InfluxDB y despacha notificaciones segun la politica del inquilino. Si el sistema esta desarmado, el evento solo se persiste y se muestra como informativo. El motor incluye el watchdog de heartbeats: mantiene la ultima marca temporal por dispositivo y, superado el umbral configurable desde el panel, genera advertencia de dispositivo caido, util para detectar bateria agotada o sabotaje.

### 6.2.3 Sistema de notificaciones

Las notificaciones se despachan por severidad: informativas (verde) solo a la PWA; advertencias (amarillo) a Gotify; criticas (rojo) a Gotify y Telegram de forma simultanea, con opcion futura de SMS, llamada o integracion con centrales de monitoreo externas. Gotify se integra por su API HTTP con tokens por cliente, y Telegram mediante el bot existente de la plataforma.

### 6.2.4 Integracion con Traccar

El rastreador vehicular envia posiciones al protocolo Osmand de Traccar en el puerto 5055 y publica telemetria por MQTT. Node-RED consume los eventos de Traccar mediante su API para evaluar geovallas y generar reglas cruzadas hogar-vehiculo, por ejemplo desarmar automaticamente la alarma del sitio cuando un vehiculo autorizado ingresa a su geovalla.

## 6.3 Frontend (PWA)

### 6.3.1 Arquitectura de la aplicacion

La PWA se construye con React, Vite y Tailwind CSS, estado global con Zustand y enrutado con React Router. Incluye manifest y service worker para instalacion en el movil y cache basico offline. Se aloja en el servidor Nginx existente bajo el dominio principal. La comunicacion con el backend es REST mas un canal WebSocket hacia Node-RED para eventos en tiempo real, evitando exponer credenciales MQTT en el navegador.

### 6.3.2 Flujo de autenticacion

El cliente se registra con email, contrasena (almacenada como hash bcrypt), nombre y telefono. El backend crea el usuario y su inquilino inicial. El login emite el JWT que la PWA almacena de forma segura y renueva automaticamente. Las rutas quedan protegidas por rol: cliente final ve solo su inquilino; el super-administrador accede al panel global.

### 6.3.3 Gestion de dispositivos

El cliente visualiza sus sitios y dispositivos, vincula nuevos equipos ingresando el numero de serie o escaneando el QR de la caja, y puede desvincularlos para liberarlos. Por cada dispositivo muestra estado en linea o fuera de linea, nivel de bateria, RSSI y ultima actividad, y permite acciones como armar, desarmar, probar sirena y configurar umbrales y horarios.

### 6.3.4 Visualizacion de alarmas

El panel principal presenta el estado de armado del sitio, botones de armado total, parcial y desarme con confirmacion, boton de panico, y linea de tiempo de eventos con severidad codificada por color. El historial permite filtrar por sitio, dispositivo y rango de fechas. El panel de super-administracion complementa con vista global de inquilinos, salud de la flota de dispositivos y gestion de versiones de firmware.

## 6.4 Bases de Datos

### 6.4.1 Esquema PostgreSQL (datos relacionales)

Tablas principales: users (id, email unico, password_hash, nombre, telefono, rol, activo); tenants (id, nombre, owner_user_id); sites (id, tenant_id, nombre, direccion, latitud, longitud); devices (id, serial_number unico, device_type, firmware_version, tenant_id nullable, site_id, status disponible/vinculado/inactivo, marcas de tiempo de vinculacion y liberacion); device_credentials (id, device_id, mqtt_user, mqtt_hash); y device_audit_log (id, device_id, tenant_id, accion bind/unbind, ejecutado_por, timestamp). Indices sobre serial, tenant y status garantizan consultas rapidas. La desvinculacion deja el dispositivo en status disponible para otro cliente, conservando la auditoria.

### 6.4.2 Esquema InfluxDB (series temporales)

Buckets: telemetria (posiciones GPS, velocidad, temperatura, bateria; retencion 90 dias), alarmas (eventos de intrusion, humo, impacto; retencion 1 ano), health (heartbeats, uptime, RSSI; retencion 30 dias) y system_logs (errores de firmware y backend; retencion 14 dias). Tags principales: tenant_id, site_id, device_id y device_type, lo que habilita filtrado multi-inquilino directo en Grafana y en las consultas del backend.

### 6.4.3 Politicas de retencion y crecimiento

Las retenciones definidas equilibran utilidad operativa y costo de almacenamiento. El crecimiento se controla con heartbeats de baja frecuencia y, en Fase 2, con tareas de downsampling que agregan datos antiguos a resoluciones menores. Los eventos de alarma nunca se agregan: se conservan integros por un ano para soporte a investigaciones y reportes.

## 6.5 Resumen del Capitulo

El diseño de software define un firmware por tareas sobre ESP-IDF y FreeRTOS con maquina de estados autonoma en el hub, protocolo ESP-NOW binario versionado y cifrado por inquilino, cliente MQTT resiliente con store-and-forward y OTA firmado con rollback, un backend Node-RED que expone API REST autenticada con JWT y motor de reglas con watchdog de heartbeats, una PWA de autoservicio instalable, y un modelo de datos dual PostgreSQL-InfluxDB con aislamiento multi-inquilino y retenciones diferenciadas.
