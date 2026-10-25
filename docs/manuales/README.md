# Manuales de Seguimiento 360

Documentacion de los dispositivos, en dos niveles. Un tema = un documento.

## Para el usuario final

Explican como instalar y usar cada aparato, sin tecnicismos. Estan pensados
para incluirse en la caja del producto y para publicarse en el sitio web.

| Documento | Contenido |
|---|---|
| [hub.md](usuario/hub.md) | Hub: la caja que conecta todo |
| [sensor-ultrasonico.md](usuario/sensor-ultrasonico.md) | Sensor de movimiento por ultrasonido |
| [portal.md](usuario/portal.md) | La cuenta web: registrarse, entrar y anadir dispositivos |

## Para quien instala y mantiene

Datos tecnicos: que hay dentro, como se comunica, que hay que saber para
diagnosticar un problema.

| Documento | Contenido |
|---|---|
| [hub.md](tecnico/hub.md) | Arquitectura, radio, MQTT, OTA, diagnostico |
| [sensor-ultrasonico.md](tecnico/sensor-ultrasonico.md) | Arquitectura, ESP-NOW, pairing, OTA |
| [protocolo-comun.md](tecnico/protocolo-comun.md) | El enlace ESP-NOW que comparten todos |
| [vinculacion.md](tecnico/vinculacion.md) | Como se vincula un nodo: fisico y logico |
| [portal.md](tecnico/portal.md) | API, registro, sesion, informes y eventos |

## Estado de los dispositivos

**Existen y funcionan:**

- **Hub** (ESP32-S3). Recibe de los sensores por radio y publica en la nube.
- **Sensor ultrasonico** (ESP32-S3). Detecta presencia por ultrasonido.

Los dos estan desplegados y verificados en hardware.

**Previstos, sin firmware todavia:**

En `firmware/` hay carpetas reservadas para sensor de movimiento PIR, humo,
inundacion, magnetico de puertas, temperatura, boton de panico, sirena y
rastreador de vehiculo. **Estan vacias**: son sitios reservados, no productos.

No tienen manual porque no existe nada que documentar. Cuando alguno tenga
firmware, se le anaden sus dos manuales.

### Lo que el sistema ya espera de ellos

Aunque el firmware no exista, el contrato **ya reserva un codigo de evento**
para cada tipo de nodo futuro:

| Codigo | Que seria |
|---|---|
| `ALRM` | deteccion generica (el ultrasonico lo usa) |
| `GAS` | fuga de gas |
| `SMOKE` | humo |
| `FIRE` | fuego |
| `PANIC` | boton de panico |
| `MOV` | movimiento (PIR) |
| `DOOR` | apertura de puerta o ventana |
| `SEIS` | sismo o vibracion |
| `SOS` | emergencia |
| `HUM` | humedad o inundacion |

La nube **ya los trata como criticos**: cuando llegue uno, lo notifica por
push ademas de guardarlo. Es decir, un nodo nuevo que emita uno de estos
codigos funciona sin tocar el backend.

Eso importa para el manual de cada nodo futuro: el codigo que emita tiene que
estar en esta lista, o la alerta se guardara pero no avisara a nadie.

Los codigos salen de `docs/firmware/specs/02-interfaces.md`, no de suposiciones.

## Como se mantiene esto

Los datos de los manuales salen del codigo, no de suposiciones. En concreto:

| Dato | De donde sale |
|---|---|
| Nombre de la red de configuracion | `firmware/hub/main/main.c` |
| Umbral de deteccion | `DISTANCE_THRESHOLD_CM` |
| Alcance del sensor | `MAX_DISTANCE_CM` |
| Numero maximo de sensores | `MAX_NODES` |
| Como se comporta la luz | los `gpio_set_level` y sus constantes |

Si un manual dice algo que el codigo no respalda, el manual sobra. Ya paso con
una cifra de alcance que no tenia fuente: se quito.

## Publicacion en el sitio web

Los manuales se convierten y se publican en el sitio con un solo comando:

    scripts/manuales/generar.sh --html --publicar       # HTML al sitio
    scripts/manuales/generar.sh                          # HTML y PDF
    scripts/manuales/generar.sh --pdf                    # solo PDF

Quedan accesibles en:

    https://seguimiento360.com/manuales/

### Que genera

| Formato | Para que |
|---|---|
| HTML | verlo en el sitio web o abrirlo con doble clic |
| PDF | imprimirlo y meterlo en la caja del producto |

Ambos salen de los mismos `.md`. **El script no inventa contenido**: si un
manual cambia, se vuelve a ejecutar y listo.

El HTML es autonomo: lleva el estilo incrustado y se abre sin dependencias. El
PDF se genera con LaTeX (calidad de imprenta).

### El estilo

Todo el aspecto sale de `scripts/manuales/estilo.css`, un solo archivo. Si
cambia la marca, se cambia ahi y se regenera todo.

### Trampa: la cache de Cloudflare

**Cloudflare cachea por ruta durante 4 horas** (`cache-control: max-age=14400`).
Si se publica un archivo en una ruta que antes no existia, Cloudflare puede
seguir sirviendo la respuesta vieja.

Paso de verdad: al publicar `/manuales/hub.pdf` por primera vez, la peticion
devolvia 200 con 16904 bytes y contenido de la pagina de inicio. El archivo
local tenia 38215 bytes y era un PDF valido.

El motivo es que `location /` de nginx lleva:

    try_files $uri $uri/ /index.html;

Cualquier ruta que no exista **devuelve la portada con codigo 200**, no un 404.
Cloudflare cacheo esa respuesta y la mantuvo.

**Como comprobarlo:**

    # sin pasar por Cloudflare
    curl -H "Host: seguimiento360.com" http://localhost:8088/manuales/hub.pdf

    # o forzando una URL distinta
    curl "https://seguimiento360.com/manuales/hub.pdf?x=1"

**Como evitarlo:** publicar primero los archivos, y despues enlazarlos. Si ya
se sirvio una version mala, hay que purgar la cache desde el panel de
Cloudflare.

Conviene **verificar el contenido publicado**, no solo el codigo de respuesta:
un 200 puede estar sirviendo otra cosa.

    # comparar tamano y primeros bytes con el original
    curl -sS -o /tmp/p.pdf https://seguimiento360.com/manuales/hub.pdf
    mutool info /tmp/p.pdf | grep Pages
