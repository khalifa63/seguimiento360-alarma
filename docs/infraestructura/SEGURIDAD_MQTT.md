# Seguridad del broker MQTT

Configuración de autenticación y control de acceso de Mosquitto, con el
aislamiento multi-tenant verificado. Incluye los hallazgos corregidos el
2026-10-23 y las tareas pendientes.

## Configuración vigente

El archivo `mosquitto.conf` vive en el host y se monta en el contenedor
(ver [Rutas de archivos](RUTAS-ARCHIVOS.md)):

    /home/antonio/iot-alarma/mosquitto/config/

Directivas relevantes:

    allow_anonymous false
    password_file /mosquitto/config/passwd
    acl_file /mosquitto/config/acl

Las tres son necesarias. **Sin `acl_file`, Mosquitto autentica pero no
autoriza**: cualquier usuario válido accede a todos los tópicos.

## Usuarios

| Usuario | Uso | Acceso |
|---|---|---|
| `s360_hub` | Hub ESP32-S3 | Solo su subárbol de tenant/site |
| `nodered` | Backend Node-RED | Todos los tópicos |

`nodered` es el único cliente con visión global: procesa eventos de todos los
tenants y publica comandos. La autorización por tenant la aplica la API con el
JWT, no el broker.

Las contraseñas se generan con `openssl rand -base64 32` y se guardan en el
gestor de credenciales del proyecto. **No van en el repositorio**: `passwd` está
en `.gitignore`.

## Control de acceso

El ACL restringe al hub a su propio subárbol:

    user s360_hub
    topic readwrite seguimiento360/tenant/2/site/1/hub/+/#
    topic write seguimiento360/hub/status

Dos detalles que no son obvios:

**El tenant va escrito a mano.** El usuario `s360_hub` no codifica el tenant, así
que el patrón `%u` de Mosquitto no sirve todavía. La regla fija `tenant/2/site/1`
porque es donde operan los cuatro hubs desplegados (verificado en PostgreSQL).
**Al desplegar un hub en otro tenant hay que añadir su regla.**

**`seguimiento360/hub/status` necesita regla propia.** Ese tópico tiene dos
niveles, mientras que el subárbol del hub tiene cinco. Sin la línea `topic write`
el hub pierde el LWT y el backend nunca lo ve desconectarse.

### Migración pendiente

El formato previsto es `tenant_<tid>_site_<sid>_hub_<mac>`, que sí encaja en:

    pattern readwrite seguimiento360/+/+/+/+/%u/#

Está comentado en el ACL a la espera de reflashear el firmware con el usuario
nuevo. Hasta entonces, cada tenant exige una regla explícita.

## Verificación

Cinco pruebas contra el broker real, ejecutadas el 2026-10-23:

| # | Prueba | Resultado |
|---|---|---|
| 1 | Conexión anónima | Rechazada |
| 2 | `nodered` publica en cualquier tenant | Permitido |
| 3 | `s360_hub` publica en su tenant (2/1) | Permitido |
| 4 | `s360_hub` publica en otro tenant (1/3) | **Descartado** |
| 5 | `s360_hub` publica el LWT de estado | Permitido |
| 6 | `s360_hub` **lee** otro tenant | **Descartado** |
| 7 | `s360_hub` lee su propio tenant | Permitido |
| 8 | Contraseña incorrecta | Rechazada |

Las pruebas 4 y 6 son las que importan: confirman que el aislamiento funciona en
ambos sentidos. Mosquitto descarta los mensajes no autorizados **en silencio**,
sin devolver error al publicador, así que la única forma de comprobarlo es
suscribirse y verificar que no llega nada.

## Hallazgos corregidos

| Hallazgo | Detalle |
|---|---|
| **ACL nunca cargado** | `mosquitto.conf` no tenía `acl_file`. El archivo `acl` existía desde el 21 de agosto pero Mosquitto lo ignoraba: **no había aislamiento real**. |
| **Falta el usuario `nodered`** | El ACL lo referenciaba desde su creación, pero solo existía `s360_hub` en `passwd`. Node-RED no podía conectar y toda la ingesta de eventos fallaba. |
| **ACL permisivo** | La regla era `topic readwrite seguimiento360/#`: el hub accedía a **todos los tenants**. |
| **BOM en el ACL** | El archivo llevaba marca de orden de bytes. Mosquitto rechaza la primera línea y no arranca. |
| **Permisos incorrectos** | `passwd` y `acl` necesitan `0700` y propietario `mosquitto`. Mosquitto 2.1 avisa y las versiones futuras se negarán a cargarlos. |

## Rotación de credenciales (2026-10-23)

Las credenciales del hub estuvieron en el historial de git desde septiembre.
Se rotaron ambas.

