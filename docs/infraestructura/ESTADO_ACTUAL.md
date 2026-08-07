# Estado Actual de la Plataforma (actualizado 2026-08-06)

Registro de diferencias reales respecto a la documentacion original (infraestructura_servidor.pdf, junio 2026) y cambios realizados durante la integracion del sistema de alarma.

## Diferencias respecto al PDF original

| Tema | PDF (junio 2026) | Realidad (agosto 2026) |
|---|---|---|
| Datos de Node-RED | ./nodered/data (bind mount) | Volumen nombrado iot-alarma_nodered-data |
| Contenedor del tunnel | container_name: cloudflared | container_name: cloud |
| Protocolo del tunnel | quic (por defecto) | http2 forzado en el command |
| PostgreSQL | No incluido | Contenedor postgres:16-alpine, usuario admin, BD seguimiento360_alarmas |
| Volumenes | gotify-data | Varios volumenes nombrados (ver docker volume ls) |

## Cambios realizados el 2026-08-06 (integracion alarma)

1. Base de datos seguimiento360_alarmas creada en el contenedor postgres. Seis tablas: users, tenants, sites, devices, device_credentials, device_audit_log. Esquema versionado en backend/database/schema.sql.
2. Node-RED: modulos pg, bcryptjs y jsonwebtoken instalados dentro del contenedor.
3. Node-RED settings.js: functionGlobalContext con pg, bcrypt, jwt, dbConfig (apunta al contenedor postgres) y jwtSecret; httpNodeMiddleware con CORS. Copia de referencia redactada en docs/infraestructura/settings-nodered-reference.js.
4. API de autenticacion desplegada y probada: POST /api/auth/register, POST /api/auth/login, GET /api/auth/profile. Flujos en la pestana Autenticacion; JSON versionado en backend/nodered-flows/flow-auth.json; snapshot completo en docs/infraestructura/snapshots/.
5. Incidente de credenciales: flows_cred.json se corrompio durante las ediciones de settings.js y fue reseteado el 2026-08-06. PENDIENTE: re-ingresar tokens en los nodos Telegram, Gotify y HTTP durante la fase Traccar.
6. NODE_RED_CREDENTIAL_SECRET definido en ~/iot-alarma/.env y consumido por credentialSecret del settings.js.

## Donde viven los secretos (NO versionar en git)

- Contrasena de postgres: env del contenedor y POSTGRES_PASSWORD en ~/iot-alarma/.env
- jwtSecret y dbConfig: /data/settings.js dentro del volumen iot-alarma_nodered-data
- Credential secret de Node-RED: NODE_RED_CREDENTIAL_SECRET en ~/iot-alarma/.env
- Todos quedan cubiertos por el backup semanal (volumenes + .env + pg_dumpall)

## Pendientes

- [ ] Re-ingresar credenciales de nodos (fase Traccar)
- [ ] Hardening de Mosquitto (allow_anonymous false + ACL) antes del lanzamiento de alarmas
- [ ] Estandarizar origenes del tunnel a nombres de contenedor
- [ ] Restringir puertos publicados a 127.0.0.1 donde sea posible
- [ ] adminAuth para Node-RED

### PWA Autenticación (Fase 2)
- **Estado:** ✅ Completado
- **Fecha:** 2026-08-07
- **Componentes:**
  - PWA vanilla (HTML/CSS/JS) en `frontend/pwa/`
  - Build versionado con `scripts/deploy-pwa.sh`
  - Login/Registro/Perfil con JWT (24h expiry)
  - CORS habilitado vía `httpNodeCors` en `settings.js`
  - Service Worker v2 con network-first para navegaciones
- **Acceso:** https://seguimiento360.com/app/
- **API:** https://nodered.seguimiento360.com/api/auth/*
