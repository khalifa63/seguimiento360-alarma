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

## Cual es la fuente de verdad

**Produccion.** El flujo que corre en el contenedor `nodered` es el que manda;
los archivos del repo son copias que pueden estar desfasadas.

Conviene tenerlo presente porque en octubre de 2026 se detecto que las tres
versiones existentes **no coincidian entre si**:

| Archivo | Nodos | Fecha |
|---|---|---|
| `/data/flows.json` en el contenedor (produccion) | 117 | 23-oct |
| `flows.json` (este directorio) | 87 | version de trabajo |
| `docs/infraestructura/snapshots/flows-full-20260806.json` | 225 | 6-ago |

Un diagnostico hecho sobre `flows.json` puede ser **falso**: describira nodos
que no estan desplegados. Ocurrio al analizar el claim code, cuyos endpoints
(`/api/claim`, `/api/admin/generate-codes`) existen en la copia del repo pero
**no en produccion**.

### Exportar produccion al repo

Antes de analizar o cambiar flujos, traer la version desplegada:

    docker cp nodered:/data/flows.json /tmp/flows_activo.json
    cp /tmp/flows_activo.json \
       docs/infraestructura/snapshots/flows-produccion-$(date +%Y%m%d).json

Y **verificar antes de versionar** que no lleve credenciales. Los nodos
`postgresql` deben quedar sin `password` (lo toma de la configuracion global del
contenedor); si alguno lo llevara incrustado, hay que quitarlo antes del commit.

### Como saber que version corre

    docker exec nodered python3 -c \
      "import json;print(len(json.load(open('/data/flows.json'))),'nodos')"

Comparar el numero con el de los archivos del repo. Si no coincide, el repo
esta desfasado.

### Al cambiar un flujo

1. Exportar produccion y guardarla como snapshot.
2. Editar **sobre ese archivo**, no sobre `flows.json`.
3. Importar en Node-RED y desplegar.
4. Volver a exportar y actualizar el snapshot, para que lo versionado sea lo
   que corre.

## Snapshot 2026-10-29

`docs/infraestructura/snapshots/flows-produccion-20261029.json`: 154 nodos
exportados del contenedor nodered, segun la convencion de este README.
Verificado sin credenciales: los nodos postgresql usan referencias de entorno
(PG_PASSWORD) y las credenciales del broker MQTT van blanqueadas en el
snapshot (los valores reales viven solo en el contenedor).

Actualizacion 2026-10-29 (SPEC-02 v0.3): la ingesta escribe la columna
`events.value` (antes `distance_cm`) y acepta tanto la clave nueva `value`
como la historica `distance_cm` del JSON del hub, para la transicion hasta
reflashear el hub. La consulta de eventos expone `value AS distance_cm`
mientras la UI termina su migracion.

Actualizacion 2026-10-30 (SPEC-02 v0.3): la ingesta resuelve `node_name` con
COALESCE desde la tabla `nodes` cuando el hub lo manda vacio, y la rama de
push consulta name/room/location por mac del nodo. El push de DOOR muestra
el estado legible (Cerrado/Abierto) en lugar de "N cm".
