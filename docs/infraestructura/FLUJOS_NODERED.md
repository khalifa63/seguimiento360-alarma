# Flujos de Node-RED — Catálogo Operativo (Seguimiento 360)

**Actualizado:** 2026-08-07
**Snapshot limpio:** backend/nodered-flows/flows.json (10 nodos, podado)
**Auditoría de funciones:** backend/nodered-flows/autenticacion-funciones.js
**Archivo legacy:** backend/nodered-flows/archive/flows-legacy-ghostkey-20260807.json
(desarrollo GhostKey, schema ghostkey_v2 sobre BD traccar; NO activo, solo referencia)

---

## 1. Configuración runtime (backend/nodered/settings.js)

- credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET (desencripta flows_cred.json)
- functionGlobalContext: pg (pg), bcrypt (bcryptjs), jwt (jsonwebtoken)
- httpNodeRoot: "/"
- httpNodeCors: ACTIVO (origin "*", metodos GET,PUT,POST,DELETE, preflight 204)
- httpNodeMiddleware: headers CORS + OPTIONS 204 (defensa en profundidad)
- Editor: sin adminAuth (endurecimiento pendiente, Cap. 7.7 del marco teorico)

## 2. Tab activo: Autenticacion (id 1a2b3c4d5e6f7001)

Patron por endpoint: http in -> function -> http response.
BD: seguimiento360_alarmas (contenedor postgres, red Docker iot-alarma_default).

### POST /api/auth/register — funcion "Registrar usuario"
- Body: email, password, full_name, phone
- Validaciones: requeridos email/password/full_name; regex email; password >= 8; email a minusculas
- bcrypt 10 rounds; role default customer
- Transaccion: INSERT users + INSERT tenants (owner_user_id = nuevo user)
- 201: success + user + tenant_id | 400 validacion | 409 email duplicado | 500

### POST /api/auth/login — funcion "Login + JWT"
- 200: success + token JWT (expiracion 24 h) + user con tenant_id
- Payload JWT: user_id, tenant_id, role (Cap. 7.4)
- 400 faltantes | 401 credenciales invalidas | 403 cuenta desactivada (is_active=false) | 500

### GET /api/auth/profile — funcion "Perfil con JWT"
- Header: Authorization Bearer
- 200: user completo + tenant_name | 401 token ausente/invalido/expirado | 404 | 500

## 3. Estandares para nuevos endpoints (Fase 3 en adelante)

1. Un tab por funcionalidad, nombre "S360 - <nombre>".
2. Primera function del chain: validar JWT con jwt de functionGlobalContext;
   rechazar con 401 antes de tocar BD.
3. SQL con pg usando parametros $1..$n; SIEMPRE filtrar por tenant_id del JWT
   (aislamiento multi-inquilino, Cap. 7.4).
4. Errores como {"error": "mensaje"} con msg.statusCode; exitos con payload JSON.
5. NO definir headers CORS por nodo: CORS es global (httpNodeCors).
6. Todo cambio de flujos: docker cp del snapshot al repo + commit (fuente de verdad).
7. Auditoria de acciones criticas en device_audit_log (bind/unbind/transfer).

## 4. Patrones rescatables del legacy GhostKey (referencia de rediseño)

Cuando se construyan los endpoints de alarma/dispositivos sobre el esquema
multi-inquilino, reutilizar estos patrones probados del archivo legacy:

- FSM de estados: FACTORY, WAREHOUSE, DISARMED, ARMED, ALERT_MOVING,
  ALERT_STOPPED, INACTIVE, BATTERY_CRITICAL.
- Modelo desired_state vs device_state: el estado real solo cambia tras ACK
  "executed" del hardware (estilo Kubernetes, evita races).
- Cola de comandos: pending -> delivered -> executed/failed/expired,
  con prioridad, expiracion 24 h y reentrega de delivered.
- Claim codes con checksum: formato XX-YYYYYY-Z, CHARSET de 31 caracteres
  (sin 0,1,I,L,O,U), checksum = suma ASCII mod 31.
- Eventos con catalogo (event_definitions) y politicas de notificacion
  (cooldown, plantillas title/message, provider Gotify).
