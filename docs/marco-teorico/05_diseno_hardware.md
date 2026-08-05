# CAPÍTULO 5: DISEÑO DE HARDWARE

## 5.1 Criterios Generales de Selección

Para la Fase 1 se emplean módulos comerciales de desarrollo (no grado industrial), priorizando bajo costo, disponibilidad en el mercado y rapidez de prototipado: placas ESP32-S3 DevKit para hub y rastreador, y módulos ESP32-C3 SuperMini para nodos sensores. El diseño se documenta de modo que en Fase 2 pueda migrarse a PCB propia sin cambiar la arquitectura de firmware. Todos los nodos comparten la misma filosofía: un solo rol por dispositivo, mínimo consumo y comunicación por ESP-NOW hacia el hub, salvo el rastreador vehicular que usa 4G.

## 5.2 Hub Central ESP32-S3

### 5.2.1 Especificaciones técnicas

Microcontrolador ESP32-S3 con doble núcleo Xtensa LX7 a 240 MHz, WiFi 2,4 GHz, Bluetooth 5 (LE) y 8 MB de flash (módulo WROOM-1 o DevKit N8). El hub ejecuta simultáneamente el coordinador ESP-NOW, el cliente WiFi/MQTT sobre WebSockets, la máquina de estados de la alarma y el provisioning BLE, por lo que se requiere la variante S3 y no la C3.

### 5.2.2 Periféricos y asignación preliminar de GPIO

    GPIO48  LED RGB WS2812 (estado: WiFi, ESP-NOW, alarma)
    GPIO9   I2C SDA  (RTC DS3231)
    GPIO10  I2C SCL  (RTC DS3231)
    GPIO11  Salida sirena (MOSFET hacia relé o sirena externa 12 V)
    GPIO12  Buzzer interno (retroalimentación local)
    GPIO0   Botón físico (pairing y prueba)
    GPIO4   Tamper (microinterruptor anti-sabotaje de carcasa)

Asignación preliminar sujeta a validación en el esquemático final.

### 5.2.3 Alimentación

Entrada 5 V/2 A por USB-C proveniente de adaptador de pared, con respaldo mediante batería LiPo 3,7 V (cargador TP4056 y convertidor boost a 5 V). El respaldo dimensiona para 4 a 8 horas de autonomía ante cortes de energía, escenario típico de sabotaje. Se incluye detección de falla de AC para publicar el evento al servidor.

### 5.2.4 Consideraciones de diseño

Capacitores de desacople de 100 nF y 10 uF en la alimentación; diodo TVS y flyback en la salida de sirena por cargas inductivas; holgura de antena según diseño de referencia de Espressif; y microinterruptor de tamper que dispara alerta al abrir la carcasa.

## 5.3 Nodo de Contacto Magnético (ESP32-C3)

Sensor de apertura de puertas y ventanas mediante interruptor de lengüeta (reed switch) con resistencia de elevación de 10 k y filtrado por hardware. El nodo permanece en sueño profundo y despierta por interrupción del GPIO ante cambio de estado, transmite la trama ESP-NOW con reintentos y retorna a sueño. Incluye divisor de tensión de alta impedancia para medir nivel de batería por ADC y reportarlo en cada evento y en heartbeats periódicos.

    GPIO2   Reed switch (interrupcion y despertar de sueno profundo)
    GPIO0   Boton de pairing
    GPIO1   ADC nivel de bateria (divisor 10 M para minimo drenaje)

Alimentación: dos celdas AAA (3 V) con regulador LDO de bajo consumo. Carcasa de dos piezas (imán y electrónica) con detección de separación.

## 5.4 Nodo de Movimiento PIR (ESP32-C3)

Se emplea el sensor piroeléctrico AM312 por su operación a 3,3 V y corriente de reposo del orden de decenas de microamperios, a diferencia de módulos HC-SR501 diseñados para alimentación continua. El nodo despierta por flanco de detección, transmite y duerme. Se incorpora temporizador de reinmunidad configurable para evitar ráfagas de eventos.

    GPIO2   Salida PIR AM312 (interrupcion de despertar)
    GPIO0   Boton de pairing

Alimentación: celda 18650 o dos AAA según carcasa final.

## 5.5 Nodo Actuador de Sirena (ESP32-C3)

Actuador alimentado por fuente de 12 V que convierte a 3,3 V para el microcontrolador mediante convertidor reductor. La salida de potencia usa MOSFET de nivel lógico (IRLZ44N) que conmuta la sirena de 110 dB. El nodo permanece en escucha ESP-NOW con ciclos de sueño ligero programados y acepta comandos de activación con duración acotada y de desactivación, con confirmación al hub. La salida opcional de sirena externa del hub (5.2.2) y este nodo cubren ambos escenarios de instalación.

## 5.6 Nodos Sensores Adicionales

