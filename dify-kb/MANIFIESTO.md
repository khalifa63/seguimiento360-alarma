# MANIFIESTO KNOWLEDGE BASE DIFY (v1)

## Precedencia (mayor a menor)
1. PROMPT-SDD-MASTER.md / PROMPT-SDD-SERVER.md (contratos §6 gemelos)
2. specs aprobadas (firmware/*/specs, backend/specs)
3. docs/hardware + docs/patrones + docs/fases (implementado)
4. docs/infraestructura (operacion)
5. infraestructura_servidor.pdf (HISTORICO junio 2026; superado en:
   auth Mosquitto per_listener_settings, PostgreSQL multi-tenant,
   PWA, hubs/alarm, usuarios MQTT nodered/hub-dev, WSS path /mqtt)

## Estado operativo actual (fuente de verdad)
- Red Docker iot-alarma_default: mosquitto, nodered, postgres,
  influxdb, grafana, gotify, nginx, cloudflared, uptime-kuma, traccar
- Mosquitto: per_listener_settings true | 1883 anonimo interno |
  8083/9001 autenticados | wss mqtt.seguimiento360.com:443 path /mqtt
- Usuarios MQTT: nodered (readwrite #), hub-dev (readwrite seguimiento360/#)
- PostgreSQL seguimiento360_alarmas: users, tenants, sites, devices,
  device_credentials, device_audit_log, hubs, alarm_state, claim_codes
- API: /api/auth/*, /api/sites, /api/devices, /api/claim, /api/hubs,
  /api/alarm/control, /api/alarm/state, /api/admin/generate-codes
- PWA: /login /register /dashboard /devices /alarm-control (router app.js)
- InfluxDB: org seguimiento360, bucket telemetria, measurement gps_data

## Carga por chat
comun/      -> ambos chats
firmware/   -> Chat A (firmware)
servidor/   -> Chat B (servidor)
Mantener sincronizado: tras cada spec aprobada o fase cerrada,
re-ejecutar scripts/dify-kb-pack.sh y re-subir cambios a Dify.
