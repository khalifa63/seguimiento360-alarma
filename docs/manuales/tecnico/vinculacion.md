# Vinculacion de nodos — Manual tecnico

Como llega un nodo desde "recien desempacado" hasta "aparece en la web".
Documenta lo que existe hoy, verificado contra el sistema desplegado, y lo que
todavia falta.

## Los dos vinculos

Un nodo tiene que quedar registrado en **dos sitios distintos**, y eso es lo que
confunde:

| Vinculo | Donde vive | Para que |
|---|---|---|
| **Fisico** | NVS del hub | el hub acepta sus eventos y le responde |
| **Logico** | PostgreSQL (`nodes`) | el nodo aparece en la web y se le puede configurar |

**Son independientes.** Un nodo puede estar vinculado fisicamente y no existir
en la base de datos. Es el caso que hoy no esta resuelto.

    [Nodo] --PAIR--> [Hub: guarda en NVS] --??--> [PostgreSQL]

## Vinculo fisico: implementado

El nodo arranca sin saber a que hub dirigirse. Lo aprende:

1. **El nodo envia un `PAIR`.** De fabrica no tiene nada en NVS, asi que usa una
   **semilla compilada**: la MAC STA del hub.
2. **El hub lo recibe.** Si el nodo no esta en su registro, lo anade, lo guarda
   en NVS (`nvs_nodes`) y enciende el LED tres veces.
3. **El hub responde con un `ACK`** cuyo `src_mac` lleva la suya.
4. **El nodo guarda esa MAC** en su NVS (`s360node` / `hub_mac`) y deja de usar
   la semilla.

Desde ese momento el nodo sabe a quien hablar aunque cambie el hub.

### Si el hub cambia

El nodo comprueba el enlace cada 10 s. A los 6 intentos sin respuesta (60 s)
borra la MAC y vuelve a buscar. El intervalo crece con los fallos, hasta 10
minutos. Cuando el hub reaparece, se re-vincula solo.

Detalle completo en [sensor-ultrasonico.md](sensor-ultrasonico.md).

### Limite de nodos

Un hub admite **20 nodos** (`MAX_NODES`). Al llegar al limite, rechaza nuevos
`PAIR` con cinco parpadeos del LED.

## Vinculo logico: manual

Hoy un nodo **no aparece en la web solo**. Hay que registrarlo:

    POST /api/nodes
    Authorization: Bearer <JWT>

    {
      "hub_id": 10,
      "mac_address": "a0:f2:62:f1:d0:f0",
      "name": "Sensor Garaje",
      "location": "Casa Principal",
      "room": "Garaje",
      "distance_threshold": 50
    }

Validaciones que aplica el backend:

| Campo | Regla |
|---|---|
| `hub_id` | obligatorio, numerico |
| `mac_address` | obligatorio, formato `xx:xx:xx:xx:xx:xx` |
| `distance_threshold` | entre 2 y 200 cm |
| resto | opcional, con valores por defecto |

La consulta comprueba que el hub **pertenece al tenant del usuario** antes de
insertar, asi que no se puede registrar un nodo en el hub de otro.

### Campos de la tabla `nodes`

| Campo | Por defecto | Notas |
|---|---|---|
| `status` | `pending` | pasa a `active` cuando reporta |
| `name` | `Nodo Sin Nombre` | |
| `location` / `room` | `Sin Ubicacion` / `Sin Habitacion` | |
| `distance_threshold` | 50 | cm |
| `last_seen` | vacio | se llena con el primer evento |

## La brecha: no hay auto-registro

**El hub no publica nada cuando vincula un nodo.** Lo guarda en su NVS, hace
parpadear el LED y responde el `ACK`, pero no avisa a la nube.

El hub solo publica cuando llegan **eventos**, y solo de nodos que ya tiene
registrados:

    if (node_idx >= 0) {
        publish_event_to_mqtt(sender_mac, payload, rx_us);
    } else {
        ESP_LOGW(TAG, "Paquete ignorado: MAC %s no registrada.", mac_str);
    }

**Consecuencia:** un nodo nuevo se vincula fisicamente, empieza a medir, y
**no aparece en la web**. El operador tiene que anotar su MAC de la consola y
registrarlo a mano con `POST /api/nodes`.

### Que haria falta para cerrarla

Dos piezas:

1. **Que el hub publique el vinculo.** Al registrar un nodo nuevo, publicar en
   el topic de eventos algo como:

       {"event":"NODE_PAIRED","mac":"a0:f2:62:f1:d0:f0","type":"ULTRASONIC"}

2. **Que el backend lo recoja.** Un flujo suscrito a ese topic que haga
   `INSERT ... ON CONFLICT (mac_address) DO UPDATE` con el `hub_id` del topic.

La tabla `nodes` no tiene restriccion unica en `mac_address`, asi que el
`ON CONFLICT` exigiria anadirla primero.

### Mientras tanto

El registro manual funciona y esta probado. No es comodo para desplegar muchos
nodos a la vez, pero es correcto.

## Configuracion posterior: implementada

Una vez registrado, el nodo se puede editar:

    PUT /api/nodes/:id
    Authorization: Bearer <JWT>

    { "name": "Sensor Entrada", "distance_threshold": 30 }

Ese endpoint hace tres cosas, no solo actualizar la base:

1. `UPDATE nodes` con `COALESCE`, asi que los campos que no se envian se
   conservan.
2. Comprueba que el nodo pertenece a un hub del tenant del usuario.
3. **Publica la configuracion al hub** por MQTT, para que la reenvie al nodo
   por ESP-NOW.

Es decir, el **downlink ya funciona**. Cambiar el umbral en la web llega al
nodo fisico sin tocar nada mas.

## Borrado

    DELETE /api/nodes/:id
    Authorization: Bearer <JWT>

Lo mismo que la edicion en cuanto a permisos: solo del propio tenant.

## Diagnostico

| Sintoma | Causa probable |
|---|---|
| El nodo mide pero no aparece en la web | es la brecha de auto-registro: falta `POST /api/nodes` |
| `mac_address debe tener formato xx:xx:xx:xx:xx:xx` | se envio sin separadores |
| `distance_threshold debe estar entre 2 y 200` | fuera de rango |
| 404 al editar un nodo | el nodo es de otro tenant |
| El nodo no recibe la config | el hub esta desconectado de MQTT |
| El nodo no se vincula | canal del router distinto del 5 |

## Que esta verificado y que no

**Verificado en el sistema desplegado:**

- Los endpoints `POST`, `PUT` y `DELETE /api/nodes` existen y validan JWT.
- `PUT` publica al hub por MQTT (nodo `Armar topic MQTT` -> `Enviar config al
  Hub`).
- La tabla `nodes` tiene los campos que se documentan.
- El hub guarda el nodo en NVS al recibir `PAIR`.

**No verificado:**

- El ciclo completo de `PUT` -> MQTT -> ESP-NOW -> nodo aplicando el umbral.
  Las piezas existen y estan conectadas en el flujo, pero no se ha probado de
  punta a punta en esta sesion.
