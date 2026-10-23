# Backend

Node-RED expone la API REST y ejecuta la lógica de negocio. PostgreSQL guarda configuración y usuarios; InfluxDB guarda telemetría.

| Documento | Qué contiene |
|---|---|
| [Base de datos](database.md) | Esquema, tablas, dump y restauración |
| [Flujos Node-RED](nodered-flows.md) | Autenticación, importación y snapshots |

## Configuración

La plantilla segura está en [`nodered/data/settings.js.example`](../../nodered/data/settings.js.example). Copia el archivo como `settings.js` y define las variables en `.env`:

```bash
NODE_RED_ADMIN_HASH=...
NODE_RED_CREDENTIAL_SECRET=...
JWT_SECRET=...
PG_HOST=postgres
PG_PORT=5432
PG_DATABASE=seguimiento360_alarmas
PG_USER=nodered_app
PG_PASSWORD=...
CORS_ORIGIN=https://seguimiento360.com
```

Genera el hash del admin con:

```bash
npx node-red admin hash-pw
```

## API principal

- `POST /api/auth/register`
- `POST /api/auth/login`
- `GET /api/auth/profile`
- `GET|POST|PUT|DELETE /api/sites`
- `GET|POST|PUT|DELETE /api/devices`
- `POST /api/claim`
- `POST /api/alarm/arm`, `POST /api/alarm/disarm`
- `GET /api/alarm/state`
- `POST /api/ota/trigger`

Los flujos versionados están en `backend/nodered-flows/`. No se versionan snapshots completos ni credenciales.
