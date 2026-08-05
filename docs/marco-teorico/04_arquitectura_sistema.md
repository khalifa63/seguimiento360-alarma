# CAPÍTULO 4: ARQUITECTURA DEL SISTEMA

## 4.1 Visión General

El sistema sigue un modelo de cuatro capas: percepción, borde, nube y aplicación. Esta separación permite que cada capa evolucione de forma independiente, que el sistema continúe operando ante caídas parciales de conectividad, y que la plataforma escale a múltiples clientes (multi-inquilino) sin rediseñar la arquitectura.

+----------------+ +----------------+ +-------------------+ +----------------+
| PERCEPCION | | BORDE | | NUBE | | APLICACION |
| | | | | | | |
| Sensores | | Hub ESP32-S3 | | Cloudflare Tunnel | | PWA cliente |
| ESP32-C3 |---->| - ESP-NOW |---->| Mosquitto (WSS) |---->| Dashboard |
| (magnetico, | ESP | - FSM alarma | WiFi| Node-RED (API) | MQTT| super-admin |
| PIR, humo, | NOW | - MQTT/WSS | WSS | InfluxDB/Postgres | HTTP| Grafana |
| inundacion) | | - Sirena loc. | 443 | Traccar / Gotify | | App movil |
| | | | | | | |
| Tracker vehic. |-----|----------------|---->| (conexion 4G directa al servidor) |
| ESP32-S3+A7670 | | | | | | |
+----------------+ +----------------+ +-------------------+ +----------------+

## 4.2 Capa de Percepción (Dispositivos)

Comprende los nodos que interactúan con el mundo físico. En el hogar, nodos basados en ESP32-C3 con un solo rol cada uno: contacto magnético de puertas y ventanas, sensor de movimiento PIR, sensor de humo y gas, sensor de temperatura y humedad, sensor de inundación, botón de pánico y actuador de sirena. Cada nodo opera con política de mínimo consumo: despierta por interrupción o por temporizador, transmite su estado por ESP-NOW y retorna a sueño profundo.

En el segmento vehicular, el nodo de percepción es el rastreador compuesto por un ESP32-S3 y un módulo SIMCom A7670SA-FASE (GNSS más LTE) comunicados por UART. Captura posición, estado de encendido, impacto y vibración, y datos OBD-II, y se conecta directamente a la nube mediante 4G, sin pasar por un hub.

## 4.3 Capa de Borde (Hub / Gateway)

El hub central, basado en ESP32-S3, es el coordinador del sitio. Sus responsabilidades son: (i) recibir tramas ESP-NOW de los nodos del hogar; (ii) ejecutar la máquina de estados finitos de la alarma con lógica autónoma, de modo que la sirena local opere aunque caiga internet; (iii) mantener conexión WiFi permanente y publicar eventos al servidor mediante MQTT sobre WebSockets seguros; (iv) almacenar temporalmente eventos no entregados y reenviarlos al restablecerse la conexión (store-and-forward); (v) exponer provisioning inicial vía BLE o punto de acceso temporal; y (vi) controlar la salida de sirena externa opcional.

La lógica local autónoma es una decisión crítica de arquitectura: la alarma no debe depender de la disponibilidad de la nube para proteger el sitio.

## 4.4 Capa de Nube (Servidor)

Se apoya en la infraestructura existente de la plataforma Seguimiento 360, desplegada con Docker y expuesta mediante Cloudflare Tunnel sin apertura de puertos en el firewall. Los servicios que intervienen son:

| Servicio | Función | Acceso público |
|---|---|---|
| Mosquitto | Broker MQTT (TCP 1883 y WebSockets 8083) | mqtt.seguimiento360.com (WSS 443) |
| Node-RED | Lógica de negocio, motor de reglas y API REST | nodered.seguimiento360.com |
| InfluxDB | Base de datos de series temporales (telemetría) | Interno |
| PostgreSQL | Base de datos relacional (usuarios, tenants, dispositivos) | Interno |
| Traccar | Plataforma de geolocalización (protocolo Osmand, puerto 5055) | gps.seguimiento360.com |
| Grafana | Dashboards y analítica | grafana.seguimiento360.com |
| Gotify | Notificaciones push | gotify.seguimiento360.com |
| Nginx | Proxy inverso y alojamiento de la PWA | seguimiento360.com |
| Cloudflared | Túnel seguro con terminación TLS en el borde | Edge 443 |
| Uptime-Kuma | Monitoreo de disponibilidad de servicios | Interno |

Cloudflare Tunnel realiza la terminación TLS en el borde, de modo que los dispositivos y usuarios siempre se comunican por HTTPS/WSS sobre el puerto 443, mientras los servicios internos operan por HTTP dentro de la red privada. Esto elimina la necesidad de abrir puertos y protege frente a escaneos externos.

