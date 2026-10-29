# Prototipo Reed Switch con ESP32 DevKit V1

Firmware ESP-IDF que monitorea el estado de un **Reed Switch magnético de 28 mm** conectado a un **ESP32 DevKit V1** y lo reporta por el monitor serial **solo cuando el estado cambia**: `Switch Cerrado` (imán cerca) o `Switch Abierto` (imán lejos). Es el prototipo del sensor de apertura (puertas/ventanas) del proyecto **seguimiento360-alarma** y sigue la convención del prototipo hermano [Sensor_de_movimiento_PIR_HC_SR501](../Sensor_de_movimiento_PIR_HC_SR501/).

- Ubicación en el servidor: `antonio@server:~/seguimiento360-alarma/pruebas/reed_switch_prototype/`
- Estado: compilado, flasheado y verificado en hardware (ESP-IDF v5.2, DevKit V1 en `/dev/ttyUSB0`); reporta solo los cambios de estado.
- Siguiente paso natural del proyecto: reportar el estado por la red siguiendo el frame v1 de [docs/firmware/specs/02-interfaces.md](../../docs/firmware/specs/02-interfaces.md) (ver [Mejoras futuras](#mejoras-futuras)).

## Descripción

El Reed Switch es un interruptor cuyos contactos se cierran o abren al acercar un imán. Este prototipo usa un Reed **Normalmente Abierto (N.O.)** con la **resistencia pull-up interna** del ESP32 activada por firmware, de modo que el pin siempre tiene un nivel eléctrico definido (sin estados flotantes):

| Condición física | Contactos del Reed (N.O.) | Nivel en GPIO4 | Mensaje en el monitor serial |
| --- | --- | --- | --- |
| Imán cerca (p. ej., puerta cerrada) | Cerrados (conducen a GND) | LOW | `Switch Cerrado` |
| Imán lejos (p. ej., puerta abierta) | Abiertos (no conducen) | HIGH (por pull-up a 3.3 V) | `Switch Abierto` |

El firmware no necesita bibliotecas externas: solo componentes estándar de ESP-IDF (`driver/gpio`, FreeRTOS y `log`).

## Hardware

| Componente | Cantidad | Notas |
| --- | --- | --- |
| ESP32 DevKit V1 | 1 | Target `esp32`, puerto serial USB-UART (CP2102 u otro). |
| Reed Switch de 28 mm | 1 | Normalmente Abierto (N.O.); 2 terminales **sin polaridad**. |
| Imán | 1 | El del propio sensor de puerta o un imán pequeño de neodimio; activa el Reed a menos de ~1–2 cm del tubo. |
| Cables dupont hembra-hembra | 2 | Para conectar el Reed al DevKit. |
| Resistencia 10 k Ohm (opcional) | 1 | Solo si se prefiere pull-up externa (ver abajo). |

## Diagrama de conexiones

```text
   ESP32 DevKit V1                    Reed Switch de 28 mm (N.O.)
  ┌────────────────┐
  │           GPIO4│──────────────● Terminal 1 ┐
  │                │                           ├─ Reed Switch
  │            GND │──────────────● Terminal 2 ┘  (sin polaridad)
  └────────────────┘

  Pull-up: la resistencia pull-up INTERNA del GPIO4 (~45 k Ohm) se activa por
  firmware. Para este prototipo NO se requiere resistencia externa.
```

Notas de cableado:

- Los dos terminales del Reed Switch no tienen polaridad: se pueden conectar en cualquier sentido.
- La serigrafía de la placa puede variar según la revisión del DevKit (GPIO4 vs D4); usa el pin etiquetado `GPIO4`/`D4` y cualquier `GND`.
- Mantén los cables cortos (<30 cm) para minimizar ruido; la pull-up interna es débil (~45 k Ohm).

**Opcional — pull-up externa de 10 k Ohm** (para cables largos o entornos ruidosos):

```text
  3V3 ──[ R 10 kΩ ]──┬── GPIO4 ──── Terminal 1 (Reed Switch)
                     │
                 (nodo común)
 Terminal 2 (Reed Switch) ──── GND
```

Si usas pull-up externa, cambia `.pull_up_en` a `GPIO_PULLUP_DISABLE` en `main/main.c`.

## Estructura del proyecto

```text
seguimiento360-alarma/pruebas/reed_switch_prototype/
├── CMakeLists.txt          # Proyecto raíz de ESP-IDF (idf.py)
├── compile_and_run.sh      # Script auxiliar: compila, flashea y abre el monitor
├── .gitignore              # Excluye build/, sdkconfig y build.log (reglas del repo)
├── main/
│   ├── CMakeLists.txt      # Registro del componente "main" (SRCS main.c)
│   └── main.c              # Firmware: lectura del Reed Switch
└── README.md               # Este documento
```

Nota: el código fuente vive en `main/main.c` porque así lo exige la estructura de componentes de ESP-IDF (`idf.py build` compila lo que registra `main/CMakeLists.txt`). Es la misma estructura de los proyectos de `firmware/` del repo.

## Cómo funciona el firmware

`main/main.c`, por pasos:

1. **Inicialización del GPIO** (`reed_switch_init`): configura GPIO4 como `GPIO_MODE_INPUT` con `GPIO_PULLUP_ENABLE` mediante `gpio_config()`. Con inicializadores designados, los campos no listados quedan en 0 (sin pull-down, sin interrupciones). `ESP_ERROR_CHECK` aborta si la configuración falla.
2. **Estado inicial**: lee el nivel de GPIO4 y lo imprime **una sola vez** como referencia (`Estado inicial: ...`).
3. **Bucle con detección de cambios**: sondea `gpio_get_level(REED_SWITCH_GPIO)` cada 500 ms y **solo imprime cuando el nivel difiere del último reportado** (una transición = un mensaje): `Switch Cerrado` (LOW) o `Switch Abierto` (HIGH), con `ESP_LOGI` (tag `REED_SWITCH`).
4. **Retardo de 500 ms**: `vTaskDelay(pdMS_TO_TICKS(REED_READ_PERIOD_MS))` cede la CPU al scheduler de FreeRTOS (no es busy-wait) y actúa como filtro del rebote mecánico del Reed.

Parámetros configurables en `main/main.c`:

| Macro | Valor por defecto | Significado |
| --- | --- | --- |
| `REED_SWITCH_GPIO` | `GPIO_NUM_4` | Pin donde va conectado el Reed Switch. |
| `REED_READ_PERIOD_MS` | `500` | Retardo entre lecturas, en milisegundos. |
| `TAG` | `"REED_SWITCH"` | Etiqueta de los mensajes de log. |

## Preparar el entorno en Linux (una sola vez)

> **En `antonio@server` ya está instalado** en `~/esp-idf`: actívalo con `. ~/esp-idf/export.sh` y salta esta sección. La instalación siguiente es solo para un equipo nuevo.

En el servidor Ubuntu (`antonio@server`), instala ESP-IDF v5.x si aún no está disponible (el repo ya trabaja con ESP-IDF v5.x; verifica con `echo $IDF_PATH` o buscando `~/esp/esp-idf`):

```bash
# Prerrequisitos (Ubuntu 22.04)
sudo apt-get update && sudo apt-get install -y \
  git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build \
  ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# ESP-IDF v5.x
mkdir -p ~/esp && cd ~/esp
git clone -b v5.2.2 --recursive https://github.com/espressif/esp-idf.git
./esp-idf/install.sh

# Activar el entorno en cada terminal nueva
. ~/esp/esp-idf/export.sh

# (Opcional) alias permanente
echo "alias get_idf='. ~/esp/esp-idf/export.sh'" >> ~/.bashrc
```

## Copiar el proyecto al servidor

Si trabajaste el prototipo en otro equipo, súbelo al servidor con `scp`:

```bash
scp -r reed_switch_prototype antonio@server:~/seguimiento360-alarma/pruebas/
```

## Compilar, cargar y monitorear

En el servidor, con el entorno de ESP-IDF activado:

```bash
cd ~/seguimiento360-alarma/pruebas/reed_switch_prototype

# Opción A: script auxiliar (compila, detecta puerto, flashea y abre el monitor)
chmod +x compile_and_run.sh
./compile_and_run.sh

# Opción B: manual
idf.py set-target esp32     # solo la primera vez
idf.py build                # compilar
idf.py -p /dev/ttyUSB0 flash   # cargar por el puerto serial
idf.py -p /dev/ttyUSB0 monitor # abrir monitor serial (salir con Ctrl+])
```

Atajos: `idf.py -p /dev/ttyUSB0 flash monitor` ejecuta ambas cosas seguidas.

Notas sobre el puerto y los permisos:

- Si `usermod` no está aplicado, `flash` fallará con permiso denegado: `sudo usermod -aG dialout $USER` y vuelve a iniciar sesión.
- Si el DevKit expone `/dev/ttyACM0` en lugar de `/dev/ttyUSB0` (según el chip USB-UART), usa ese puerto; verifica con `ls /dev/ttyUSB* /dev/ttyACM*` o `dmesg | tail` tras conectar la placa.
- Si el flasheo no arranca, mantén pulsado el botón `BOOT` de la placa durante el inicio de la carga.

## Salida esperada en el monitor serial

```text
I (325)  REED_SWITCH: Prototipo Reed Switch iniciado (GPIO 4)
I (330)  REED_SWITCH: Estado inicial: Switch Abierto
         (silencio: el estado no cambia)
I (15230) REED_SWITCH: Switch Cerrado    ← imán acercado al Reed
         (silencio mientras el imán siga cerca)
I (21540) REED_SWITCH: Switch Abierto    ← imán retirado
```

Prueba rápida: acerca y aleja el imán del tubo del Reed (a menos de ~1–2 cm); cada transición genera exactamente un mensaje. Si el serial está en silencio, es porque el estado no ha cambiado.

## Problemas conocidos y notas

- **Reed Normalmente Cerrado (N.C.)**: si el Reed es N.C. en reposo, la lógica se invierte (imán lejos = LOW). Corrige intercambiando los mensajes en el `if` de `app_main`, o documenta el tipo de sensor usado.
- **Rebote mecánico (contact bounce)**: un Reed puede rebotar unos milisegundos al conmutar. El sondeo cada 500 ms lo mitiga para reportar estado, pero no sirve para contar aperturas precisas; para eso usa interrupciones con debounce (ver Mejoras futuras).
- **Pines a evitar**: GPIO0, GPIO2, GPIO5, GPIO12 y GPIO15 son pines de *strapping* y pueden impedir el arranque si quedan en nivel indebido; GPIO6–GPIO11 pertenecen a la flash interna; GPIO34–GPIO39 son de entrada únicamente y **no tienen pull-ups internas** (un Reed ahí quedaría flotante).
- **Pull-up interna débil (~45 k Ohm)**: con cables largos o ruido eléctrico el pin puede leer errático. Usa pull-up externa de 10 k Ohm (y desactiva la interna en el firmware).
- **Reporte solo en cambios**: el serial permanece en silencio mientras el estado no cambie (comportamiento esperado, no un fallo). El sondeo de 500 ms filtra rebotes, pero para contar aperturas con precisión de milisegundos conviene pasar a interrupciones con debounce.
- **Logs con `ESP_LOGI`**: si prefieres salida plana con `printf` (como en otros ejemplos), sustituye `ESP_LOGI(TAG, ...)` por `printf("...\n")`; el comportamiento es idéntico.

## Mejoras futuras

- [ ] Detección por interrupciones (`GPIO_INTR_NEGEDGE`/`POSEEDGE`) con debounce por timer, en lugar de sondeo.
- [ ] Reportar el evento de apertura/cierre por ESP-NOW al hub con el frame v1 de [docs/firmware/specs/02-interfaces.md](../../docs/firmware/specs/02-interfaces.md) (siguiendo [docs/firmware/specs/00-gobierno-sdd.md](../../docs/firmware/specs/00-gobierno-sdd.md) si se promueve a `firmware/`).
- [ ] Añadir LED indicador de estado (p. ej., GPIO2 con lógica invertida) para prueba sin monitor.
- [ ] Registro de eventos con timestamp (NVS o InfluxDB vía hub) para auditoría.
- [ ] Soporte de múltiples Reed Switch en un array de GPIOs.
- [ ] Modo deep sleep con despertar por GPIO (`esp_sleep_enable_gpio_wakeup`) para nodos a batería.

## Referencias

- [ESP-IDF: Periféricos GPIO (API `driver/gpio`)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html)
- [ESP-IDF: `idf.py` e `idf.py monitor`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-monitor.html)
- [ESP-IDF: instalación en Linux y macOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-macos-setup.html)
- Strapping del ESP32: [ESP32 datasheet, sección Strapping Pins](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
