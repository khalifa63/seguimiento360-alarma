# CAPÍTULO 2: ESTADO DEL ARTE

## 2.1 Sistemas de Alarma Convencionales

Los sistemas de alarma convencionales se basan en un panel de control cableado que supervisa zonas mediante contactos magnéticos, sensores infrarrojos pasivos (PIR) y sirenas, y se comunica con una central receptora de alarmas (CRA) a través de la red telefónica conmutada o comunicadores GSM propietarios. Su modelo de negocio combina costo de instalación, cuotas mensuales y contratos de permanencia.

Si bien ofrecen alta confiabilidad y certificaciones de grado, presentan limitaciones relevantes para el mercado objetivo de este proyecto: elevado costo total, dependencia de infraestructura fija, ausencia de integración nativa con rastreo vehicular y encadenamiento a proveedores por protocolos propietarios.

## 2.2 Sistemas de Alarma IoT

Con la masificación de los teléfonos inteligentes surgieron plataformas como Tuya Smart, Ring, SimpliSafe y AJAX Systems, que emplean sensores inalámbricos (WiFi, Zigbee o protocolos propietarios), aplicaciones móviles y notificaciones push. Se distinguen dos modelos: (i) hardware más suscripción por monitoreo profesional (Ring, SimpliSafe, ADT); y (ii) compra de hardware sin suscripción (AJAX, Tuya).

Las soluciones económicas dependen de nubes ubicadas fuera de la región, con cuestionamientos de privacidad y soporte limitado; las premium tienen alto costo inicial y requieren instaladores certificados. Esta brecha deja un espacio para una solución intermedia: hardware asequible, servidor local, sin suscripción obligatoria y soporte técnico regional, que es precisamente el posicionamiento de este proyecto.

## 2.3 Tecnologías de Comunicación Inalámbrica

### 2.3.1 WiFi (IEEE 802.11)

WiFi opera en la banda de 2,4 GHz con infraestructura de punto de acceso y velocidades del orden de Mbps. Su consumo energético es alto respecto a otras alternativas (picos de cientos de mA durante transmisión), por lo que resulta adecuado para nodos con alimentación continua. En este proyecto se reserva para el hub central, que mantiene conexión permanente con el servidor.

### 2.3.2 ESP-NOW

ESP-NOW es un protocolo propietario de Espressif que opera en la capa de enlace (MAC) sin necesidad de conexión ni punto de acceso. Ofrece latencia del orden de milisegundos, tramas con carga útil de hasta 250 bytes, cifrado opcional basado en claves PMK/LMK, y soporte de hasta 20 pares (peers) por defecto, de los cuales hasta 6 pueden estar cifrados. Puede coexistir con el modo estación (STA) de WiFi en el mismo radio, lo que permite que un dispositivo reciba datos por ESP-NOW y los retransmita por WiFi/MQTT. Su bajo consumo lo hace ideal para nodos que despiertan, transmiten y vuelven a sueño profundo. Estas características lo convierten en la tecnología elegida para el enlace sensor-hub dentro del hogar.

### 2.3.3 LoRa / LoRaWAN

LoRa ofrece alcances de varios kilómetros con tasas de datos muy bajas (kbps) y requiere gateways y servidores de red. Es apropiado para escenarios rurales o agrícolas; en un entorno residencial resulta excesivo en alcance, insuficiente en tasa de datos y agrega costo de infraestructura. Se descarta para la Fase 1 y se reserva como opción de expansión futura.

### 2.3.4 4G/LTE

La red celular brinda cobertura amplia con latencias de decenas de milisegundos. Módulos como el SIMCom A7670SA integran GNSS y LTE en un solo componente, con soporte de pilas TCP/IP y MQTT. El costo asociado al plan de datos se justifica en vehículos, donde no existe infraestructura fija disponible. Es la tecnología elegida para el rastreador vehicular.

### 2.3.5 Comparativa

| Tecnología | Alcance | Tasa de datos | Consumo | Costo | Uso en el proyecto |
|---|---|---|---|---|---|
| WiFi | 30-100 m | Mbps | Alto | Bajo | Hub a nube |
| ESP-NOW | 50-200 m campo abierto | 250 B/trama, baja latencia | Muy bajo | Muy bajo | Sensor a hub |
| LoRa | 2-15 km | kbps | Bajo | Medio | Descartado en Fase 1 |
| 4G/LTE | Cobertura celular | Mbps | Alto | Medio (SIM) | Vehículo a nube |

## 2.4 Protocolos de Mensajería IoT

### 2.4.1 MQTT

MQTT es un estándar OASIS basado en el modelo publicar/suscribir con un intermediario (broker). Posee cabecera mínima de 2 bytes, tres niveles de calidad de servicio (QoS 0, 1 y 2), y mecanismos diseñados para conexiones inestables: testamento (Last Will and Testament), mensajes retenidos y sesiones persistentes. Puede transportarse sobre TCP o WebSockets con cifrado TLS. En el proyecto se emplea el broker Eclipse Mosquitto, ya operativo en la infraestructura Seguimiento 360, y MQTT sobre WebSockets seguros (WSS, puerto 443) a través de Cloudflare Tunnel, lo que permite a los dispositivos conectarse sin apertura de puertos en el firewall.