| Credencial | Estado |
|---|---|
| MQTT (`S360_MQTT_PASSWORD`) | Rotada y en uso |
| Portal cautivo | Rotada y luego sustituida por el esquema derivado de la MAC (ver más abajo) |

La contraseña MQTT antigua **ya no autentica** contra el broker. Se verificó
explícitamente después del cambio.

### Procedimiento aplicado

El orden importa: si se cambia el broker antes de flashear, el hub queda fuera
hasta que termine el flasheo.

1. Generar las contraseñas con `openssl rand -base64 32`.
2. Escribirlas en `firmware/hub/main/secrets.h` (no versionado).
3. Compilar y flashear el hub **preservando la NVS**.
4. Actualizar el broker con `mosquitto_passwd -b`.
5. Reiniciar Mosquitto y verificar que la antigua ya no sirve.
6. Confirmar que el hub reconecta solo.

Entre los pasos 3 y 4 el hub queda sin conectar: es esperado. El firmware
reintenta con backoff y vuelve solo.

### Verificación

El hub reconectó sin intervención y quedó autenticado:

    New client connected as hub_3cdc7562c2b0 (u's360_hub')

## Portal cautivo

El hub levanta un AP para el aprovisionamiento inicial. Desde el 2026-10-23 el
SSID y la contraseña **derivan de la MAC**, no son fijos:

| Elemento | Valor | Ejemplo |
|---|---|---|
| SSID | `S360-XXXX` | `S360-C2B0` |
| Contraseña | `S360-XXXX` | `S360-C2B0` |
| URL del portal | `http://192.168.4.1` | — |

`XXXX` son los cuatro últimos dígitos hexadecimales de la MAC del hub, en
mayúsculas. Para el hub `3cdc7562c2b0` el resultado es `C2B0`.

### Por qué derivan de la MAC

Con un SSID fijo, dos hubs en la misma casa emiten la misma red y el teléfono
se conecta al de más señal, sin forma de elegir cuál se está configurando.
Con el sufijo de MAC cada equipo tiene su propia red y el instalador sabe a
cuál conectarse.

Además, el sufijo coincide con el identificador que el resto del sistema usa
para el hub: los tópicos MQTT (`hub/3cdc7562c2b0`) y la etiqueta física del
equipo.

### Recomendación de despliegue

Imprimir la MAC en la etiqueta del equipo. El instalador no memoriza nada:
lee la etiqueta, se conecta a `S360-XXXX` y entra al portal.

### Nota sobre la rotación

Estas credenciales **no se rotan** como las de MQTT: al derivarse de la MAC,
cambiar la contraseña exigiría reflashear. La protección es que la red solo
existe durante el aprovisionamiento (timeout de 10 minutos de inactividad).

Si se necesitara una contraseña distinta de la del SSID, habría que volver a
definirla en `secrets.h` y reiniciar el ciclo de rotación.

## Riesgos abiertos

1. **`passwd` y `acl` no están versionados ni respaldados.** Una reinstalación
   los pierde. Considerar incluirlos en el respaldo semanal.
2. **Contraseña de `s360_hub` compartida por los cuatro hubs.** Si se filtra
   una, hay que rotarla en todos y reflashear.
3. **Los hubs no usan TLS.** El listener `8883` existe pero está sin configurar;
   el tráfico interno viaja en claro por la red Docker.
4. **Las contraseñas antiguas siguen en el historial de git.** Rotarlas
   neutraliza su uso, pero quien tenga acceso al repositorio puede leerlas.
5. **`pg_hba.conf` usa `trust` para `127.0.0.1`.** Las conexiones locales a
   PostgreSQL no validan contraseña, así que una prueba desde dentro del
   contenedor siempre da OK aunque la credencial sea incorrecta. Verificar
   siempre desde otro contenedor.
6. **La contraseña de PostgreSQL no persiste.** El rol se corrigió con
   `ALTER USER`, que se pierde si se recrea el contenedor. Para que sea
   permanente hay que fijar `POSTGRES_PASSWORD` antes del primer arranque.


## Operación

Añadir un usuario:

    docker exec mosquitto mosquitto_passwd -b /mosquitto/config/passwd <usuario> '<password>'
    docker restart mosquitto

Aplicar un cambio de ACL:

    docker restart mosquitto
    docker logs mosquitto 2>&1 | grep -i acl
    # debe aparecer: Plugin builtin-security has registered to receive 'acl-check' events

Si aparece `Error opening acl file`, revisar BOM y permisos antes de seguir.

## Documentos relacionados

- [Rutas de archivos](RUTAS-ARCHIVOS.md): dónde vive cada configuración.
- [Flujos Node-RED](FLUJOS_NODERED.md): tópicos y estructura de los flujos.
- [Contratos de interface](../firmware/specs/02-interfaces.md): jerarquía de tópicos.
- [Seguridad](../marco-teorico/07_seguridad.md): modelo de amenazas.
