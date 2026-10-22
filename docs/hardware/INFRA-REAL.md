# INFRA-REAL v1.0 — Estado real del servidor
Fuente: infraestructura_servidor.pdf (Junio 2026)

- Broker: Mosquitto (Docker, red iot-alarma_default)
- Externo: wss://mqtt.seguimiento360.com:443 (Cloudflare Tunnel -> mosquitto:8083 WS)
- Interno: mqtt://mosquitto:1883 (sin cifrar)
- TLS: termina en Cloudflare; listener 8883 TLS pendiente de config
- Auth HOY: allow_anonymous true
- Auth OBJETIVO: user/pass por dispositivo (mosquitto_passwd); mTLS no disponible
- Topics: §6 sin cambios (seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/...)
- Consecuencia: CR A4 v0.3 (WSS + CA pública + user/pass; mTLS futuro)