### 2.4.2 CoAP

CoAP sigue un modelo REST sobre UDP con seguridad DTLS, orientado a dispositivos muy restringidos. Aunque eficiente, exige infraestructura y herramientas más complejas en el servidor y cuenta con menor ecosistema para dashboards y automatización, por lo que no se adopta.

### 2.4.3 HTTP y WebSockets

HTTP bajo el modelo petición/respuesta genera sobrecarga significativa para telemetría frecuente. WebSockets, en cambio, ofrece un canal full-duplex permanente y se utiliza en este proyecto únicamente como transporte de MQTT a través de Cloudflare, combinando lo mejor de ambos mundos.

### 2.4.4 Comparativa y selección

| Criterio | MQTT | CoAP | HTTP |
|---|---|---|---|
| Modelo | Pub/Sub | REST/UDP | Petición/respuesta |
| Sobrecarga | Muy baja | Baja | Alta |
| QoS | 0/1/2 | Confirmable | N/A |
| Conexiones inestables | LWT, sesiones | Parcial | No |
| Ecosistema servidor | Amplio (Mosquitto) | Medio | Amplio |

Se selecciona MQTT por su idoneidad para telemetría continua, su soporte de desconexiones, y su integración nativa con Node-RED, InfluxDB y Traccar en la infraestructura existente.

## 2.5 Plataformas de Microcontroladores

### 2.5.1 ESP32-S3

Incorpora doble núcleo Xtensa LX7 a 240 MHz, WiFi y Bluetooth 5 (LE), aceleración de operaciones vectoriales, amplio número de GPIO y soporte de PSRAM externa. Su potencia lo hace adecuado para el hub coordinador (que ejecuta WiFi, ESP-NOW, BLE y MQTT simultáneamente) y para el rastreador vehicular (que gestiona UART con el módulo 4G y GNSS).

### 2.5.2 ESP32-C3

Basado en RISC-V de un núcleo a 160 MHz, con WiFi y Bluetooth 5 (LE), se distingue por su bajo costo y bajo consumo en sueño profundo. Es la plataforma elegida para los nodos sensores, donde el presupuesto energético y económico es crítico.

### 2.5.3 Justificación frente a alternativas

Frente a alternativas como STM32 con radio externo (mayor costo de integración), Raspberry Pi (mayor consumo y costo) o Arduino (limitaciones de memoria y conectividad), la familia ESP32 ofrece el mejor equilibrio entre costo, consumo, conectividad integrada y madurez del entorno de desarrollo ESP-IDF con FreeRTOS, razón por la cual es el MCU más utilizado en IoT comercial.

## 2.6 Plataformas de Rastreo Vehicular

### 2.6.1 Traccar

Traccar es una plataforma de geolocalización de código abierto que soporta más de un centenar de protocolos de dispositivos GPS, incluido el protocolo OsmAnd sobre HTTP (puerto 5055), utilizado en este proyecto para la ingesta de posiciones. Ofrece interfaz web, API REST, geovallas, gestión de eventos y notificaciones, y bases de datos H2, MySQL o PostgreSQL. Ya se encuentra operativo en la infraestructura Seguimiento 360.

### 2.6.2 Alternativas

OpenGTS, pionero del código abierto, se encuentra con desarrollo limitado; GPSGate y plataformas comerciales propietarias implican licencias y cuotas mensuales por vehículo, incompatibles con el modelo sin suscripción del proyecto.

### 2.6.3 Selección

Se ratifica Traccar por su licencia abierta, su catálogo de protocolos, su API integrable con Node-RED y su presencia ya validada en la infraestructura existente.

## 2.7 Análisis de Trabajos Relacionados

Se revisaron proyectos académicos y comunitarios relevantes: sistemas de alarma residencial con ESP32, sensores PIR y de humo con notificaciones por Telegram; sistemas SCADA IoT de bajo costo basados ESP32 publicados en revistas científicas; y kits domóticos comunitarios con NodeMCU. Estos trabajos validan la viabilidad técnica del enfoque, pero comparten limitaciones: son monoinquilino, carecen de actualizaciones OTA, no implementan resiliencia local ante caídas de internet, no integran monitoreo vehicular y no contemplan seguridad de extremo a extremo ni escalabilidad comercial.

El presente proyecto se diferencia por: arquitectura multi-inquilino con panel de super-administración, hub con lógica autónoma offline, integración nativa hogar-vehículo, seguridad MQTT con ACL, OTA firmado, y un modelo de negocio sin suscripción obligatoria apoyado en una infraestructura de servidor ya operativa.

## 2.8 Resumen del Capítulo

El análisis del estado del arte muestra que la tecnología necesaria (ESP32, ESP-NOW, MQTT, Traccar) es madura y accesible, que los modelos comerciales existentes dejan un espacio desatendido en Latinoamérica, y que los trabajos académicos previos no abordan la escalabilidad multi-inquilino ni la integración hogar-vehículo. Estos hallazgos justifican las decisiones tecnológicas adoptadas y se detallan en los capítulos siguientes.
