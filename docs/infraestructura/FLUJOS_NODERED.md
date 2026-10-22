# Flujos Node-RED y API

Actualizada: 2026-08-08

## Tabs activos
- Autenticacion (id 1a2b3c4d5e6f7001)
- S360 - Panel (id s360_panel_001)

## Nodos compartidos
- s360_pg_alarmas: postgreSQLConfig, host postgres, BD seguimiento360_alarmas,
  user admin, password env PG_PASSWORD
- s360v2_http_ok / s360v2_http_error: http response (respetan msg.statusCode)

## settings.js (UN solo functionGlobalContext)

    functionGlobalContext: {
        pg: require('pg'),
        bcrypt: require('bcryptjs'),
        jwt: require('jsonwebtoken'),
        dbConfig: { host: 'postgres', port: 5432,
                    database: 'seguimiento360_alarmas', user: 'admin',
                    password: process.env.PG_PASSWORD },
        jwtSecret: process.env.JWT_SECRET
    }

## Endpoints de autenticación

POST /api/auth/register -> 201 user
POST /api/auth/login    -> 200 {token, user}
GET  /api/auth/profile  -> 200 user

Query login:

    SELECT u.id, u.email, u.password_hash, u.full_name, u.phone, u.role,
           u.is_active, t.id as tenant_id
    FROM users u
    LEFT JOIN tenants t ON t.owner_user_id = u.id AND t.is_active = true
    WHERE u.email = $1;

Payload JWT: {user_id, email, role, tenant_id, iat, exp}, expira 24h,
firmado con global.get('jwtSecret').

## Sitios

GET /api/sites

    SELECT id, name, address, latitude, longitude, is_active, created_at
    FROM sites WHERE tenant_id = $1 ORDER BY name;

POST /api/sites (name requerido; tenant_id del JWT, nunca del body)

    INSERT INTO sites (tenant_id, name, address, latitude, longitude)
    VALUES ($1,$2,$3,$4,$5)
    RETURNING id, tenant_id, name, address, latitude, longitude,
              is_active, created_at;

PUT /api/sites/:id (update parcial)

    UPDATE sites
    SET name = COALESCE($1, name), address = COALESCE($2, address),
        latitude = COALESCE($3, latitude), longitude = COALESCE($4, longitude)
    WHERE id = $5 AND tenant_id = $6
    RETURNING id, tenant_id, name, address, latitude, longitude,
              is_active, created_at;

Respuestas: 200/201 success+site | 400 nombre | 404 ajeno/inexistente | 401

## Dispositivos

GET /api/devices

    SELECT d.id, d.serial_number, d.device_type, d.firmware_version, d.status,
           d.bound_at, d.unbound_at, d.site_id, s.name as site_name
    FROM devices d LEFT JOIN sites s ON s.id = d.site_id
    WHERE d.tenant_id = $1 ORDER BY s.name, d.device_type;

## Claim codes

POST /api/admin/generate-codes {prefix(2 chars), quantity(1-500)}

    INSERT INTO claim_codes (code, device_type, tenant_id, created_by)
    VALUES (...) ... RETURNING id, code, device_type, status, created_at;

POST /api/claim {code, site_id}
Flujo: JWT -> validar formato/checksum -> SELECT por code (SIN filtro status)
-> check JS (404/400/403) -> UPSERT devices -> UPDATE claim_codes -> 201

    INSERT INTO devices (tenant_id, site_id, device_type, serial_number,
                         status, bound_at)
    VALUES ($1,$2,$3,$4,'active',NOW())
    ON CONFLICT (serial_number) DO UPDATE
    SET tenant_id = EXCLUDED.tenant_id, site_id = EXCLUDED.site_id,
        status = 'active', bound_at = NOW()
    RETURNING id, tenant_id, site_id, device_type, serial_number, status,
              bound_at;

    UPDATE claim_codes
    SET status = 'used', claimed_device_id = $1, claimed_at = NOW()
    WHERE id = $2;

## Troubleshooting

- "Token inválido": validar que la función usa global.get('jwtSecret'),
  no env.get('JWT_SECRET').
- "bind message supplies X parameters...": gate con outputs:2 debe retornar
  [null, msg] en error; revisar que wires tenga DOS arrays.
- Request colgada (curl sin respuesta): wires incompleto en nodo outputs:2.
- "relation does not exist": tabla no creada; verificar con \dt.
- Crash-loop "list.map is not a function": settings.js malformado
  (externalModules); restaurar desde el repo.

