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

## Ventana de vinculacion: varios hubs en la misma casa

**El problema.** Un nodo de fabrica no sabe a que hub pertenece: usa una
**semilla compilada** (la MAC del hub) para su primer `PAIR`, y acepta el `ACK`
de quien responda. En una casa con dos hubs —planta baja y primera— el nodo de
arriba podia acabar vinculado al hub de abajo: le bastaba con estar en alcance.

Lo mismo al reves: todos los nodos de fabrica comparten la semilla, asi que un
hub respondia a cualquier nodo nuevo que lo oyera.

### Como se resuelve

**El hub solo adopta nodos desconocidos mientras la ventana esta abierta.**
Cerrada —el estado normal— ignora cualquier `PAIR` que no sea de un nodo ya
vinculado.

Asi el hub de la planta baja no roba el nodo de la primera: lo oye y lo
rechaza.

### Como se abre

Dos vias, porque el instalador puede estar en dos situaciones distintas:

| Via | Cuando usarla | Como |
|---|---|---|
| **MQTT** | el hub ya tiene red y el instalador esta con el movil | `{"action":"pairing"}` al topic `command` |
| **Boton fisico** | el hub aun no tiene red | pulsar el BOOT (GPIO 0) |

La ventana dura **120 segundos** (`PAIR_WINDOW_SEC`) y se puede reabrir
pulsando otra vez: cada apertura reinicia el plazo.

### Como se sabe que esta abierta

**El LED parpadea** mientras la ventana esta abierta. Es la unica senal visible
de que el hub esta escuchando.

Al cerrarse, el LED vuelve a su estado normal. Ojo con esto: **el LED lo
comparten la ventana y el indicador de alarma**, asi que la ventana solo lo
toca mientras esta abierta y al cerrar lo devuelve al estado que le toca
(encendido si la alarma esta armada).

### Procedimiento de instalacion

1. Abrir la ventana: en la app, o pulsando el boton del hub.
2. Comprobar que el LED parpadea.
3. Encender el nodo.
4. En unos segundos queda vinculado y el LED deja de parpadear.

**El orden importa:** hay que abrir la ventana **antes** de encender el nodo. Si
se enciende primero, el nodo insiste cada vez con mas espera y hay que esperar a
su siguiente intento.

### Que NO resuelve

**No cierra el caso de un vecino vinculando a la vez.** Si abres tu ventana y el
vecino tiene un nodo de fabrica mandando `PAIR` en ese momento, tu hub podria
adoptarlo.

Reduce el problema de "siempre" a "solo durante la ventana", que es una mejora
grande, pero no lo elimina. Cerrarlo del todo exige un **token compartido** en
el `PAIR`: el hub solo adoptaria nodos con el codigo correcto. Es otro cambio y
exige grabar ese codigo en cada nodo, paso de fabricacion que hoy no existe.

### Rechazo visible

Cuando la ventana esta cerrada y llega un `PAIR` desconocido, el hub lo
registra:

    PAIR de a0:f2:62:f1:d0:f0 rechazado: la ventana de vinculacion esta cerrada.
    Abrala desde la app o pulsando el boton del hub.

Un silencio dejaria al instalador sin pista de por que no vincula.

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

## Cuando hay varios hubs en la casa

Hasta la version 6.1.0 el hub adoptaba **cualquier** nodo desconocido que
pidiera entrar. En una casa con dos hubs —planta alta y planta baja— el nodo
recien sacado de la caja se vinculaba al hub que antes le respondiera, no al que
el instalador queria. Los nodos de fabrica comparten la misma semilla de
emparejamiento, asi que los dos hubs eran igual de validos para el nodo.

### Solucion: ventana de vinculacion

El hub solo adopta nodos desconocidos si la ventana esta abierta. Cerrada es el
estado normal, y en ese estado ignora cualquier `PAIR` que no venga de un nodo ya
vinculado.

Se abre de dos maneras:

| Via | Requisito | Como |
|---|---|---|
| MQTT | el hub ya tiene red | `{"action":"pairing"}` al topic `command` |
| Boton | ninguno | pulsar **BOOT** (GPIO 0) |

Dura **120 s**. Volver a pedirla reinicia el plazo.

### Como se sabe

- El LED parpadea **lento** (700 ms), frente al parpadeo rapido de error (120 ms).
- El hub publica el estado en `.../hub/{mac}/pairing`:

      {"event":"PAIRING_open","seconds":120,"mac":"3cdc7562c2b0"}
      {"event":"PAIRING_closed","seconds":0,"mac":"3cdc7562c2b0"}

### Sustituir un sensor averiado

Con la ventana cerrada, un nodo de reemplazo no puede entrar. Hay que abrirla. Y
si el sensor viejo sigue dado de alta, el hub no lo distingue del nuevo: se
olvida primero.

    {"action":"unpair","mac":"a0:f2:62:f1:d0:f0"}

Respuesta del hub por consola: `Nodo a0:f2:62:f1:d0:f0 olvidado. Quedan 0/20.`

Limite del registro: **20 nodos**.