Humo y gas: sensor MQ-2 con comparador y umbral ajustable; por requerir precalentamiento y lectura continua, opera alimentado por 5 V con muestreo periódico y sin sueño profundo. Temperatura y humedad: sensor BME280 por I2C con muestreo cada 5 minutos y sueño profundo entre lecturas. Inundación: sonda de electrodos con transistor de detección que despierta el nodo solo ante presencia de agua, maximizando autonomía. Botón de pánico: nodo con pulsador de gran superficie, transmisión inmediata con tres reintentos y confirmación luminosa; alimentación por celda de larga duración.

## 5.7 Rastreador Vehicular (ESP32-S3 + A7670SA-FASE)

### 5.7.1 Arquitectura del módulo

El rastreador integra un ESP32-S3 como procesador principal y un módulo SIMCom A7670SA-FASE que aporta conectividad LTE y receptor GNSS, comunicados por UART a 115200 bps con comandos AT y canal de datos. Se opta por esta topología en lugar de OpenCPU para mantener un solo entorno de desarrollo (ESP-IDF) en toda la línea de productos; la migración a OpenCPU se evaluará en Fase 2 como optimización de costo.

    ESP32-S3 UART1 TX/RX   A7670SA-FASE (comandos AT y datos)
    GPIO4    Entrada ignicion (divisor y optoacoplador desde 12 V)
    GPIO5    Salida corte de motor (relevo, opcional y con condiciones de seguridad)
    GPIO48   LED de estado
    GPIO0    Boton de servicio

### 5.7.2 Alimentación y protecciones

Entrada de 9 a 24 V del vehículo con convertidor reductor a 5 V y 3,3 V, fusible automotriz en la entrada, diodos TVS contra transientes de carga y descarga de carga (load dump), y batería LiPo de respaldo con cargador integrado que permite detectar desconexión de batería principal (evento de sabotaje) y continuar reportando posición.

### 5.7.3 Antenas y montaje

Antenas LTE y GNSS externas con conector, montadas con vista al cielo y separadas de fuentes de ruido; carcasa con fijación oculta en tablero.

## 5.8 Análisis de Consumo Energético y Autonomía

Valores estimados con perfiles de operación típicos, a validar en banco de pruebas:

| Nodo | Alimentación | Corriente sueño | Pico transmisión | Autonomía estimada |
|---|---|---|---|---|
| Contacto magnético | 2xAAA 1200 mAh | 50 uA | 150 mA por 60 ms | 6 a 12 meses |
| PIR | 18650 2500 mAh | 70 uA | 150 mA por 60 ms | 4 a 8 meses |
| Inundación | 2xAAA 1200 mAh | 20 uA | 150 mA por 60 ms | 12 a 24 meses |
| Pánico | 18650 2500 mAh | 30 uA | 150 mA por 60 ms | 12 meses |
| Humo/gas | 5 V continuo | N/A | 180 mA promedio | Solo con alimentación |
| Hub | 5 V + LiPo | N/A | 240 mA con WiFi activo | Respaldo 4 a 8 h |
| Rastreador | 12 V + LiPo | Sueño ligero 2 mA | 500 mA en ráfaga 4G | Respaldo 2 a 4 h |

La estrategia de sueño profundo con despertar por interrupción y transmisión de tramas cortas por ESP-NOW es el factor determinante de la autonomía en nodos a batería.

## 5.9 Lista de Materiales y Costos Preliminares (BOM)

Costos de referencia por módulo en cantidades unitarias de componentes comerciales:

| Módulo | Componentes principales | Costo estimado |
|---|---|---|
| Hub | ESP32-S3 DevKit, DS3231, buzzer, MOSFET, TP4056, boost | USD 12 a 15 |
| Magnético | ESP32-C3 SuperMini, reed, portapilas AAA | USD 4 a 5 |
| PIR | ESP32-C3 SuperMini, AM312, portapilas | USD 5 a 6 |
| Sirena | ESP32-C3, IRLZ44N, reductor 12 V a 3,3 V, sirena 110 dB | USD 8 a 10 |
| Humo/gas | ESP32-C3, MQ-2, fuente 5 V | USD 7 a 9 |
| Temp/humedad | ESP32-C3, BME280 | USD 6 a 7 |
| Inundación | ESP32-C3, sonda | USD 4 a 5 |
| Pánico | ESP32-C3, pulsador, 18650 | USD 6 a 7 |
| Rastreador | ESP32-S3, A7670SA-FASE, porta-SIM, antenas, reductor, LiPo | USD 25 a 30 |

Estos costos permiten sostener los precios de venta definidos en el plan de negocio con márgenes superiores al 60 por ciento en kits.

## 5.10 Resumen del Capítulo

El diseño de hardware define nueve módulos sobre la familia ESP32, con un hub S3 de operación continua y respaldo, nodos C3 de mínimo consumo con sueño profundo y despertar por interrupción, y un rastreador vehicular con protecciones de grado automotriz. Las asignaciones de GPIO, los perfiles de consumo y las listas de materiales quedan documentadas como base para el esquemático final y el prototipado, y se establece la ruta de migración a PCB propia en Fase 2.
