# Banco de pruebas de dos hubs

Cómo verificar el pairing del nodo cuando se necesita un segundo hub, sin
depender de tener dos hubs del producto. Complementa AT-012 de
[03-pruebas-aceptacion.md](specs/03-pruebas-aceptacion.md).

## Por qué existe

Con un solo hub hay comportamientos del pairing que no se pueden probar: si el
nodo distingue un hub ajeno, o si reaprende la MAC al cambiar de hub. El hub
real tampoco sirve para el caso negativo, porque siempre responde.

## El hub falso

Un ESP32 clásico (DevKit) con firmware propio que **solo** responde a los PAIR
del nodo con un ACK v1 válido. No tiene WiFi, MQTT, ni registro de nodos: es
un respondedor, no un hub.

Reutiliza `firmware/shared/espnow_protocol/` en vez de copiar el formato del
frame. Un ACK copiado a mano divergiría del contrato de
[02-interfaces.md](specs/02-interfaces.md) en cuanto cambiara.

El firmware vive fuera del repo, en `~/espnow-hub-falso/` del servidor. No se
versiona porque es material de banco, no producto.

### Montaje

    # El directorio reutiliza el protocolo compartido del repo
    set(EXTRA_COMPONENT_DIRS "$ENV{HOME}/seguimiento360-alarma/firmware/shared")

    idf.py set-target esp32
    idf.py -p /dev/ttyUSB0 flash monitor

El DevKit aparece como `ttyUSB0` con chip CH340 (`1a86:7523`), **sin serial
único**: identificarlo por `ID_MODEL=USB_Serial`, no por el número de puerto.

### Dos tropiezos al montarlo

1. **`esp_now` no es un componente** en IDF v5.2: va dentro de `esp_wifi`.
   Y `espnow_protocol` hay que declararlo en `REQUIRES` del componente `main`,
   o el include falla aunque el componente sí se compile.

2. **ESP-NOW exige registrar el destino antes de enviarle.** El hub falso
   respondía con `ESP_ERR_ESPNOW_NOT_FOUND` porque solo tenía el peer
   broadcast. Los peers concretos se registran al recibir el primer PAIR.
   Es la misma restricción que obligó al pairing real (ver "Fallo 3" de
   02-interfaces.md).

## Cómo dirigir el nodo a un hub concreto

El nodo de fábrica usa las constantes `HUB_MAC_SEED_*` compiladas. Para
apuntarlo al hub falso se compila con otro valor:

    idf.py -DS360_SEED=0x0070071c6fa4 build

Requiere un hook en `firmware/sensor-ultrasonic/main/CMakeLists.txt`, que
**no está en el repo**: se añade y se revierte para cada prueba. El hook debe
ir **después** de `idf_component_register`, porque `${COMPONENT_LIB}` aún no
existe antes.

    if(DEFINED S360_SEED)
        target_compile_definitions(${COMPONENT_LIB}
            PRIVATE "S360_SEED_OVERRIDE=${S360_SEED}")
    endif()

Y en `main.c`, `HUB_MAC_SEED_*` se envuelve en `#ifdef S360_SEED_OVERRIDE`.

`idf.py -D` define variables de CMake, **no macros de C**: usarlo directo no
llega al compilador. Conviene confirmar con
`grep S360_SEED_OVERRIDE build/compile_commands.json`.

## Qué se probó y qué no

**Probado (AT-012):** un nodo de fábrica se empareja con el hub falso y guarda
su MAC `00:70:07:1c:6f:a4` en NVS. Con el hub real silenciado y el nodo
sembrado hacia el falso, el ciclo de sustitución funciona con hardware
independiente.

**Un resultado inesperado:** con ambos hubs encendidos y el nodo vinculado al
real, el hub falso quedó **en silencio absoluto** aunque no tiene filtro
alguno. No fue el filtro `mac_es_de_este_hub()` lo que lo mantuvo fuera: fue
que **ESP-NOW entrega solo al peer registrado**. El filtro protege un caso más
estrecho —dos hubs que registren al mismo nodo, como con uno de repuesto— y
ese caso concreto **sigue sin probarse**.

## Pendiente

- Prueba negativa real del filtro: forzar al nodo a tener dos peers y
  comprobar que el hub rechaza la trama que no va dirigida a él.
