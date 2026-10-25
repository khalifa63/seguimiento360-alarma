# Portal web — Manual tecnico

Como funciona la parte web: registro, sesion, listado de dispositivos,
configuracion e informes. Documenta lo verificado contra el sistema desplegado y
las brechas reales.

## Arquitectura

    [Navegador] --> [nginx :8088] --> /api/ --> [Node-RED :1880] --> [PostgreSQL]
       portal/        estaticos       proxy         flujos            alarmas

- **Frontend**: HTML estatico en `~/iot-alarma/website/portal/`, servido por
  nginx.
- **API**: flujos de Node-RED. El proxy `/api/` los expone en el mismo dominio.
- **Datos**: PostgreSQL, base `seguimiento360_alarmas`.

El nginx propio escucha en el **8088**; el 80 lo ocupa otro contenedor. Por eso
`localhost/...` da 404 y `localhost:8088/...` funciona.

## Endpoints

Verificados en el flujo desplegado:

| Metodo | Ruta | Que hace |
|---|---|---|
| POST | `/api/auth/register` | crea usuario y su tenant |
| POST | `/api/auth/login` | valida y emite JWT |
| GET | `/api/auth/profile` | perfil del usuario del token |
| GET | `/api/auth/nodes` | dispositivos del tenant |
| GET | `/api/nodes` | dispositivos (mismo dato, otra ruta) |
| POST | `/api/nodes` | registra un dispositivo |
| PUT | `/api/nodes/:id` | edita y sincroniza con el aparato |
| DELETE | `/api/nodes/:id` | borra |
| GET | `/api/overview` | informe jerarquico: sitios -> hubs -> nodos |
| GET | `/api/events` | historial de eventos, con filtros |
| POST | `/api/alarm/:action` | armar y desarmar |
| POST | `/api/ota/trigger` | lanza una actualizacion de firmware |

Todos menos el registro y el login **exigen JWT**.

## Registro

`POST /api/auth/register` recibe `email`, `password`, `full_name`, `phone`.

Lo que hace, verificado en el codigo:

| Validacion | Regla |
|---|---|
| Email y contrasena | obligatorios |
| Contrasena | minimo **8 caracteres** |
| Email | formato valido por expresion regular |
| Duplicado | devuelve **409** si ya existe |

Y despues, en una sola conexion:

1. Hashea con **bcrypt**, 10 rondas.
2. `INSERT INTO users` con rol `customer`.
3. `INSERT INTO tenants` con el nombre derivado del correo, ligado al usuario.

**Crea el tenant automaticamente.** Un usuario nuevo puede usar el portal sin
que nadie le configure nada.

## Sesion

`POST /api/auth/login` devuelve un **JWT de 24 horas**. El frontend lo guarda y
lo envia como `Authorization: Bearer <token>`.

Cada endpoint valida el token y de ahi saca el `tenant_id`. **El aislamiento
entre clientes se apoya en ese campo**, no en el `id` que mande el navegador:
todas las consultas filtran por `tenant_id`.

### Un detalle del login

El endpoint toma el tenant del primer registro que devuelve su consulta. Si un
usuario es propietario de mas de un tenant, el token puede traer uno arbitrario.
Se detecto durante las pruebas y costo un rato de diagnostico. **Falta un
`ORDER BY` explicito.**

## Listado de dispositivos

`GET /api/auth/nodes` devuelve los nodos del tenant:

    SELECT n.id, n.name, n.mac_address, n.location, n.room, n.status,
           n.last_seen, n.created_at, h.name AS hub_name
    FROM nodes n
    JOIN hubs h ON n.hub_id = h.id
    WHERE h.tenant_id = $1
    ORDER BY n.created_at DESC;

**No tiene paginacion.** Con muchos dispositivos devuelve todos. Es aceptable
hasta unos cientos; a partir de ahi conviene anadir `LIMIT` y `OFFSET`.

## Informes

`GET /api/overview` es el informe jerarquico: agrupa **sitios -> hubs -> nodos**
en una sola respuesta. Es lo que usa el panel para mostrar la estructura.

Devuelve, por nodo: id, mac, nombre, ubicacion, habitacion, umbral, estado y
ultima senal. Y por hub: id, nombre, mac, estado, ultima conexion y estado de la
alarma.

**Esto cubre parcialmente la brecha de "vista por hub"**: los datos ya vienen
agrupados por hub. Lo que falta es una **pagina** que los muestre asi.

## Historial de eventos

`GET /api/events` acepta filtros y **si tiene paginacion**:

| Parametro | Efecto |
|---|---|
| `site_id` | filtra por sitio |
| `node_mac` | filtra por dispositivo |
| `event_type` | filtra por tipo (`ALRM`, `HEAR`...) |
| `from` / `to` | rango de fechas |
| `limit` / `offset` | paginacion (tope 200 por pagina) |

Devuelve ademas el total de coincidencias, para poder paginar.

### Los datos existen y se estan guardando

No es una promesa: la tabla `events` tiene registros reales de los tres tipos
que produce el sistema hoy.

| Tipo | Que significa |
|---|---|
| `ALRM` | el sensor detecto algo dentro del umbral |
| `HEAR` | latido del nodo, para saber que sigue vivo |
| `NODE_LOST` | el hub dejo de recibir latidos de un nodo |

Las cifras concretas cambian cada dia, asi que no se copian aqui. Para verlas:

    docker exec postgres psql -U admin -d seguimiento360_alarmas \
      -c "SELECT event_type, count(*) FROM events GROUP BY event_type;" 

Los eventos se guardan por dos vias:

1. **Telemetria continua** (GPS, bateria, distancia) a **InfluxDB**.
2. **Eventos discretos** a **PostgreSQL**, que es lo que muestra la API.

## Las brechas reales

Verificadas, no heredadas de una auditoria:

### 1. No hay auto-registro de dispositivos

El hub guarda el nodo en su NVS al vincularlo, pero **no lo publica**. Hasta que
alguien lo registra a mano con `POST /api/nodes`, el dispositivo **no aparece en
la web**.

Detalle y solucion propuesta en [vinculacion.md](vinculacion.md).

### 2. Paginas sin datos

`nodos.html` y `eventos.html` existen en el portal, tienen titulo y encabezado,
pero **no hacen ninguna llamada a la API**: cero `fetch`. Son maquetas.

Ademas **no estan enlazadas** desde el panel. Un usuario no llega a ellas.

### 3. El panel no se actualiza solo

Carga los datos al abrir. Para ver cambios hay que recargar. No hay suscripcion
MQTT en el navegador.

### 4. Sin vista por hub

`/api/overview` ya devuelve la jerarquia agrupada, pero **no existe la pagina**
que la muestre. Falta `hub_detail.html`.

### 5. Sin correos

No hay confirmacion de registro, ni recuperacion de contrasena. Un usuario que
olvida su clave depende del soporte.

### 6. Sin rate limiting

`/api/auth/register` y `/api/auth/login` no limitan intentos.

### 7. Login sin `ORDER BY`

Ya descrito arriba: con varios tenants, el token puede traer uno arbitrario.

## Diagnostico

| Sintoma | Causa |
|---|---|
| `404` en `/api/...` desde el servidor | se consulto el puerto 80 en vez del 8088 |
| `401` en todo | el token expiro (dura 24 h) |
| El dispositivo no aparece | no se registro con `POST /api/nodes` |
| `409` al registrar | ese correo ya existe |
| `mac_address debe tener formato...` | se envio sin los dos puntos |
| El historial sale vacio | no hay eventos de ese dispositivo en ese rango |
