# Rotacion de credenciales (2026-10-27)

Se filtraron varias credenciales en el repositorio publico de GitHub. Este
documento deja constancia de que se roto cada una, como, y que queda pendiente.

---

## Motivo

Al preparar un `git push` se reviso el contenido y aparecieron tres secretos:

| Secreto | Donde estaba | Gravedad |
|---|---|---|
| Contrasena de PostgreSQL | `settings.js` del repo, literal | **Era la real de produccion** |
| `jwtSecret` | `settings.js` del repo, literal | Firma los JWT de los usuarios |
| Clave privada VAPID | `env` de una pestana del flujo | Permite enviar push suplantando el sistema |

Los tres se quitaron antes de subir, pero **quedan en el historial de commits
anteriores**. Por eso hay que rotarlos: cambiar la credencial es lo unico que
neutraliza lo ya publicado.

**Aclaracion sobre el `jwtSecret`:** el que se filtro (`d6e1c387...`) **no era el
de produccion** (`1d776787...`). Estaba obsoleto, asi que no supuso riesgo real.

---

## 1. Contrasena de PostgreSQL — ROTADA

Generada con `openssl rand -hex 24` (48 caracteres).

Se actualizo en **tres sitios**, no solo en el `.env`:

| Archivo | Variable |
|---|---|
| `.env` | `POSTGRES_PASSWORD` |
| `traccar.xml` | `database.password` |
| Base de datos | `ALTER USER admin WITH PASSWORD ...` |

**Ojo con Traccar:** usa la misma contrasena y el mismo usuario, en la base
`traccar`. Si se cambia solo el `.env`, Traccar deja de conectar.

Despues se recrearon `nodered` y `traccar` para que tomaran la nueva.

### Trampa al verificar

`pg_hba.conf` tiene `trust` para conexiones locales dentro del contenedor:

    local   all   all                    trust
    host    all   all   127.0.0.1/32     trust
    host    all   all   all              scram-sha-256

Con `trust`, PostgreSQL acepta **cualquier** contrasena desde dentro del
contenedor. Probar la clave vieja ahi da un falso positivo: tambien funciona una
inventada, y hasta ninguna.

La regla que si exige contrasena es la ultima, que aplica a conexiones desde
**otros contenedores**. Ahi es donde hay que comprobar.

---

## 2. jwtSecret — ROTADO

Generado con `openssl rand -hex 32` (64 caracteres).

Al hacerlo aparecieron **dos problemas de configuracion**:

**a) El `settings.js` ignoraba la variable de entorno.** Tenia el secreto escrito
en duro, y era **distinto** al del `.env`. Cambiar solo el `.env` no habria
hecho nada. Se cambio a:

    jwtSecret: process.env.JWT_SECRET || '<respaldo>'

**b) `JWT_SECRET` estaba duplicado en el `docker-compose.yml`**, dos veces en el
mismo servicio `nodered`.

### Efecto

**Los usuarios tienen que volver a entrar al portal.** Los JWT firmados con el
secreto anterior dejan de valer. No afecta a las notificaciones push, que son
independientes del JWT.

---

## 3. Claves VAPID — PENDIENTE

**Sin rotar todavia.** La clave privada filtrada permite enviar notificaciones
push suplantando al sistema.

**Por que no se hizo:** al regenerarlas, **todos los dispositivos pierden la
suscripcion** y hay que reactivar las notificaciones en cada uno.

**Como se haria:**

1. Generar el par nuevo:

       npx web-push generate-vapid-keys --json

2. Guardar el resultado en `/data/vapid.json` dentro del contenedor `nodered`.

3. Actualizar la clave **publica** en el portal: la etiqueta
   `<meta name="vapid-public">` de `index.html`, `dashboard.html`, `nodos.html`,
   `eventos.html` y `diagnostico.html`.

4. Subir la version de cache (`comun.js?v=...`) para que los navegadores no usen
   la clave antigua.

5. Avisar a los usuarios: cada uno debe abrir el portal y pulsar el boton azul
   del aviso **"Activa las notificaciones de alarma"**. El aviso aparece solo
   cuando el dispositivo no esta suscrito.

**Antes de hacerlo**, conviene borrar los tokens viejos:

    DELETE FROM user_fcm_tokens;

Los reintentos contra tokens muertos devuelven 403; el nodo ya detecta 404 y 410
y avisa en el log, pero no los borra solo.

---

## Resumen

| Credencial | Estado |
|---|---|
| Contrasena PostgreSQL | Rotada y verificada |
| jwtSecret | Rotado y verificado |
| Claves VAPID | **Pendiente** |

---

## Pendiente de fondo: el historial de Git

Los secretos **siguen en los commits anteriores** del repositorio publico. La
rotacion neutraliza su uso, pero el historial los conserva.

Para limpiarlo haria falta `git filter-repo --replace-text`. Es invasivo:
reescribe todos los commits e invalida los clones existentes. Con las
credenciales ya rotadas, no es urgente.
