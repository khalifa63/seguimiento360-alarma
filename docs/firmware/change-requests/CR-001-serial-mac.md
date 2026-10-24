# CR-001 — El serial del hub pasa a ser su MAC

Estado: **en revision** — la decision es correcta, la aplicacion esta bloqueada por la desincronizacion
Afecta a: HUB-IF-003, HUB-IF-004, `/api/claim`, AT-007

## Que cambia

El identificador de un hub deja de ser un serial inventado por el backend y
pasa a ser **su MAC**, en formato canonico.

## Por que

Cada parte usaba una convencion distinta y ninguna coincidia:

| Quien | Formato | Donde |
|---|---|---|
| `/api/claim` | `SN-` + codigo, p.ej. `SN-S3-AZJH86-M` | nodo "Preparar UPSERT" |
| `/api/hubs` | `HUB-` + marca de tiempo | nodo "Prep INSERT hub" |
| Hub fisico | su MAC, p.ej. `3cdc7562c2b0` | `HUB_ONLINE` |

El claim code existia para dar de alta un equipo nuevo sin configurarlo a
mano, pero no podia funcionar: el backend creaba un registro con un serial que
el equipo nunca reportaria, asi que la fila quedaba huerfana y el hub seguia
sin cuenta.

El propio backend lo dejaba entrever:

    // Actualizar hub por ID (sin validar serial_number en este paso)
    // El serial_number ya fue validado cuando se registro el hub

La vinculacion se apoyaba en el ID del hub, no en el serial. Funciona mientras
el hub ya exista; no cubre el alta de uno nuevo.

## La decision

**La MAC es el identificador.** Ya es unica, ya viene grabada de fabrica, el
firmware ya la reporta y la tabla `hubs` ya la usa hoy (`3cdc7562c2b0`). Es la
opcion con menos cambios y la unica que no exige un paso de fabricacion.

Se descartaron:

- **Serial grabado en NVS.** Mas honesto semanticamente, pero exige un paso de
  produccion que grabe el numero, y no existe.
- **El claim code como identificador.** Ataria la identidad del equipo a un
  codigo de un solo uso: al canjearlo dejaria de servir para identificarlo.

## Formato canonico

    xxxxxxxxxxxx

Doce digitos hexadecimales en minusculas, **sin separadores**. Ejemplo:
`3cdc7562c2b0`.

Es la forma que ya usan la tabla `hubs` (`serial_number = 3cdc7562c2b0`) y los
topics MQTT. Se eligio esa y no la de dos puntos porque era la que ya estaba en
uso en los dos sitios donde el dato se persiste y se compara.

**Presentacion para humanos:** en payloads legibles y en la consola la MAC
aparece como `3c:dc:75:62:c2:b0`. El hub ya lo hace asi: publica `mac` con
separadores en `HUB_ONLINE` y usa la forma compacta para los topics.

**Normalizacion:** el backend acepta mayusculas, guiones, dos puntos y el
formato compacto, y **siempre guarda la forma canonica sin separadores**.
Normalizar en un solo sitio evita que cada consumidor tenga que hacerlo.

## Contrato nuevo de `POST /api/claim`

Antes:

    { "code": "S3-AZJH86-M", "site_id": 1 }

Despues:

    { "code": "S3-AZJH86-M", "site_id": 1, "hub_mac": "3c:dc:75:62:c2:b0" }

`hub_mac` es **obligatorio**: sin el, el backend no sabe a que equipo vincular
el codigo. El serial del hub pasa a ser la MAC normalizada, no `SN-` + codigo.

## Compatibilidad

**No hay datos que migrar:**

- `devices`: 0 filas.
- `hubs`: 1 fila, y su `serial_number` ya es la MAC, asi que cumple el formato
  sin tocarla.

El cambio es de codigo, no de registros.

## Hallazgo al aplicar el cambio (2026-10-24)

Al ir a modificar el backend aparece algo que **invalida el diagnostico
original** y que conviene resolver antes de seguir.

### El flujo desplegado no es el del repo

| | Nodos | Fecha | Endpoints que expone |
|---|---|---|---|
| `backend/nodered-flows/flows.json` | 87 | version de trabajo | `/api/claim`, `/api/hubs`, `/api/sites` |
| **Produccion** (`/data/flows.json`) | **117** | 23-oct | `/api/ota/trigger`, `/api/overview`, `/api/events` |
| `docs/infraestructura/snapshots/flows-full-20260806.json` | 225 | 6-ago | varios |

Son **tres versiones distintas**, y la que corre no es ninguna de las que estan
en el repo. El claim code (`/api/claim`) **no existe en produccion**: solo esta
en la copia del repo.

### Produccion ya usa la MAC

El flujo desplegado ya trata `hub_mac` y `serial_number` como el mismo dato:

    if (!row.hub_mac) missing.push('serial_number');
    const hubMac = String(row.hub_mac).replace(/:/g, '').toLowerCase();

Construye los topics con la MAC sin dos puntos. Es decir: **la decision de este
CR ya esta aplicada en produccion**, pero por otra via y sin documentar.

### Lo que esto significa

1. La decision de usar la MAC como identificador **es correcta y ya esta en
   marcha**. Este CR la formaliza y la documenta.
2. El diagnostico de la brecha de seriales se hizo sobre la copia del repo, no
   sobre lo desplegado. **El claim code que analice no esta en produccion**, asi
   que la brecha afecta a una version que no corre.
3. Antes de tocar nada conviene **decidir cual es la fuente de verdad**: si
   produccion, hay que exportarla al repo; si el repo, hay que desplegarlo.

### Pendiente antes de aplicar nada mas

- **Sincronizar repo y produccion.** Sin esto, cualquier cambio en el repo no
  llega al sistema y cualquier diagnostico sobre el repo puede ser falso.
- Comprobar si el claim code debe existir. Si produccion no lo tiene y el alta
  de hubs se hace de otra forma, HUB-F-016 cambia de alcance.

## Pendiente que este CR no resuelve

Si alguien canjea un codigo con la MAC de un hub **que ya pertenece a otro
tenant**, el `ON CONFLICT` lo reasigna. Conviene decidir si debe rechazarse.
Se anota, no se resuelve aqui.
