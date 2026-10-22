# Visión y alcance

## Qué es Seguimiento 360

Plataforma de alarma y monitoreo IoT para hogar y vehículos. Combina nodos sensores de bajo consumo, un hub ESP32-S3 que actúa como gateway, y un backend en la nube que centraliza usuarios, dispositivos, telemetría y notificaciones.

## Problema que resuelve

Las alarmas tradicionales dependen de una central y de cableado. Seguimiento 360 busca:

- Instalación inalámbrica con nodos a batería.
- Latencia local baja entre sensores y hub (ESP-NOW).
- Conectividad a la nube sin abrir puertos en el router (Cloudflare Tunnel + MQTT over WSS en 443).
- Gestión multi-tenant desde una PWA, con aislamiento por `tenant_id`.
- Historial de telemetría y alertas push.

## Arquitectura en tres capas

1. **Borde:** nodos ESP32 (sensores, botón de pánico, sirena) → hub ESP32-S3 por ESP-NOW, protocolo M2bis.
2. **Transporte:** el hub publica por MQTT over WSS (puerto 443) a través de Cloudflare Tunnel.
3. **Servidor:** Node-RED procesa y expone la API; PostgreSQL guarda configuración; InfluxDB guarda telemetría; Grafana, Traccar y Gotify cubren visualización, rastreo y alertas; la PWA es la interfaz de usuario.

## Qué está implementado

- Autenticación JWT multi-tenant (registro, login, perfil).
- CRUD de sitios y dispositivos, códigos de vinculación (`claim codes`).
- Armado/desarmado por sitio desde la PWA (publicación MQTT asíncrona).
- Portal cautivo en el hub para aprovisionamiento Wi-Fi/MQTT.
- Actualización OTA de firmware por HTTPS.
- Telemetría y alertas por MQTT hacia InfluxDB y Gotify.
- Validación del enlace ESP-NOW M2bis (AT-02: ACK < 50 ms).

El detalle cronológico y las lecciones por fase están en [Estado por fases](fases/RESUMEN_FASES.md).

## Fuera de alcance por ahora

- Aplicación móvil nativa.
- Integración con centrales de monitoreo externas.
- Facturación y cobro automático.
- Cifrado de extremo a extremo en el tramo ESP-NOW (hay AES-128 con clave precompartida, pero no rotación automática).

## Documentos relacionados

- [Manual general](01-manual-general.md): arquitectura, endpoints, base de datos y operación.
- [Marco teórico](marco-teorico/portada.md): diseño formal y análisis de mercado.
- [Hardware](hardware/README.md): diseño del hub, nodos y protocolo.
- [Firmware](firmware/README.md): implementación y specs.
- [Backend](backend/README.md): Node-RED y base de datos.
