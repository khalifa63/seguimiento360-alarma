# Cómo reactivar Gotify (si alguna vez lo necesitas)

**Estado actual: Gotify está desactivado.**

- Contenedor `gotify`: **detenido** (no se borró, sigue ahí con sus datos)
- Reinicio automático: **desactivado** (no se levanta solo al reiniciar el servidor)
- Node-RED: el cable que enviaba alertas a Gotify está **desconectado**
- Las alertas ahora van **solo por Firebase** (notificaciones push)

**Resultado:** las notificaciones llegan bien, sin el texto JSON y sin la doble notificación de antes.

---

## Por qué se desactivó

Antes cada alarma enviaba el mensaje dos veces:

1. **Firebase** → la notificación limpia (la que se ve bien)
2. **Gotify** → el mismo mensaje en bruto, que se mostraba como JSON feo:

```
{"title":"🚨 ALARMA — Garaje","body":"Sensor Ultrasónico Gar
· 4 cm · batería 95% · Casa Principal","gravedad":"critica",
"vibrate":[400,150,400,150,400,150,400,150,800],"tag":"s...
```

Como Firebase ya cumple la función, Gotify era redundante y gastaba 10 MB de RAM.

---

## Cómo reactivarlo (3 pasos)

### Paso 1: levantar el contenedor

```bash
ssh antonio@192.168.1.200
docker update --restart=always gotify
docker start gotify
docker ps --filter name=gotify
```

Debe aparecer `Up ... (healthy)`.

### Paso 2: reconectar el cable en Node-RED

El flujo está en `/data/flows.json` dentro del contenedor `nodered`.

1. Abre el editor de Node-RED (`https://seguimiento360.com/nodered/` o el puerto de tu instalación).
2. Busca el nodo **"Parsear Evento"**.
3. Verás que su salida inferior (la de abajo) está **sin conectar**.
4. Arrastra un cable desde esa salida hasta el nodo **"2. Formatear Alerta Gotify"**.
5. Pulsa **Deploy** (el botón rojo de arriba a la derecha).

### Paso 3: comprobar que funciona

Envía una alarma de prueba y mira el log de Node-RED:

```bash
docker logs nodered 2>&1 | tail -5
```

Si aparece `Enviando alerta a Gotify` y `PUSH OK`, ya está funcionando otra vez.

---

## Alternativa rápida (sin el editor visual)

Si prefieres hacerlo por consola, este script reconecta el cable:

```bash
ssh antonio@192.168.1.200
docker stop nodered
docker run --rm -v iot-alarma_nodered-data:/data node:20-alpine node -e '
const fs = require("fs");
const F = "/data/flows.json";
const fl = JSON.parse(fs.readFileSync(F, "utf8"));
const p = fl.find(n => n.id === "55aba35dd7851a86");
p.wires = [p.wires[0], ["f7dba86636e4cf52"]];
fs.writeFileSync(F, JSON.stringify(fl, null, 4), "utf8");
console.log("cable reconectado");
'
docker start nodered
```

---

## Archivos de respaldo

Antes de desconectar el cable se guardó una copia del flujo original:

```
/data/flows.json.bak-gotify-sin-cable
```

Para volver al estado anterior completo, dentro del contenedor `nodered`:

```bash
docker stop nodered
docker run --rm -v iot-alarma_nodered-data:/data node:20-alpine sh -c \
  'cp /data/flows.json.bak-gotify-sin-cable /data/flows.json'
docker start nodered
```

---

## Nota sobre el JSON en el navegador de escritorio

**Ya esta corregido.** Durante un tiempo el ordenador mostraba el JSON en bruto en
las notificaciones, mientras el movil las veia bien.

La causa era que el Service Worker del escritorio seguia siendo una version
antigua: leia el aviso con `event.data.text()` en lugar de interpretarlo como
JSON. Chrome no sustituye un Service Worker hasta que se cierran todas las
pestanas del sitio, y ademas Cloudflare lo cacheaba 4 horas.

Se resolvio con el versionado del Service Worker. Ver el apartado
"Actualizacion del Service Worker" en `docs/infraestructura/FLUJOS_NODERED.md`.
