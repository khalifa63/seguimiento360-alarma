# Flujos Node-RED

## flow-auth.json
API de autenticacion del sistema de alarma (pestana Autenticacion).
- POST /api/auth/register: crea usuario y tenant, password con bcrypt (10 rounds)
- POST /api/auth/login: valida credenciales y emite JWT con expiracion de 24 horas
- GET /api/auth/profile: valida Bearer token y devuelve el perfil

Dependencias (instaladas dentro del contenedor nodered): pg, bcryptjs, jsonwebtoken.
Configuracion: functionGlobalContext del settings.js (dbConfig apunta al contenedor postgres, base seguimiento360_alarmas).

## Importar
Node-RED, menu Import, seleccionar o pegar flow-auth.json, luego Deploy.

## Snapshots completos
docs/infraestructura/snapshots/flows-full-*.json contiene TODOS los flujos (GPS + auth) exportados desde la API. Para restaurar la plataforma: Import del snapshot mas reciente.
