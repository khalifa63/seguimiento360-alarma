# Sensor ultrasonico — Manual tecnico

Nodo de deteccion de presencia por ultrasonido. Para la version divulgativa,
ver [usuario/sensor-ultrasonico.md](../usuario/sensor-ultrasonico.md).

## Que es

Nodo de borde que mide distancia cada 5 segundos y avisa al hub cuando algo
entra en su umbral. **No tiene pila de red propia**: toda su comunicacion es
ESP-NOW.

    [HC-SR04] --> [ESP32-S3] --ESP-NOW--> [HUB]

## Hardware

| Elemento | Valor |
|---|---|
| MCU | ESP32-S3 |
| Flash | 4 MB |
| Sensor | HC-SR04 (ultrasonido) |
| TRIG | GPIO 5 |
| ECHO | GPIO 4 |
| LED indicador | GPIO 2 |
| Consola | USB nativo |

## Parametros de operacion

| Parametro | Valor | Constante |
|---|---|---|
| Intervalo de medicion | 5 s | `MEASUREMENT_INTERVAL_SEC` |
| Umbral de deteccion | 50 cm | `DISTANCE_THRESHOLD_CM` |
| Distancia maxima valida | 400 cm | `MAX_DISTANCE_CM` |
| Heartbeat | 600 s | `HEARTBEAT_INTERVAL_SEC` |
| Comprobacion del enlace | 10 s | `LINK_CHECK_SEC` |
| ACK perdidos antes de desvincular | 6 | `ACK_LOST_MAX` |

El umbral de 50 cm es lo que dispara `ALRM`. Por encima, el nodo sigue midiendo
en silencio; cada 10 minutos manda un `HEAR` para que el hub sepa que sigue
vivo.

**Nota sobre `MAX_DISTANCE_CM`:** es el limite por encima del cual una lectura
se considera invalida, no el alcance real del sensor. El HC-SR04 da lecturas
fiables hasta unos 4 metros en condiciones normales.

## Particiones

| Nombre | Offset | Tamano | Uso |
|---|---|---|---|
| `nvs` | 0x9000 | 0x6000 | MAC del hub aprendida |
| `otadata` | 0xf000 | 0x2000 | que particion arranca |
| `phy_init` | 0x11000 | 0x1000 | calibracion de radio |
| `ota_0` | 0x20000 | 0x100000 | firmware A (1 MB) |
| `ota_1` | 0x120000 | 0x100000 | firmware B (1 MB) |

La `nvs` **conserva el offset de la version anterior sin OTA**. Es donde el
nodo guarda la MAC del hub: moverla obligaria a re-emparejar cada nodo ya
instalado.

## Vinculacion con el hub

El nodo no lleva la MAC del hub compilada. La aprende:

1. De fabrica no tiene nada en NVS. Usa una **semilla compilada** para el primer
   `PAIR`: la MAC **STA** del hub.
2. El hub responde con un `ACK` cuyo `src_mac` lleva la suya.
3. El nodo la guarda en NVS (namespace `s360node`, clave `hub_mac`) y deja de
   usar la semilla.

**La semilla apunta a la STA, no al SoftAP.** Apuntaba al SoftAP al principio, y
cuando el hub paso a modo STA puro (sin portal) esa MAC dejo de existir: el
envio fallaba con `ESP_ERR_ESPNOW_NOT_FOUND`. La STA existe en los dos modos.

### Recuperacion si el hub desaparece

Si el hub deja de responder, el nodo no se queda enviando al vacio:

1. Comprueba el enlace cada 10 s con un `PAIR`.
2. A los 6 sin respuesta (60 s), borra la MAC de NVS y vuelve a buscar.
3. El intervalo de reintento **crece con los fallos**: 30 s, 60 s... hasta
   10 minutos, con jitter para no sincronizar varios nodos.
4. Cuando el hub reaparece, se re-vincula solo y el contador vuelve a cero.

Sin esto, un hub sustituido dejaba al nodo enviando a una MAC inexistente, sin
sintoma visible.

## Comportamiento del LED

| Estado | LED | Consola |
|---|---|---|
| Sin vincular | Parpadea cada 2 s | "Sin hub en NVS..." |
| Vinculado | Apagado | "Hub vinculado en NVS: ..." |
| Detecta algo | 5 destellos rapidos | "OBJETO DETECTADO" |
| Hub ausente 60 s | Vuelve a parpadear | "El hub no responde..." |

## Actualizacion por el aire

El nodo **no tiene pila de red**: no puede descargar nada por si mismo. El hub
le manda por ESP-NOW las credenciales WiFi y la URL, y entonces se asocia y
descarga.

| Aspecto | Valor |
|---|---|
| Transporte | ESP-NOW del hub al nodo, luego HTTPS |
| Verificacion | firma RSA-3072 obligatoria |
| Rollback | activo, se confirma al arrancar |
| Version | `1.0.0-ota` |

**Por que no viaja la imagen por ESP-NOW:** el payload util de una trama son
229 bytes, asi que 700 KB son unos 3075 fragmentos. ESP-NOW no tiene
retransmision por fragmento: con una perdida del 0,1% por fragmento, la
probabilidad de recibir el binario entero es del 4,6%.

### Comprobacion de canal

El nodo **escanea el AP antes de asociarse** y aborta si no esta en el canal 5.
Asociarse mueve el radio al canal del AP y romperia el enlace con el hub.

## Diagnostico

| Sintoma | Causa probable |
|---|---|
| LED parpadea siempre | no encuentra el hub, o no hay pairing |
| `Error al enviar: 1` | `ESP_ERR_ESPNOW_NOT_FOUND`: peer no registrado |
| El hub no ve los PAIR | canal del router distinto del 5 |
| Mide siempre lo mismo | el sensor apunta a algo fijo, o esta tapado |
| Distancia 0 | lectura invalida: fuera de rango o eco perdido |

## Que NO tiene

- No se conecta al WiFi por si solo: depende del hub para el OTA.
- No guarda mediciones: lo que no llega al hub se pierde.
- No tiene bateria.
- No distingue tipos de objeto: solo dice que hay algo a X centimetros.