- Snapshot (tabla devices) separado del historico (device_telemetry).
- Soft-delete universal con deleted_at IS NULL.

## 5. Tópicos MQTT multi-inquilino (Cap. 4.6, pendiente implementar en flujos)

    seguimiento360/tenant/{tenant_id}/site/{site_id}/hub/{hub_id}/status
    seguimiento360/tenant/{tenant_id}/site/{site_id}/hub/{hub_id}/command
    seguimiento360/tenant/{tenant_id}/site/{site_id}/sensor/{sensor_id}/alert
    seguimiento360/tenant/{tenant_id}/vehicle/{vehicle_id}/gps
    seguimiento360/admin/ota/manifest

## 6. Tab: S360 - Panel (Fase 3.1) — 2026-08-08

Endpoints del panel, filtrados por tenant_id del JWT.
Config Postgres: nodo s360_pg_alarmas (host postgres, BD seguimiento360_alarmas,
user admin, password via env PG_PASSWORD).

### GET /api/sites
Auth: Bearer JWT
Query:
    SELECT id, name, address, latitude, longitude, is_active, created_at
    FROM sites
    WHERE tenant_id = $1
    ORDER BY name;
Respuesta 200: array de sitios del tenant autenticado.

### GET /api/devices
Auth: Bearer JWT
Query:
    SELECT d.id, d.serial_number, d.device_type, d.firmware_version, d.status,
           d.bound_at, d.unbound_at, d.site_id, s.name as site_name
    FROM devices d
    LEFT JOIN sites s ON s.id = d.site_id
    WHERE d.tenant_id = $1
    ORDER BY s.name, d.device_type;
Respuesta 200: array de dispositivos con nombre de sitio.

### PWA
- Vista #/devices (js/views/devices.js): sitios con dispositivos agrupados,
  dispositivos sin sitio asignados y regreso al dashboard.
- Dashboard: boton "Ver Mis Sitios y Dispositivos".

## 6. Tab: S360 - Panel (Fase 3.1) — 2026-08-08

Endpoints del panel, filtrados por tenant_id del JWT.
Config Postgres: nodo s360_pg_alarmas (host postgres, BD seguimiento360_alarmas,
user admin, password via env PG_PASSWORD).

### GET /api/sites
Auth: Bearer JWT
Query:
    SELECT id, name, address, latitude, longitude, is_active, created_at
    FROM sites
    WHERE tenant_id = $1
    ORDER BY name;
Respuesta 200: array de sitios del tenant autenticado.

### GET /api/devices
Auth: Bearer JWT
Query:
    SELECT d.id, d.serial_number, d.device_type, d.firmware_version, d.status,
           d.bound_at, d.unbound_at, d.site_id, s.name as site_name
    FROM devices d
    LEFT JOIN sites s ON s.id = d.site_id
    WHERE d.tenant_id = $1
    ORDER BY s.name, d.device_type;
Respuesta 200: array de dispositivos con nombre de sitio.

### PWA
- Vista #/devices (js/views/devices.js): sitios con dispositivos agrupados,
  dispositivos sin sitio asignados y regreso al dashboard.
- Dashboard: boton "Ver Mis Sitios y Dispositivos".

### POST /api/sites (Fase 3.2.1) — 2026-08-08
Auth: Bearer JWT
Body: name (requerido), address, latitude, longitude (opcionales)
Query:
    INSERT INTO sites (tenant_id, name, address, latitude, longitude)
    VALUES ($1, $2, $3, $4, $5)
    RETURNING id, tenant_id, name, address, latitude, longitude, is_active, created_at;
tenant_id tomado SIEMPRE del JWT (nunca del body).
201: success + site | 400 nombre faltante | 401 | 500

### PUT /api/sites/:id (Fase 3.2.1)
Auth: Bearer JWT
Body: name (requerido), address, latitude, longitude
Query:
    UPDATE sites SET name=$1, address=$2, latitude=$3, longitude=$4
    WHERE id=$5 AND tenant_id=$6
    RETURNING ...;
El WHERE con tenant_id garantiza aislamiento multi-tenant.
200: success + site | 404 no encontrado | 400 | 401