## curl de prueba

    TOKEN=$(curl -s -X POST http://localhost:1880/api/auth/login \
      -H "Content-Type: application/json" \
      -d '{"email":"test2@seg360.com","password":"Test1234"}' \
      | grep -o '"token":"[^"]*"' | cut -d'"' -f4)

    curl -s -H "Authorization: Bearer $TOKEN" http://localhost:1880/api/sites
    curl -s -H "Authorization: Bearer $TOKEN" http://localhost:1880/api/devices

## Fase 3.2.3a: Hubs + Alarm Control (MQTT)

### Arquitectura MQTT
Mosquitto tiene listeners diferenciados:

| Puerto | Protocolo | Exposición | Auth | Uso |
|---|---|---|---|---|
| 1883 | MQTT TCP | Solo red Docker | NO | Node-RED, scripts internos |
| 8083 | WebSocket | Vía Cloudflare | SÍ | ESP32 remotos |
| 8883 | MQTT TLS | Pendiente SSL | SÍ | Futuro |
| 9001 | WebSocket | Vía Cloudflare | SÍ | Alternativo |

### Tablas nuevas
- `hubs`: dispositivos ESP32 físicos (1 sitio : N hubs)
- `alarm_state`: último estado de alarma por sitio

### Tópicos MQTT
- Downlink: `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/command` (retained)
- Uplink hello: `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/hello`
- Uplink response: `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/response`

### Endpoints
- `POST /api/hubs` {site_id, name, device_type} -> 201 hub pending
- `GET /api/hubs?site_id=X` -> lista con alarm_state
- `POST /api/alarm/control` {action: arm|disarm, hub_id} -> publica MQTT + audit log
- `GET /api/alarm/state?site_id=X` -> estado actual

### Flujo de confirmación de dispositivo
1. Usuario registra hub (status=pending)
2. ESP32 al primer boot publica hello con serial_number
3. Node-RED recibe hello, verifica serial coincide
4. UPDATE hubs SET status='active', last_seen=NOW()
5. Hub queda disponible para comandos arm/disarm

## Configuración MQTT con listeners diferenciados

### mosquitto.conf
- Puerto 1883: MQTT TCP, allow_anonymous true (red Docker interna)
- Puerto 8083: WebSocket, allow_anonymous false, password_file + acl (acceso externo)
- Puerto 9001: WebSocket alternativo, allow_anonymous false

### ACL
- user nodered: topic readwrite # (acceso completo)
- pattern seguimiento360/+/+/+/+/%u/# (para ESP32 con username específico)
- pattern ghostkey/%u/# (legacy GhostKey)

### Credenciales Node-RED
- user: nodered
- password: ${MQTT_PASSWORD} (definir en `.env`, no versionar)
- broker: mosquitto:1883

## Arquitectura MQTT con listeners diferenciados (solución final)

### Configuración de puertos
- **Puerto 1883** (TCP): Red Docker interna, `allow_anonymous true`, SIN password_file ni acl_file
  - Usado por: Node-RED, scripts internos
  - Seguridad: Red Docker es zona confiable (defense in depth)
- **Puerto 8083** (WebSocket): Acceso externo vía Cloudflare, `allow_anonymous false`, con password_file + acl_file
  - Usado por: ESP32 remotos
  - Seguridad: Autenticación fuerte + ACL restrictivo
- **Puerto 9001** (WebSocket): Alternativo, misma configuración que 8083
- **Puerto 8883** (TLS): Pendiente configuración SSL

### Por qué esta arquitectura
1. **Simplicidad**: Node-RED no necesita manejar credenciales encriptadas
2. **Seguridad por capas**: Red Docker interna es confiable, acceso externo tiene auth fuerte
3. **Alineación con documentación**: Respetando el principio de mínimo privilegio
4. **Escalabilidad**: Agregar servicios internos es trivial (solo conectan a 1883)

### Tópicos MQTT
- Downlink: `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/command` (retained)
- Uplink hello: `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/hello`
- Uplink response: `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/response`

## Configuración MQTT con per_listener_settings (solución final)

### Problema resuelto
Mosquitto 2.x carga plugins de seguridad globalmente cuando cualquier listener tiene autenticación configurada. Esto causaba que el listener 1883 (con `allow_anonymous true`) también rechazara conexiones anónimas.

### Solución: `per_listener_settings true`
Esta directiva al inicio de `mosquitto.conf` permite que cada listener tenga su propia configuración de seguridad independiente.

### Configuración de puertos

### Resultado
- **Puerto 1883**: Conexiones anónimas permitidas (Node-RED, scripts internos)
- **Puertos 8083/9001**: Requieren autenticación (ESP32 remotos vía Cloudflare)
- **Seguridad por capas**: Red Docker interna confiable + acceso externo con auth fuerte
