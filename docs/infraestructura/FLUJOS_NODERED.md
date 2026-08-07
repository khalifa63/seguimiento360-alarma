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
