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
Los snapshots completos de flujos NO se versionan: pueden contener credenciales. Se exportan desde el contenedor cuando haga falta:

```bash
docker exec nodered cat /data/flows.json > /tmp/flows-full-$(date +%F).json
```

Para restaurar: importar el snapshot más reciente desde la UI de Node-RED.
