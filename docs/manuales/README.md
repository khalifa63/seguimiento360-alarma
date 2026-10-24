# Manuales de Seguimiento 360

Documentacion de los dispositivos, en dos niveles. Un tema = un documento.

## Para el usuario final

Explican como instalar y usar cada aparato, sin tecnicismos. Estan pensados
para incluirse en la caja del producto y para publicarse en el sitio web.

| Documento | Dispositivo |
|---|---|
| [hub.md](usuario/hub.md) | Hub: la caja que conecta todo |
| [sensor-ultrasonico.md](usuario/sensor-ultrasonico.md) | Sensor de movimiento por ultrasonido |

## Para quien instala y mantiene

Datos tecnicos: que hay dentro, como se comunica, que hay que saber para
diagnosticar un problema.

| Documento | Contenido |
|---|---|
| [hub.md](tecnico/hub.md) | Arquitectura, radio, MQTT, OTA, diagnostico |
| [sensor-ultrasonico.md](tecnico/sensor-ultrasonico.md) | Arquitectura, ESP-NOW, pairing, OTA |
| [protocolo-comun.md](tecnico/protocolo-comun.md) | El enlace ESP-NOW que comparten todos |

## Estado de los dispositivos

**Existen y funcionan:**

- **Hub** (ESP32-S3). Recibe de los sensores por radio y publica en la nube.
- **Sensor ultrasonico** (ESP32-S3). Detecta presencia por ultrasonido.

Los dos estan desplegados y verificados en hardware.

**Previstos, sin firmware todavia:**

En `firmware/` hay carpetas reservadas para sensor de movimiento PIR, humo,
inundacion, magnetico de puertas, temperatura, boton de panico, sirena y
rastreador de vehiculo. **Estan vacias**: son sitios reservados, no productos.

No se documentan aqui porque no existe nada que documentar. Cuando alguno tenga
firmware, se le anaden sus dos manuales.

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
