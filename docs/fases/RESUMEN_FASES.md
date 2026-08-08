# Resumen de Fases - Seguimiento 360 Alarmas

Actualizada: 2026-08-08
Estado: Fase 3.2.2b completada. Siguiente: 3.2.3 (armado/desarmado MQTT).

## Fase 1: Base ✅
- PostgreSQL 16, BD seguimiento360_alarmas (contenedor postgres, user admin)
- Tablas: users, tenants, sites, devices, device_credentials, device_audit_log
  (claim_codes agregada en 3.2.2a)
- API auth Node-RED (register/login/profile) con JWT 24h
- Backup 3-2-1 y documentación base

## Fase 2: PWA ✅
- PWA vanilla (sin frameworks), mobile-first, dark mode
- Router SPA hash-based: #/login, #/register, #/dashboard, #/devices
- Build versionado: deploy-pwa.sh inyecta ?v=TIMESTAMP en imports y SW
- CORS global en Node-RED (httpNodeCors + httpNodeMiddleware)
- Deploy: frontend/pwa del repo -> ~/iot-alarma/website/app/
- Commit: c66f301

## Fase 3.1: API sitios/dispositivos ✅
- GET /api/sites y GET /api/devices filtrados por tenant_id del JWT
- Vista PWA #/devices
- Config node s360_pg_alarmas (BD seguimiento360_alarmas, password env PG_PASSWORD)
- Lecciones:
  * Validar JWT con global.get('jwt') y global.get('jwtSecret');
    NUNCA env.get('JWT_SECRET') ni libs (libs conflictua con global.get)
  * settings.js debe tener EXACTAMENTE UN functionGlobalContext
    (duplicados causaron crash-loop y secretos inconsistentes)
  * functionGlobalContext define: pg, bcrypt, jwt, dbConfig, jwtSecret
- Commits: ed76dd4, b0d877d, 25d0491, bdb2eb9, de303ba, fc17791

## Fase 3.2.1: CRUD sitios ✅
- POST /api/sites (201), PUT /api/sites/:id (parcial con COALESCE, 404 si ajeno)
- PWA: formulario de creación y edición inline con nombre+direccion prellenados
- Aislamiento multi-tenant verificado (WHERE tenant_id = JWT)
- Lección: PUT sin COALESCE sobrescribia address con null
- Commits: 5a55aa8, 1dac66f

## Fase 3.2.2a: Claim codes (backend) ✅
- Tabla claim_codes: code (XX-YYYYYY-Z), device_type, status active/used,
  created_by, tenant_id, site_id, claimed_device_id, claimed_at, expires_at
- CHARSET 31 chars (sin 0/1/I/L/O/U); checksum = suma ASCII mod 31
- POST /api/admin/generate-codes {prefix, quantity} -> 201 + codes[]
- POST /api/claim {code, site_id}:
  formato -> checksum -> SELECT sin filtro de status -> check en JS
  (404 no existe / 400 ya usado / 403 ajeno) -> UPSERT devices
  (serial SN-CODE, ON CONFLICT serial_number) -> UPDATE claim_codes used
- Lecciones (criticas):
  * Nodos con outputs:2 deben retornar [msg, null] o [null, msg] y sus
    wires deben tener DOS arrays; wires incompleto = request colgada
  * SELECT con status='active' impedia distinguir 404 de 400
  * Validar checksum ANTES del SELECT evita enumeracion de codigos
- Commits: 8e27d13, 3cea517, e72eb1b, b876cb0, fcf8a6d, 26c862b, b6013d8

## Fase 3.2.2b: Vinculación (frontend) ✅
- #/devices: formulario vincular (codigo + selector de sitio),
  sitios con dispositivos agrupados, sección admin (generar códigos)
  visible solo si role=admin en el JWT
- api.js: claimDevice, generateCodes

## Fase 3.2.3: Armado/desarmado (siguiente)
- POST /api/alarm/arm y /api/alarm/disarm -> publicar MQTT a
  seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/command
- Botones por sitio en PWA; WebSocket para eventos en Fase 3.3

## Convenciones globales
- Nodos: s360v{version}_{endpoint}_{accion}
- Error HTTP: msg.statusCode + msg.payload {error} -> s360v2_http_error
- Exito HTTP: s360v2_http_ok (respeta msg.statusCode)
- SQL: SIEMPRE tenant_id del JWT; updates parciales con COALESCE

## Fase 3.2.3b: Frontend alarma (PWA) ✅
- Vista #/alarm-control accesible desde dashboard
- Selector de sitio + botones ARMAR (rojo) / DESARMAR (verde)
- Polling cada 10s de /api/alarm/state (NO WebSocket, respetando batería ESP32)
- Toast con nota: "El ESP32 lo ejecutará en su próximo wake-up (5-10 min)"
- Muestra último hub activo del sitio + serial + last_seen

### Patrón de diseño aplicado
Publicación MQTT asíncrona post-respuesta HTTP:
1. Flujo síncrono: validar → auditar → actualizar estado → http_ok
2. Efecto lateral: trigger con setTimeout(100ms) → mqtt out
3. Ventaja: respuesta HTTP inmediata (< 100ms), publish no bloquea
4. Alineado con PDF §4.3 (fire-and-forget, ESP32 lee en wake-up)
