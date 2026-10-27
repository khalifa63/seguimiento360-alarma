# nginx devuelve 502 tras recrear un contenedor

**Sintoma:** el portal carga, pero todas las rutas `/api/` devuelven **502**.
El login falla. Los archivos estaticos (HTML, CSS, imagenes) siguen bien.

En los logs de nginx aparece:

    connect() failed (111: Connection refused) while connecting to upstream,
    request: "POST /api/auth/login",
    upstream: "http://172.18.0.6:1880/api/auth/login"

**Causa:** nginx guarda en memoria la IP del contenedor al que reenvia. Cuando ese
contenedor se **recrea** (`docker compose up -d --force-recreate`), Docker le
asigna otra IP. nginx sigue intentando la vieja y no hay nada escuchando ahi.

**Por que engana:** el nombre `nodered` si resuelve bien:

    docker exec nginx getent hosts nodered     # devuelve la IP NUEVA

nginx usa la IP que resolvio al arrancar; no vuelve a preguntar.

---

## Solucion

    docker restart nginx

Unos 8 segundos despues, las rutas `/api/` responden otra vez.

---

## Cuando ocurre

Cada vez que se **recrea** un contenedor al que nginx reenvia. En la practica:

- Al recrear `nodered` (por ejemplo, al cambiar una variable de entorno)
- Al recrear cualquier servicio que este detras del proxy

**No ocurre** al reiniciar con `docker restart`: en ese caso Docker conserva la IP.
Solo pasa al **recrear** el contenedor.

---

## Como comprobarlo

    # IP actual del contenedor
    docker inspect nodered --format '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}'

    # Que IP esta usando nginx (mirar el upstream del error 502)
    docker logs --tail 20 nginx 2>&1 | grep upstream

    # nginx alcanza a nodered?
    docker exec nginx wget -qO- --timeout=5 http://nodered:1880/api/overview

Un `401 Unauthorized` significa que **si** llega (la API pide token).
Un `Connection refused` confirma el problema.

---

## Detalle: `localhost` no sirve en este servidor

Al probar desde dentro del servidor, `curl http://localhost:PUERTO` devuelve
`000`. No es un fallo: en esta maquina `localhost` resuelve a IPv6 y los
servicios escuchan en IPv4.

Usar la IP explicita:

    curl http://127.0.0.1:1880
