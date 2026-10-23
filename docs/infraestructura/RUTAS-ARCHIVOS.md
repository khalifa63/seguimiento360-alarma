# Rutas de archivos y volúmenes

Mapa de dónde vive físicamente cada archivo de configuración y cada dato
persistente de la plataforma, con sus tres perspectivas: host, contenedor y
repositorio. Sirve para editar la configuración correcta, montar copias de
seguridad y evitar confusiones entre archivos reales y plantillas.

Verificado el 2026-10-23 contra el servidor de producción.

## Las tres perspectivas

| Perspectiva | Qué es | Cómo se accede |
|---|---|---|
| **Host** | El archivo real en el disco del servidor | Sesion SSH en el servidor |
| **Contenedor** | La ruta que ve el proceso dentro de Docker | `docker exec <contenedor> ...` |
| **Repositorio** | Lo que está versionado en git | `~/seguimiento360-alarma` |

La regla general: **el archivo vivo está en el host**, el contenedor lo ve por un montaje, y el repositorio solo guarda plantillas sin secretos.

## Node-RED

| Contexto | Ruta |
|---|---|
| Host (archivo vivo) | `/var/lib/docker/volumes/iot-alarma_nodered-data/_data/settings.js` |
| Contenedor | `/data/settings.js` |
| Repositorio (plantilla) | `nodered/data/settings.js.example` |

El volumen `iot-alarma_nodered-data` se monta en `/data`. El archivo real
**requiere `sudo`** para leerse desde el host, porque los volúmenes de Docker
pertenecen a `root`.

**El `settings.js` real no existe en el repositorio.** En `nodered/data/` solo vive `settings.js.example`; el archivo con credenciales nunca se versiona.

### Editar el settings.js

```bash
sudo nano /var/lib/docker/volumes/iot-alarma_nodered-data/_data/settings.js
docker restart nodered
```

Antes de reiniciar, verificar que haya **exactamente un**
`functionGlobalContext` (duplicarlo provoca un crash-loop de Node-RED):

```bash
docker exec nodered grep -c "functionGlobalContext:" /data/settings.js
```

## PostgreSQL

| Contexto | Ruta |
|---|---|
| Host | `/home/antonio/iot-alarma/postgres/data` |
| Contenedor | `/var/lib/postgresql/data` |

Es un **bind mount**, no un volumen nombrado. Los datos se respaldan con
`pg_dumpall`, no copiando el directorio en caliente. El esquema versionado está
en `backend/database/schema.sql`.

## Mosquitto

| Contexto | Host | Contenedor |
|---|---|---|
| Configuración | `/home/antonio/iot-alarma/mosquitto/config` | `/mosquitto/config` |
| Datos | `/var/lib/docker/volumes/iot-alarma_mosquitto-data/_data` | `/mosquitto/data` |
| Logs | `/var/lib/docker/volumes/iot-alarma_mosquitto-log/_data` | `/mosquitto/log` |

La configuración es un bind mount, así que se edita directo:

```bash
ls -la /home/antonio/iot-alarma/mosquitto/config/
# mosquitto.conf, acl, passwd
docker restart mosquitto
```

El directorio contiene `mosquitto.conf`, `acl` y `passwd`. La autenticación está activa: el broker **rechaza conexiones anónimas**.

## InfluxDB

| Contexto | Host | Contenedor |
|---|---|---|
| Configuración | `/var/lib/docker/volumes/5c65464617170921a7ee17eec475ef87773c80e61a34587abf2da7030ff0ce4/_data` | `/etc/influxdb2` |
| Datos | `/var/lib/docker/volumes/iot-alarma_influxdb-data/_data` | `/var/lib/influxdb2` |

El volumen de configuración tiene un **nombre generado por Docker**, no un
nombre legible. Es la consecuencia de no haber fijado el nombre en el compose.
Conviene renombrarlo en el futuro para que el backup sea predecible.

## Grafana

| Contexto | Host | Contenedor |
|---|---|---|
| Datos | `/var/lib/docker/volumes/iot-alarma_grafana-data/_data` | `/var/lib/grafana` |

## Traccar

| Contexto | Host | Contenedor |
|---|---|---|
| Configuración | `/home/antonio/iot-alarma/traccar.xml` | `/opt/traccar/conf/traccar.xml` |
| Datos | `/var/lib/docker/volumes/iot-alarma_traccar-data/_data` | `/opt/traccar/data` |
| Logs | `/var/lib/docker/volumes/iot-alarma_traccar-logs/_data` | `/opt/traccar/logs` |

## Gotify

| Contexto | Host | Contenedor |
|---|---|---|
| Datos | `/var/lib/docker/volumes/iot-alarma_gotify-data/_data` | `/app/data` |

## Nginx

| Contexto | Host | Contenedor |
|---|---|---|
| Configuración del sitio | `/home/antonio/iot-alarma/nginx/default.conf` | `/etc/nginx/conf.d/default.conf` |
| Credenciales básicas | `/home/antonio/iot-alarma/nginx/.htpasswd` | `/etc/nginx/.htpasswd` |
| Contenido web | `/home/antonio/iot-alarma/website` | `/usr/share/nginx/html` |

La PWA se despliega en `/home/antonio/iot-alarma/website/app/` mediante
`scripts/deploy-pwa.sh`. Un cambio de frontend sin ejecutar ese script no llega
al usuario.

## Uptime Kuma

| Contexto | Host | Contenedor |
|---|---|---|
| Datos | `/var/lib/docker/volumes/iot-alarma_uptime-kuma-data/_data` | `/app/data` |

## Otros archivos del host

| Archivo | Ruta | Contenido |
|---|---|---|
| Variables de entorno | `/home/antonio/iot-alarma/.env` | Contraseñas de Postgres, secretos JWT y de credenciales |
| Composición de servicios | `/home/antonio/iot-alarma/docker-compose.yml` | Definición de contenedores, redes y montajes |

El `.env` tiene permisos `600` y pertenece a `antonio`. Es el archivo más
sensible del servidor: concentra las credenciales que consumen los contenedores.

## Volúmenes con nombre duplicado

`docker volume ls` muestra pares que parecen duplicados:

```text
iot-alarma_grafana-data      iot-alarma_grafana_data
iot-alarma_influxdb-data     iot-alarma_influxdb_data
iot-alarma_nodered-data      iot-alarma_nodered_data
```

Los que usan **guion** (`-data`) son los que los contenedores tienen montados
hoy. Los que usan **guion bajo** (`_data`) son residuos de una composición
anterior y no están en uso. **No borrarlos sin verificar antes** qué contenedor
los referencia.

## Respaldos

El respaldo semanal cubre volúmenes, `.env` y `pg_dumpall`. Al restaurar,
respetar la distinción de este documento: los volúmenes van a
`/var/lib/docker/volumes/`, los bind mounts a `/home/antonio/iot-alarma/`.

Las copias puntuales de firmware se guardan en `/home/antonio/backups-flash/`
con el patrón `<placa>-<AAAAMMDD>-<HHMMSS>.bin`.

## Documentos relacionados

- [Estado actual](ESTADO_ACTUAL.md): diferencias respecto al diseño original.
- [Flujos de Node-RED](FLUJOS_NODERED.md): estructura de los flujos y del settings.
