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

## Riesgos abiertos

1. **`passwd` y `acl` no están versionados ni respaldados.** Una reinstalación
   los pierde. Considerar incluirlos en el respaldo semanal.
2. **Contraseña de `s360_hub` compartida por los cuatro hubs.** Si se filtra
   una, hay que rotarla en todos y reflashear.
3. **Los hubs no usan TLS.** El listener `8883` existe pero está sin configurar;
   el tráfico interno viaja en claro por la red Docker.
4. **Rotación pendiente.** `hub_password_2026` estuvo publicado en GitHub y debe
   rotarse (ver [SECURITY.md](../../SECURITY.md)).

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
