# Contenedor `docker-init_permissions-1` desactivado

**Fecha:** 2026-10-27
**Motivo:** reinicio en bucle infinito consumiendo CPU.

---

## El problema

El contenedor se reiniciaba cada pocos segundos. Su log repetía sin parar:

    Permissions already initialized. Exiting.

Al terminar su tarea (ajustar permisos de archivos al arrancar), salía con
código 0. Pero tenía `restart: unless-stopped`, así que Docker lo volvía a
lanzar inmediatamente. Y otra vez. Sin fin.

**Reinicios acumulados: 3.201.**

Cada arranque levanta un contenedor, monta volúmenes y ejecuta el script de
permisos, para nada. Es el tipo de consumo silencioso que no se nota hasta que
se mira `docker ps -a`.

---

## Qué se hizo

    docker update --restart=no docker-init_permissions-1
    docker stop docker-init_permissions-1

**Sin borrar el contenedor.** Sigue existiendo, por si hay que relanzarlo.

---

## Por qué es seguro

`init_permissions` es un **contenedor de inicialización**: hace una tarea (dar
permisos a los ficheros del volumen) y termina. Su trabajo ya estaba hecho
—lo dice su propio log: *"Permissions already initialized"*—.

No es un servicio: no sirve peticiones ni mantiene estado.

---

## Verificación

| Comprobación | Resultado |
|---|---|
| Deja de reiniciarse | Exited (0), sin reinicio tras 30 s |
| Restart policy | `no` |
| `docker-web-1` (Dify) | Up 2 days, responde `307` |
| `docker-api-1` | Up 2 days (healthy) |
| `docker-db_postgres-1` | Up 2 days (healthy) |
| Resto del stack Dify | Los 15 contenedores arriba |
| Seguimiento 360 | nodered, mosquitto, postgres, influxdb, nginx — todos arriba |

Dify responde correctamente en `127.0.0.1:3000` y `127.0.0.1:80` con un `307`
(redirección al login, comportamiento normal).

---

## Si alguna vez hay que relanzarlo

Solo hace falta si se reconstruye el volumen de datos de Dify y hay problemas
de permisos en los ficheros:

    docker update --restart=no docker-init_permissions-1   # seguir sin bucle
    docker start docker-init_permissions-1
    docker logs docker-init_permissions-1

Al terminar saldrá con código 0 y **no se reiniciará** (la política `no` lo
impide). Si se quiere que vuelva a reiniciarse solo:

    docker update --restart=unless-stopped docker-init_permissions-1

---

## Nota

Este contenedor pertenece al stack de **Dify**, no a Seguimiento 360. Se
desactivó porque gastaba recursos del mismo servidor. No afecta a las alarmas,
al portal ni a las notificaciones push.