## 4.5 Capa de Aplicación

Comprende dos interfaces web: la aplicación web progresiva (PWA) orientada al cliente final, con registro de cuenta, vinculación y gestión de dispositivos, armado y desarmado de la alarma, visualización de eventos en tiempo real e historial; y el panel de super-administración, orientado al operador de la plataforma, con visibilidad global de todos los inquilinos, estado de dispositivos, gestión de firmware OTA y métricas de operación. Ambas se alojan en el servidor y se acceden desde navegador de escritorio o móvil, instalables como aplicación gracias a las capacidades PWA.

## 4.6 Modelo Multi-inquilino y Jerarquía de Tópicos

El sistema organiza la información en una jerarquía de cuatro niveles: inquilino (cliente), sitio (propiedad), hub y dispositivo. Los vehículos se asocian directamente al inquilino. Esta jerarquía se refleja en el esquema de tópicos MQTT:

seguimiento360/tenant/{tenant_id}/site/{site_id}/hub/{hub_id}/status
seguimiento360/tenant/{tenant_id}/site/{site_id}/hub/{hub_id}/command
seguimiento360/tenant/{tenant_id}/site/{site_id}/sensor/{sensor_id}/alert
seguimiento360/tenant/{tenant_id}/vehicle/{vehicle_id}/gps
seguimiento360/admin/ota/manifest

El aislamiento de datos entre clientes se garantiza mediante autenticación MQTT con credenciales únicas por dispositivo y listas de control de acceso (ACL) que restringen cada dispositivo a los tópicos de su propio inquilino. El super-administrador dispone de permisos sobre el árbol completo bajo seguimiento360/admin.

## 4.7 Flujo de Datos End-to-End

Caso de alarma residencial: (1) el contacto magnético detecta apertura y despierta de sueño profundo; (2) transmite una trama ESP-NOW al hub con reintentos; (3) el hub evalúa la máquina de estados y, si el sistema está armado, activa la sirena local de inmediato; (4) publica el evento de alerta por MQTT sobre WSS a través de Cloudflare; (5) Node-RED recibe el evento, lo persiste en InfluxDB y aplica las reglas de notificación; (6) se despachan notificaciones push por Gotify y Telegram según la severidad; (7) la PWA del cliente muestra el evento en tiempo real.

Caso vehicular: (1) el rastreador obtiene posición GNSS y telemetría; (2) publica por MQTT vía 4G y envía la posición a Traccar mediante el protocolo Osmand; (3) Traccar y Node-RED evalúan geovallas y eventos; (4) se generan notificaciones y se actualizan los dashboards.

## 4.8 Resiliencia y Autonomía Local

La arquitectura contempla cuatro mecanismos de resiliencia: (i) lógica de alarma autónoma en el hub, que opera sin internet; (ii) store-and-forward de eventos en memoria no volátil del hub ante caídas de conexión; (iii) testamento MQTT (Last Will and Testament) y heartbeats configurables para detectar dispositivos y hubs caídos o saboteados; y (iv) monitoreo de disponibilidad de servicios con Uptime-Kuma y alertas al operador.

## 4.9 Decisiones de Diseño y Justificación

| Decisión | Justificación |
|---|---|
| ESP-NOW para sensores del hogar | Sin punto de acceso, baja latencia, mínimo consumo y costo nulo por nodo |
| Hub con lógica autónoma | La protección del sitio no depende de la nube |
| MQTT sobre WSS por Cloudflare | Sin puertos abiertos, TLS en el borde, funciona tras NAT |
| InfluxDB para telemetría y PostgreSQL para relaciones | Cada base según su fortaleza: series temporales vs relacional |
| Node-RED como backend | Ya operativo, integración nativa con MQTT, HTTP y bases de datos |
| Multi-tenant con ACL por dispositivo | Aislamiento estricto de datos entre clientes |
| JSON como formato de payloads en Fase 1 | Legibilidad y depuración; se evaluará CBOR o Protobuf al escalar |
| Traccar para geolocalización | Código abierto, amplio catálogo de protocolos, ya operativo |

## 4.10 Resumen del Capítulo

La arquitectura de cuatro capas con borde inteligente garantiza operación autónoma ante fallas de conectividad, aprovecha una infraestructura de nube ya operativa para reducir costos y tiempos, y establece un modelo multi-inquilino con aislamiento estricto que habilita el escalamiento comercial. Los flujos de datos end-to-end quedan definidos para los casos residencial y vehicular, y las decisiones tecnológicas quedan justificadas frente a alternativas.
