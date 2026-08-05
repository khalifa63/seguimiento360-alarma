# CAPÍTULO 1: INTRODUCCIÓN

## 1.1 Planteamiento del Problema

En el contexto latinoamericano, la inseguridad patrimonial constituye una de las principales preocupaciones de hogares y pequeñas empresas. Los robos a viviendas y el hurto de vehículos y flotas generan pérdidas económicas significativas y una demanda creciente de soluciones de seguridad y monitoreo.

Los sistemas tradicionales presentan limitaciones estructurales: (i) los servicios de alarma monitoreada exigen cuotas mensuales elevadas y contratos de permanencia; (ii) el monitoreo residencial y el rastreo vehicular se comercializan como productos separados, sin integración operativa; (iii) las soluciones de bajo costo basadas en plataformas IoT foráneas dependen de servidores en el extranjero, lo que introduce riesgos de latencia, disponibilidad y privacidad de los datos; y (iv) el soporte técnico local es limitado o inexistente.

En consecuencia, se identifica la siguiente pregunta de investigación y desarrollo: ¿cómo diseñar e implementar una infraestructura integrada de alarma y monitoreo para hogar y vehículos, escalable a múltiples clientes, con soporte local, soberanía de datos y sin suscripciones obligatorias?

## 1.2 Justificación

El proyecto se justifica en cuatro dimensiones:

- Tecnológica: la madurez del ecosistema ESP32, los protocolos MQTT y ESP-NOW, y las plataformas abiertas de geolocalización (Traccar) permiten construir sistemas de grado comercial a costos de hardware reducidos.
- Económica: el mercado global de seguridad residencial inteligente se valoró en USD 33.200 millones en 2025, el mercado de seguridad vehicular en USD 12.000 millones en 2024 con crecimiento anual del 7,1 %, y el mercado de monitoreo de alarmas supera los USD 69.000 millones en 2026. En Latinoamérica, tan solo el mercado mexicano de seguridad electrónica alcanzó USD 1.034 millones en 2025.
- Operativa: el proyecto se apoya en una infraestructura de servidor preexistente (plataforma Seguimiento 360) que ya opera servicios MQTT, geolocalización, automatización, bases de datos temporales y notificaciones, lo que reduce costos y tiempos de desarrollo.
- Social: ofrece a pequeñas empresas y consumidores finales una alternativa accesible, sin cuotas obligatorias y con datos alojados en servidores locales, fortaleciendo la privacidad y el control sobre su propia información.

## 1.3 Objetivos

### 1.3.1 Objetivo General

Diseñar e implementar una infraestructura integral de alarma y monitoreo para hogares y vehículos, basada en el ecosistema ESP32 y en la plataforma Seguimiento 360, que integre sensores inalámbricos de bajo consumo, un hub coordinador con lógica autónoma, rastreo vehicular 4G y una aplicación web progresiva, escalable a múltiples clientes y sin suscripciones obligatorias en su fase inicial.

### 1.3.2 Objetivos Específicos

1. Diseñar una arquitectura multi-inquilino (multi-tenant) que permita administrar múltiples clientes, sitios y dispositivos con aislamiento de datos y un panel de super-administración.
2. Desarrollar el firmware del hub central basado en ESP32-S3, con coordinación de nodos vía ESP-NOW, cliente MQTT, máquina de estados finitos para la alarma y operación autónoma ante caídas de internet.
3. Desarrollar nodos sensores de bajo consumo (apertura magnética, movimiento, humo/gas, temperatura/humedad, inundación y botón de pánico) sobre la familia ESP32-C3.
4. Desarrollar un rastreador vehicular basado en ESP32-S3 y módulo A7670SA-FASE (GNSS + 4G LTE) con telemetría de ubicación, encendido, impacto y datos OBD-II.
5. Implementar una aplicación web progresiva (PWA) de autoservicio para registro de clientes, vinculación de dispositivos, control de la alarma y visualización de eventos.
6. Implementar mecanismos de seguridad de extremo a extremo: autenticación MQTT con listas de control de acceso, autenticación de usuarios con JWT, y actualizaciones de firmware OTA firmadas.
7. Establecer procedimientos de copias de seguridad, monitoreo y recuperación ante desastres para el servidor, el código y la documentación del proyecto.
8. Validar el sistema con un grupo inicial de clientes beta y definir el plan de negocio para su escalamiento comercial.

## 1.4 Alcance y Limitaciones

### Alcance (Fase 1)

- Hub central ESP32-S3 sin pantalla, con salida opcional para sirena externa.
- Seis tipos de nodos sensores/actuadores para hogar y un rastreador vehicular.
- Comunicación ESP-NOW en el hogar y 4G/LTE directa al servidor en vehículos.
- Plataforma en la nube sobre la infraestructura existente (MQTT, Node-RED, InfluxDB, Grafana, Traccar, Gotify, Cloudflare Tunnel).
- PWA responsive para cliente final y panel de super-administración.
- Modelo comercial de venta de hardware sin suscripción obligatoria.

### Limitaciones

- No se incluyen cámaras ni análisis de video en la Fase 1 (se difieren a Fase 2).
- Se emplean módulos comerciales, no componentes de grado industrial ni certificaciones de seguridad (UL/CE/Grado 2-3).
- El funcionamiento del hogar depende de la disponibilidad de red WiFi y energía eléctrica del cliente, mitigado con lógica local autónoma en el hub.
- El rastreo vehicular está condicionado a la cobertura de la red celular del operador.
- El módulo vehicular se implementa inicialmente con ESP32 + A7670SA-FASE por UART; la alternativa OpenCPU se evaluará durante su desarrollo.

## 1.5 Estructura del Documento

El presente documento se organiza en diez capítulos. El Capítulo 2 revisa el estado del arte en tecnologías de comunicación inalámbrica, protocolos IoT y plataformas de rastreo. El Capítulo 3 analiza el mercado y la competencia. El Capítulo 4 describe la arquitectura general del sistema. Los Capítulos 5 y 6 detallan el diseño de hardware y software respectivamente. El Capítulo 7 aborda la seguridad. El Capítulo 8 documenta el despliegue, la operación y las estrategias de copia de seguridad y continuidad. El Capítulo 9 presenta el plan de negocio, y el Capítulo 10 concluye con los resultados y el trabajo futuro.

## 1.6 Metodología de Trabajo

El proyecto sigue un enfoque iterativo e incremental: (i) definición y refinamiento de requerimientos mediante sesiones de análisis; (ii) diseño documental (marco teórico) como base de las decisiones técnicas; (iii) prototipado de hardware y firmware por módulos; (iv) integración continua con control de versiones Git y respaldos 3-2-1; (v) validación con clientes beta; y (vi) documentación formal de cada fase. El desarrollo del proyecto se apoya en herramientas de inteligencia artificial para acelerar el diseño, mientras que la lógica de producción se implementa en Node-RED.
