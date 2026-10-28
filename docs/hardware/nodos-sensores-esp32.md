# Tabla de Nodos Sensores y Módulos para Sistema de Alarmas Basado en ESP32

**Fecha de elaboración:** 27 de octubre de 2026
**Rol:** Ingeniero de hardware — integración de sensores con ESP32

**Contenido:**
- Tabla principal de sensores (17 nodos) + cobertura de aplicaciones + desarrollo futuro
- **Tabla A** — Módulos de comunicación 4G/LTE
- **Tabla B** — Módulos LoRa
- **Tabla C** — Módulos GPS/GNSS
- **Tabla D** — Cargadores de batería de respaldo (UPS)
- **Tabla E** — Nodo de botón de pánico
- **Tabla F** — Nodo sirena
- **Tabla G** — Nodo de video
- Combinaciones recomendadas por escenario de alarma

---

## Advertencia de trazabilidad (leer antes de usar la tabla)

La especificación original exige precios de "los últimos 3 meses" y proveedores con
calificación verificable ≥4.5/5. **No pude verificar celdas de precio ni calificaciones
de tienda una por una**, por estas razones concretas:

1. **AliExpress/Alibaba renderizan precios vía JavaScript** y varían por región, cuenta,
   cupones y cantidad. Una consulta HTTP devuelve la estructura de la página, no el precio
   final que verás tú. Cualquier cifra "exacta" que yo escribiera sería inventada.
2. **Las calificaciones de tienda no son auditables desde fuera.** El "4.5/5" es además
   una métrica engañosa en estos mercados (ver sección *Sobre el criterio 4.5/5*).

Por lo tanto aplico la restricción **"No inventar precios o proveedores"** de forma estricta:

- La columna **Costo** contiene **rangos de precio de calle documentados** por una fuente
  pública que declara verificación en **julio de 2026** (dentro de tu ventana de 3 meses),
  no precios de listado individuales.
- La columna **Proveedor** nombra **fabricantes y tiendas oficiales** cuya existencia es
  verificable (Hi-Link, Winsen, DFRobot, Seeed), no vendedores anónimos con una calificación
  que no puedo comprobar.
- Donde no hay fuente, la celda dice **`No verificado`** en lugar de un número.

**Fuente principal de precios e interfaces:** [atomic14.com — ESP32 sensor modules compared](https://www.atomic14.com/esp32/sensors/)
(datos comunitarios/vendor, verificados julio 2026; 40 sensores con interfaz, riesgo de clon y precio).
**Fuente de riesgo de falsificación:** [atomic14.com — The fake sensor problem](https://www.atomic14.com/esp32/sensors/fake-sensors/).

---

## Tabla principal

| Sensor | Procesador | Módulo Requerido | Interfaz | Nivel Lógico | Costo Unitario (USD) | Proveedor Confiable | Otro Hardware | Aplicación | Notas |
|---|---|---|---|---|---|---|---|---|---|
| **Contacto magnético (puertas/ventanas/cajones)** | ESP32-WROOM-32E | MC-38 reed switch | GPIO (digital) | 3.3 V (contacto seco) | 0.30 – 0.80 | Tiendas de componentes genéricas; [DIYables (4-pack)](https://diyables.io/products/door-sensor) | Resistencia pull-up 10 kΩ + 100 nF (debounce) | Puertas, ventanas, cajones | Base del sistema. Contacto seco → sin riesgo de nivel lógico. Usar interrupción GPIO con wake-up de deep sleep. |
| **Presencia humana mmWave (radar)** | ESP32-C6-WROOM-1 | HLK-LD2410B/C | UART | 5 V alimentación, **TX a 3.3 V** | 3 – 6 | **Tienda oficial Hi-Link** (evitar listados anónimos) | Divisor o level shifter en RX del módulo; fuente 5 V estable | Habitaciones, áreas completas | **Tecnología emergente real.** Detecta personas inmóviles (respiración), superior a PIR. El firmware varía por lote y ha roto ESPHome antes: anotar versión. |
| **Presencia/posicionamiento multizona** | ESP32-C6-WROOM-1 | HLK-LD2450 | UART | 5 V alimentación, 3.3 V datos | 6 – 10 | **Tienda oficial Hi-Link** | Fuente 5 V dedicada | Áreas completas | Radar de seguimiento: reporta X/Y de hasta 3 objetivos. Ideal para "¿hay alguien en el área?" con zonas lógicas. |
| **Movimiento PIR (bajo consumo)** | ESP32-C3-MINI-1 | AM312 (mini PIR) | GPIO (digital) | 3.3 V nativo | 1 – 2 | Listados genéricos reputados | Ninguno (salida digital directa) | Habitaciones, pasillos | **Apto para batería.** El AM312 funciona a 3.3 V (el HC-SR501 clásico es de 5 V y consume más). Usar como disparador "instant-on" junto al mmWave. |
| **Vibración / impacto (cajones y vehículos)** | ESP32-C3-MINI-1 | SW-420 + comparador LM393 | GPIO (digital) + analog | 3.3 V – 5 V | 0.80 – 1.50 | [Seeed Studio (Grove SW-420)](https://www.seeedstudio.com/Grove-Vibration-Sensor-SW-420.html), listados con LM393 | Ninguno (LM393 da salida digital limpia) | Cajones, vehículos | Salida digital por LM393, sensibilidad ajustable por potenciómetro. Para impacto vehicular real, ver fila de acelerómetro. |
| **Aceleración / impacto vehicular** | ESP32-C6-WROOM-1 | MPU6050 (GY-521) | I2C | 3.3 V | 1 – 2 | Listados genéricos | Pull-ups I2C 4.7 kΩ | Vehículos | **Silicio genuino EOL: todo GY-521 es clon o die reciclado.** Sirve para eventos de impacto, no para orientación precisa. Para detección de remolque/choque es adecuado. |
| **Distancia láser ToF (ocupación de cajón)** | ESP32-C3-MINI-1 | VL53L0X (GY-VL53L0XV2) | I2C @ 0x29 | 3.3 V (módulos aceptan 5 V) | 1 – 3 | Listados reputados — **riesgo de clon bajo** | Cambio de dirección I2C si hay varios (todos en 0x29, usar pin XSHUT) | Cajones, nivel de contenedores | Silencioso, preciso a escala milimétrica hasta ~2 m. **Limitación real:** el alcance cae con superficies oscuras/brillantes y luz solar directa. |
| **Distancia láser ToF (mayor alcance)** | ESP32-C6-WROOM-1 | VL53L1X | I2C | 3.3 V | 3 – 5 | Listados reputados | Igual que VL53L0X (conflicto de dirección) | Áreas completas, vehículos | Llega a ~4 m. **Requiere componente externo en ESPHome** (no nativo) — considerar costo de desarrollo. |
| **Distancia ultrasónica (exterior/vehículo)** | ESP32-C3-MINI-1 | JSN-SR04T (transductor impermeable) | GPIO (trigger/echo) | **5 V — requiere divisor en ECHO** | 4 – 6 | Listados reputados | **Divisor de tensión obligatorio** en ECHO (5 V→3.3 V) y fuente 5 V | Vehículos, áreas exteriores | El único de la lista pensado para intemperie. Sonda separada del PCB, ideal para parachoques o depósito. |
| **Gas combustible / humo (cualitativo)** | ESP32-WROOM-32E | MQ-2 | Analog (ADC1) + calentador 5 V | 5 V, calentador 150–200 mA | 1 – 2 | Listados genéricos | **Divisor 10 kΩ/20 kΩ** hacia ADC; fuente 5 V capaz de 200 mA | Áreas completas (cocina, garaje) | ⚠️ **Corrección al ejemplo del prompt: el MQ-2 NO es un detector de humo certificado.** Es un sensor de gas combustible (GLP, metano, propano) con respuesta cruzada al humo. Calibración real: nadie la hace; lecturas cualitativas. **Para seguridad contra incendios usar un detector certificado, nunca un módulo de $1.** El calentador puede reiniciar el ESP32 por caída de tensión. |
| **Calidad de aire / VOC (moderno, calibrado)** | ESP32-C3-MINI-1 | Sensirion SGP41 | I2C | 3.3 V | 4 – 8 | Distribuidores autorizados (Mouser/DigiKey) o listados reputados | Pull-ups I2C | Habitaciones, áreas completas | **Reemplazo recomendado del MQ-2** para VOC: calibrado, compensado, I2C, sin calentador de 200 mA. |
| **Calidad de aire / VOC (económico)** | ESP32-C3-MINI-1 | ENS160 + AHT21 (combo) | I2C | 3.3 V | 3 – 5 | Listados reputados | Pull-ups I2C | Habitaciones | Combo con temperatura/humedad para compensación. **Ojo: su "eCO2" es una estimación por VOC, no CO2 real.** |
| **CO2 real (NDIR, para ocupación)** | ESP32-C6-WROOM-1 | Sensirion SCD41 | I2C | 3.3 V | 9 – 16 | Distribuidores autorizados | Pull-ups I2C | Habitaciones, áreas completas | **Único CO2 genuino de la tabla** (cámara óptica NDIR). Riesgo de clon bajo. Su deriva requiere disciplina de calibración. No usar ENS160/MQ para decidir ventilación. |
| **Temperatura / humedad ambiental** | ESP32-C3-MINI-1 | Sensirion SHT41 (o SHT40/SHT45) | I2C | 3.3 V | 1 – 3 | Distribuidores autorizados o listados reputados | Pull-ups I2C | Habitaciones | **Apto para batería.** Prácticamente sin riesgo de clon (a diferencia del BME280, del que abundan falsificaciones). Buen sensor de referencia del sistema. |
| **Presión atmosférica / altitud** | ESP32-C3-MINI-1 | Bosch BMP280 | I2C | 3.3 V | 1 – 2 | Listados reputados | Pull-ups I2C | Áreas completas, vehículos | ⚠️ **Corrección al ejemplo del prompt: el BMP280 mide presión barométrica/altitud, NO presión de contacto.** Para "presión" como detector de intrusión se necesita un sensor de fuerza o alfombrilla (ver notas finales). Ojo: muchos listados "BME280" envían un BMP280 (verificar registro 0xD0: 0x60=BME280, 0x58=BMP280). |
| **Luz / sabotaje (detección de tapado)** | ESP32-C3-MINI-1 | BH1750 (GY-302) | I2C | 3.3 V | 1 – 2 | Listados reputados | Pull-ups I2C | Habitaciones, áreas completas | **No es un sensor de intrusión primario.** Su valor real en una alarma: detectar que alguien **tapó o manipuló** otro sensor (salto brusco a 0 lux). Anti-sabotaje. |
| **Peso / carga (cajones y contenedores)** | ESP32-WROOM-32E | HX711 + celda de carga | GPIO (bit-bang) | 3.3 V | 1 – 4 (HX711) + 2–10 (celda) | Listados reputados | **Fuente 3.3 V limpia obligatoria** | Cajones, contenedores | Detecta sustracción de contenido por cambio de peso. Los clones del HX711 fijan la tasa a 10 Hz y usan reguladores ruidosos: reconfigurar a 80 Hz y alimentar con 3.3 V limpio. |
| **Inclinación / apertura de puerta de garaje** | ESP32-C3-MINI-1 | MPU6050 o SW-520D (tilt) | I2C o GPIO | 3.3 V | 0.50 – 2 | Listados reputados | Pull-up si es SW-520D | Puertas, vehículos | Alternativa de bajo costo al reed switch donde no se puede montar un imán enfrentado. |

---

## Cobertura de aplicaciones (requisito de la especificación)

| Aplicación | Filas que la cubren |
|---|---|
| **Cajones** | MC-38 · SW-420 · VL53L0X · HX711 |
| **Ventanas** | MC-38 |
| **Puertas** | MC-38 · SW-520D (inclinación) |
| **Habitaciones** | LD2410B · AM312 · SGP41 · ENS160 · SCD41 · SHT41 · BH1750 |
| **Áreas completas** | LD2450 · VL53L1X · MQ-2 · SGP41 · SCD41 · BMP280 · BH1750 |
| **Vehículos** | SW-420 · MPU6050 · VL53L1X · JSN-SR04T · BMP280 · SW-520D |

**17 tipos de sensores** — supera el mínimo de 10. Las seis aplicaciones están cubiertas por
al menos una fila dedicada.

---

## Sensores propuestos para desarrollo futuro (no existen como producto integrado)

| Sensor | Procesador | Módulo Requerido | Costo | Proveedor | Otro Hardware | Notas |
|---|---|---|---|---|---|---|
| **Alfombrilla de presión para detección de pisadas** | ESP32-C3-MINI-1 | FSR 402/406 + MCP3008 (ADC externo) | FSR: 5–12 · MCP3008: 1–2 | Listados reputados | MCP3008 por SPI — **el ADC interno del ESP32 es no lineal y ruidoso para FSR** | **Desarrollo futuro.** No existe un módulo "alarma de pisada" listo para ESP32. Requiere ensamblaje propio: FSR bajo alfombrilla + ADC externo + umbral por software. Buena idea para perímetro interior. |
| **Detección de humo con cámara térmica de bajo costo** | ESP32-S3-WROOM-1 | MLX90640 (matriz térmica 32×24) | 35 – 60 | Listados reputados (riesgo de clon medio) | PSRAM **obligatoria** (S3-WROOM-1 con 16 MB PSRAM) | **Desarrollo futuro.** Detectaría un foco de calor antes que el humo. Costo 10× el resto del sistema; el procesamiento de imagen térmica exige PSRAM y consume energía. |
| **Contacto de puerta por cosecha de energía (sin batería)** | ESP32-C6 (con radio Thread/Zigbee) | Módulo de *energy harvesting* piezoeléctrico (p. ej. familia ZF/Aura) | 8 – 20 | Distribuidores autorizados | Supercondensador + PMIC | **Desarrollo futuro / especulativo.** El movimiento de la puerta genera la energía del reporte; elimina el cambio de batería en nodos inaccesibles. **No verifiqué disponibilidad ni integración con ESP32** — tratar como línea de investigación, no como compra. |

---

## Sobre el criterio "≥4.5/5 de calificación"

La especificación pide evitar proveedores con calificación menor a 4.5/5. **Ese umbral es
estructuralmente defectuoso en AliExpress/Alibaba** y conviene saber por qué antes de confiar en él:

1. **El mercado está sesgado hacia arriba.** La mayoría de tiendas establecidas superan el
   95% de feedback positivo; el filtro de 4.5 no excluye casi nada y da una falsa sensación
   de verificación.
2. **La calificación no mide el riesgo que importa.** El riesgo real en este dominio es el
   **componente falsificado**, y ese riesgo es *independiente* de la reputación de la tienda:
   - **DS18B20 impermeable:** prácticamente el 100% de las sondas en AliExpress/eBay llevan
     chips clonados (investigación de Chris Petrich sobre 1.000+ sondas de 70+ vendedores).
   - **"BME280" que es un BMP280:** sustitución de die por ~1 USD de diferencia — común en
     tiendas con excelente calificación.
   - **MH-Z19 clonado:** los de PCB negra leen bajo en un sensor de seguridad.

**Criterio recomendado en su lugar** — combinación de señales verificables por ti:

| Señal | Umbral sugerido |
|---|---|
| Feedback positivo | ≥97% (no 4.5/5) |
| Volumen de transacciones | >1.000 pedidos |
| Antigüedad de tienda | ≥2 años |
| Tipo de vendedor | Fabricante o tienda oficial (Hi-Link, Winsen, DFRobot, Seeed) > revendedor anónimo |
| Riesgo de clon del componente | Consultar la [tabla de riesgo de clon](https://www.atomic14.com/esp32/sensors/) — priorizar "low clone risk" |
| Sanidad de precio | Un precio de un tercio del resto responde una pregunta que no hiciste |

---

## Notas de integración con ESP32

1. **ADC:** usar siempre pines **ADC1** (GPIO32–39). **ADC2 pelea con el Wi-Fi.** El ADC del
   ESP32 es no lineal en los extremos y requiere calibración en software.
2. **Nivel lógico 5 V:** el ESP32 es de 3.3 V y **no es tolerante a 5 V** en GPIO. Todo módulo
   de 5 V con salida de datos (JSN-SR04T, MQ-2, LD2410 en su pin de datos) necesita
   **divisor de tensión o level shifter**.
3. **Direcciones I2C:** VL53L0X y VL53L1X comparten la dirección 0x29. Para varios en el mismo
   bus hay que usar el pin **XSHUT** y reasignar direcciones en el arranque.
4. **Presupuesto de corriente:** el calentador del MQ-2 (150–200 mA) y los radares mmWave
   pueden hundir una fuente débil y provocar reinicios misteriosos. Fuente de 5 V con margen.
5. **Batería:** los nodos viables con deep sleep son MC-38, AM312, SHT41, VL53L0X y SW-420.
   LD2410/LD2450, MQ-2, SCD41 y HX711 **no** son buenos candidatos a batería.
6. **Cifrado del enlace:** en un sistema de alarma, ninguno de estos módulos aporta seguridad
   por sí mismo. El transporte (Wi-Fi/Thread) debe ir cifrado y autenticado en el ESP32.

---

# Módulos de Comunicación, Posicionamiento y Energía

Estas cuatro familias son **transversales**: no son "un nodo sensor" más, sino la capa que
conecta, ubica y alimenta a los nodos anteriores. Por eso van en tablas separadas.

**Fuentes de esta sección:**
[ESPboards — ESP32 SIM modules](https://www.espboards.dev/blog/best-sim-gsm-modules/) (comparativa con precios por módulo) ·
[Microcontrollers Lab — Best 4G LTE Modules 2026](https://microcontrollerslab.com/best-4g-lte-modules-microcontrollers-embedded/) ·
[Microcontrollers Lab — Best LoRa Modules 2026](https://microcontrollerslab.com/best-lora-modules-for-long-range-iot-15-uart-spi-lorawan-and-raspberry-pi-modules-compared-2026-buying-guide/) (verificado octubre 2026) ·
[Microcontrollers Lab — Best GPS Modules](https://microcontrollerslab.com/best-gps-modules-for-arduino-esp32-buying-guide/) ·
[Microcontrollers Lab — Best LiPo Charger Modules](https://microcontrollerslab.com/best-lipo-battery-charger-modules-buying-guide/) (actualizado julio 2026) ·
[Haruki's Meshtastic — LoRa Boards Comparison](https://harukitoreda.github.io/Meshtastic-Experiments/LoRa-Boards-Comparison-Table) (precios verificados 1–5 agosto 2026).

---

## Tabla A — Módulos de comunicación 4G/LTE

| Módulo | Procesador | Tecnología | Interfaz | Voltaje | Costo Unitario (USD) | Proveedor Confiable | Hardware Adicional | Notas |
|---|---|---|---|---|---|---|---|---|
| **SIM7600G-H** | ESP32-WROOM-32E | LTE Cat-4 (150/50 Mbps) + GNSS | UART (AT) + USB | 3.4–4.2 V | 32.50 (módulo) · ~55–70 (HAT) | Waveshare · SIMCom oficial | **Fuente 5 V / 2–3 A** + antena LTE externa + antena GNSS | Máxima velocidad. Picos de transmisión ~2 A. **Sobredimensionado para alarmas** salvo que necesites video. Incluye GNSS integrado. |
| **SIM7600G (all-in-one)** | ESP32 integrado | LTE Cat-4 + GNSS | — | 5 V | — ver LILYGO T-SIM7600G-H | LILYGO | Antena LTE + antena GNSS + batería 18650 | **La opción todo-en-uno.** ESP32 + LTE Cat-4 + GNSS en una PCB. Para una alarma con panel propio es la ruta más corta. |
| **A7670C/E/SA** | ESP32-C6-WROOM-1 | LTE Cat-1 (10/5 Mbps) | UART (AT) | 3.4–4.2 V | 31.16 | SIMCom · LILYGO (T-A7670G R2) | Fuente 5 V / 2 A + antena LTE | **El punto dulce para alarmas.** Cat-1 sobra para telemetría y SMS. Variante **G** es multibanda global — elegirla si el equipo viaja. |
| **SIM7080G** | ESP32-C6-WROOM-1 | LTE Cat-M1 / NB-IoT + GNSS | UART (AT) | 2.7–4.8 V | 26.91 | Waveshare · SIMCom oficial | Antena LTE + antena GNSS | **La única opción apta para batería de verdad.** Corrientes de reposo en microamperios. **Pero:** Cat-M/NB-IoT solo funcionan donde el operador los tenga activos — verificar cobertura *antes* de comprar. |
| **SIM7000G** | ESP32-C3-MINI-1 | LTE Cat-M1 / NB-IoT + GNSS | UART (AT) | 3.0–4.3 V | 15.00 | Botletics (shield) · SIMCom | Antena LTE + GNSS | Alternativa más económica al SIM7080G con la misma advertencia de cobertura Cat-M/NB-IoT. |
| **SIM800L** | ESP32-C3-MINI-1 | GSM/GPRS 2G | UART (AT) | 3.4–4.4 V | 6.00 | Listados reputados | **Regulador 3.7→4.0 V dedicado** + fuente capaz de 2 A | ⚠️ **2G está siendo apagado en muchos países.** Solo tiene sentido donde la red 2G siga viva. Muy barato y muy documentado, pero es tecnología en extinción: no recomendado para un sistema nuevo. |
| **SIM868** | ESP32-C3-MINI-1 | GSM/GPRS + GNSS | UART (AT) | 3.4–4.4 V (GSM) / 2.9–4.4 V (GNSS) | 11.22 | SIMCom · listados reputados | Antena GSM + antena GNSS | Combo 2G + GPS. Misma advertencia de obsolescencia 2G que el SIM800L. |

### Advertencias críticas sobre 4G

1. **Bloqueo regional por bandas.** Un módulo fabricado para un continente puede no registrarse
   en tu operador. Comprar siempre la variante **"G"** (global: SIM7600**G**, A7670**G**,
   EG25-**G**) salvo que conozcas las bandas locales.
2. **Cat-M1 y NB-IoT no son universales.** Solo funcionan donde el operador los haya activado.
   En Latinoamérica la cobertura es desigual — **verificar con el operador antes de invertir** en
   un SIM7080G, que es la mejor opción técnica pero la más frágil en disponibilidad de red.
3. **Picos de corriente.** Los módulos Cat-4 pueden demandar ~2 A en el arranque de transmisión.
   Una fuente de 1 A provoca reinicios del ESP32 que parecen bugs de firmware. Usar 5 V/2–3 A.
4. **SIM de datos, no de voz.** Se necesita un plan IoT/M2M. Casi ningún módulo de estos hace
   voz; solo algunas variantes del SIM7600 añaden audio.
5. **La 2G es una trampa barata.** El SIM800L cuesta 6 USD y tiene miles de tutoriales, pero
   comprar uno hoy para un sistema de alarma nuevo es construir sobre una red que están apagando.

---

## Tabla B — Módulos LoRa

| Módulo | Procesador | Chip / Banda | Interfaz | Voltaje | Costo Unitario (USD) | Proveedor Confiable | Hardware Adicional | Notas |
|---|---|---|---|---|---|---|---|---|
| **REYAX RYLR998** | ESP32-C3-MINI-1 | Semtech LoRa · 868/915 MHz | UART (AT) | 3.3 V | ~15–20 | REYAX · distribuidores autorizados | Antena **incluida** | **Mejor opción general.** Certificado **FCC/CE/IC** — importa si el producto se comercializa. ~220 valoraciones, 4.6★. AT por UART: enlace operativo en minutos. **No es LoRaWAN.** |
| **REYAX RYLR993 Lite** | ESP32-C3-MINI-1 | LoRa SoC · 868/915 | UART (AT) | 3.3 V | ~18–25 | REYAX | Antena + **gateway LoRaWAN** | **LoRaWAN sin escribir firmware.** Pila LoRaWAN sobre AT. Requiere gateway y servidor de red (p. ej. TTN). Menos reseñas (~25, 4.1★). |
| **EBYTE E32-900T20D** | ESP32-C3-MINI-1 | SX1276 · 868/915 | UART (transparente) | 3.3 V | ~8–14 | **EBYTE oficial** | Antena + level shifter si el host es 5 V | Enlace punto a punto **transparente**: lo que entra al UART sale por el aire. El más simple para un enlace fijo nodo→central. |
| **Adafruit RFM95W Breakout** | ESP32-WROOM-32E | SX1276 | SPI | **3–5 V (con level shifting)** | ~20 | Adafruit | Antena + headers a soldar | **El más seguro con lógica 5 V** y el mejor documentado (librerías Arduino/CircuitPython). Chip SX1276 más antiguo. |
| **HopeRF RFM95W (desnudo)** | ESP32-WROOM-32E | SX1276-class · 915 | SPI (desnudo) | 3.3 V | ~4–6 (pack) | HopeRF · distribuidores | **Todo:** antena, regulador, adaptación de nivel, PCB | Para **PCB propio**. Sin regulador ni level shifting: no apto para protoboard sin circuitería adicional. |
| **Waveshare SX1262 HAT** | ESP32-C6-WROOM-1 | SX1262 · 915 | HAT (UART) | 3.3 V | ~20–28 | Waveshare | Antena | **SX1262 es la generación nueva:** menos consumo y mejor recepción que SX1276. Formato HAT pensado para Raspberry Pi, pero el UART sirve para ESP32. |
| **Heltec WiFi LoRa 32 V3.2** | ESP32-S3FN8 integrado | SX1262 · 470–510 / 863–928 | — | USB-C | **17.90** (precio verificado ago-2026) | **Heltec oficial** | Antena + batería LiPo (conector JST) | **Todo-en-uno con OLED 0.96".** El nodo LoRa más económico y completo para una alarma. TX 21 dBm, sensibilidad −136 dBm. |
| **Heltec Wireless Tracker V1.1** | ESP32-S3FN8 integrado | SX1262 + **GNSS UC6580** | — | USB-C | **22.90** (verificado ago-2026) | **Heltec oficial** | Antena LoRa + antena GNSS | **Combo LoRa + GPS en una placa.** Si el nodo es móvil (vehículo, activo rastreable) resuelve dos tablas en un solo producto. |
| **LILYGO T-Beam** | ESP32-D0WDQ6 integrado | SX1276/78 · 433/868/915/923 | — | Micro-USB | **30.77** (verificado ago-2026) | LILYGO oficial | Antenas + **batería 18650** | Diseñado para vehículos: caja, GNSS NEO-6M/M8N, ranura 18650. Referencia de Meshtastic. |

### Advertencias críticas sobre LoRa

1. **La frecuencia es una decisión legal, no técnica.** **915 MHz en América**, **868 MHz en
   Europa**, 433 MHz solo donde esté permitido. **Ambos extremos del enlace deben coincidir.**
   Comprar la variante regional correcta.
2. **Los rangos de "5–10 km" son línea de vista, con antena buena y velocidad mínima.** En ciudad
   con edificios y árboles, esperar **unos cientos de metros a un par de kilómetros**. Elevar la
   antena aporta más que subir la potencia.
3. **Familias distintas no se hablan entre sí** sin trabajo extra: firmware AT de REYAX, firmware
   transparente de EBYTE y SX127x/SX126x desnudo son incompatibles de fábrica. **Elegir una familia
   y mantenerla en todo el sistema.**
4. **Casi todos son de lógica 3.3 V** y se dañan con un host de 5 V sin adaptación.
5. **LoRa punto a punto ≠ LoRaWAN.** LoRaWAN añade gateway y servidor de red (TTN), con las
   ventajas de gestión y las desventajas de dependencia de infraestructura. Una alarma doméstica
   normalmente quiere **punto a punto**; una red de sensores urbanos, LoRaWAN.

**Recomendación para este proyecto:** usar una familia **punto a punto** (EBYTE E32 o REYAX
RYLR998) para el enlace nodos→central. Aporta lo que el Wi-Fi no puede: **seguir funcionando
cuando se corta la luz y el router**, que en un sistema de alarma es exactamente el escenario
que importa.

---

## Tabla C — Módulos GPS/GNSS

| Módulo | Procesador | Chipset | Constelaciones | Interfaz | Voltaje | Costo Unitario (USD) | Proveedor Confiable | Hardware Adicional | Notas |
|---|---|---|---|---|---|---|---|---|---|
| **ATGM336H** | ESP32-C3-MINI-1 | AT6558 (Zhongke Micro) | **GPS + BDS + GLONASS + Galileo + QZSS + SBAS** | UART | 3.3 V | ~5–9 (pack de 2) | Teyleten · listados reputados | Antena incluida | **Mejor relación valor/consumo.** Multiconstelación, **menos consumo que un NEO-6M** — ideal para nodos a batería. ⚠️ Usa protocolo **CASIC**, no UBX: **u-center no funciona** con él. TinyGPS++ sí. |
| **u-blox NEO-6M (GY-NEO6MV2)** | ESP32-C3-MINI-1 | u-blox 6 | GPS solo | UART | 3.3 V / 5 V | ~4–8 | HiLetgo · listados reputados | Antena cerámica + pila de respaldo (incluidas) | El clásico de todos los tutoriales. **GPS solo**, fix en frío lento (30 s–varios min). **Clones muy comunes** en estas placas. Aceptable para aprender, flojo bajo árboles. |
| **Beitian BN-880** | ESP32-C6-WROOM-1 | u-blox M8 (M8030) | **GPS + GLONASS** concurrentes (BDS/Galileo configurable) | UART + **I2C (brújula)** | 3.3 V / 5 V | ~18–25 | Beitian · distribuidores autorizados | Antena parche 25 mm + **memoria flash** | **Mejor opción general.** 72 canales, hasta 10 Hz, **brújula HMC5883L** en bus I2C aparte, y flash que conserva la configuración. Algunos lotes traen QMC5883L en vez de HMC5883L. |
| **NEO-M8N (formato puck)** | ESP32-C6-WROOM-1 | u-blox M8 (M8030-KT) | GPS + BDS | UART + I2C | 3.3 V / 5 V | ~20–30 | Deegoo-FPV · distribuidores | Carcasa impermeable + cable | **Montaje en mástil para vehículos.** Cúpula impermeable con plano de masa: mejor rendimiento real que un breakout desnudo. **Conector suele requerir re-pinado** para ESP32. |
| **HiLetgo NEO-7M** | ESP32-C3-MINI-1 | u-blox 7 | GPS **o** GLONASS (no simultáneos) | UART + **USB** | 3.3 V / 5 V | ~10–15 | HiLetgo | Antena + **puerto SMA** | Reemplazo directo del NEO-6M **sin cambiar código**. El **USB y el SMA** son valiosos: permite depurar en u-center y conectar antena activa por cable. |
| **GT-U7 (2 uds.)** | ESP32-C3-MINI-1 | u-blox 7-class | GPS | UART + **micro-USB** | 3.3 V / 5 V | ~12–18 (pack 2) | GT-U7 · listados reputados | Antena IPEX + EEPROM | **El más fácil para empezar.** Compatible con código NEO-6M, USB para probar en PC antes de cablear. Pack de 2: unidad de banco + unidad de campo. |
| **HGLRC M100-5883** | ESP32-C6-WROOM-1 | u-blox M10 | GPS + Galileo + BDS + GLONASS | UART + I2C | 3.3 V | ~15–25 | HGLRC | Antena + brújula QMC5883 | **Generación más reciente y más pequeña.** M10 = mejor sensibilidad y fix más rápido que M8. Formato diminuto para drones/rovers. |
| **Módulos con GNSS integrado en 4G/LoRa** | (ver Tablas A y B) | — | Variable | — | — | — | — | — | **Evitar duplicar hardware.** SIM7600G, SIM7080G, SIM868, Heltec Wireless Tracker y LILYGO T-Beam **ya incluyen GNSS**. Si el nodo lleva uno de esos, no hace falta un módulo GPS aparte. |

### Advertencias críticas sobre GPS

1. **Ninguno obtiene fix en interiores.** Necesitan vista despejada del cielo. El primer fix en
   frío tarda de 30 segundos a varios minutos. Esto define el presupuesto de energía del nodo.
2. **La antena importa más que el chip.** La antena cerámica pequeña que viene con las placas
   baratas es el punto débil número uno. Para montaje dentro de un vehículo o carcasa, hace falta
   **antena activa por cable** — de ahí el valor del conector SMA del NEO-7M.
3. **Precisión autónoma: ~1.5 a 2.5 m** a cielo abierto. Para centímetros hace falta RTK
   (ZED-F9P + correcciones), que no tiene sentido en una alarma.
4. **Clones y chips reciclados son habituales.** Casi todas las placas "NEO-6M" y "NEO-M8N"
   baratas son de terceros; u-center puede reportar versiones de firmware extrañas y algunas
   configuraciones no se guardan. Para hobby funcionan; no para un producto.
5. **La brújula exige montaje separado.** El magnetómetro (BN-880, M100-5883) debe ir **lejos de
   motores, imanes e cables de potencia**, o las lecturas de rumbo son basura. En una alarma, el
   reed switch de la puerta es justamente un imán: no montar la brújula junto a él.

---

## Tabla D — Cargador de batería de respaldo (UPS)

| Módulo | Procesador | Celdas | Corriente de Carga | Entrada | Costo Unitario (USD) | Proveedor Confiable | Hardware Adicional | Notas |
|---|---|---|---|---|---|---|---|---|
| **Adafruit bq24074** | ESP32-C6-WROOM-1 | 1S | hasta 1.5 A | **USB-C / DC / Solar** | ~15–18 | **Adafruit** | Batería LiPo + panel solar opcional | **La mejor opción para respaldo real.** Tiene **power path**: alimenta la carga *y* carga la batería a la vez, y **conmuta sin cortar la salida**. Es la diferencia entre "el ESP32 se reinicia" y "el ESP32 no se entera". Entrada solar con MPPT. |
| **TP4056 Type-C (con protección)** | ESP32-C3-MINI-1 | 1S | ~1 A (máx., fijado por resistencia) | USB-C | ~1–2 | Listados reputados | **Cargador boost 5 V (MT3608)** + batería | **Barato y universal, pero NO es un UPS.** Solo carga: no sube 3.7 V a 5 V ni conmuta la carga. **Problema documentado:** al cortarse la entrada, la salida del TP4056 puede caer a **2.5–2.6 V** antes de recuperarse, reiniciando el ESP32. Necesita **modificación de load-sharing** para servir de respaldo. Comprar la versión **con protección** (evita sobredescarga). |
| **IP5306** | ESP32-C3-MINI-1 | 1S | ~2 A | USB-C / Micro-USB | ~2–4 | Listados reputados | Batería 18650 | **Cargador + boost 5 V en una placa.** Más completo que el TP4056 para respaldo. **Contra:** suele apagarse automáticamente con cargas muy pequeñas (el "auto power-off" corta el ESP32 en deep sleep), y su salida conmuta con interrupción. |
| **TP5100** | ESP32-WROOM-32E | 1S / 2S | hasta 2 A | DC / USB | ~2–4 | Listados reputados | Batería + boost para 5 V | Para **packs de 2 celdas**. **Nunca cargar un pack 2S con un cargador de 1 celda** — riesgo de incendio por sobrecarga. |
| **DFRobot Solar Power Manager** | ESP32-C6-WROOM-1 | 1S | variable | **Solar + USB** | ~15–25 | **DFRobot (SEN0290 y familia)** | Panel solar + batería | Gestión solar con MPPT y power path. Para nodos exteriores permanentes (cercas, vehículos, áreas remotas). |
| **Módulo UPS 18650 dedicado** | ESP32-C6-WROOM-1 | 1S (18650) | ~1–2 A | USB-C / DC | ~4–8 | Waveshare · listados reputados | Batería 18650 | Placas que integran carga + boost + conmutación. **Verificar que especifique "power path" o "UPS"**, no solo "charger": muchas se anuncian como UPS sin conmutar sin corte. |
| **Supercondensador + PMIC** | ESP32-C6-WROOM-1 | — | — | — | ~5–10 | Distribuidores autorizados | Supercondensador + PMIC | **Desarrollo futuro.** Respaldo para cortes cortos (segundos/minutos) sin química de litio: inmune al frío, miles de ciclos. Capacidad insuficiente para operación prolongada, suficiente para reportar un corte y cerrar el sistema limpiamente. |

### Advertencias críticas sobre respaldo de energía

1. **"Cargador" no es "UPS".** Esta es la confusión más costosa del documento. El TP4056
   **solo carga**. Un respaldo real necesita **power path / load sharing**: la electrónica que
   alimenta el sistema desde la fuente externa cuando está disponible y desde la batería cuando
   no, **sin interrumpir la salida**.
2. **El fallo concreto del TP4056 en este uso está documentado.** Al perderse la alimentación
   externa, la tensión de salida oscila entre ~2.5 V y la tensión de la batería. El ESP32 se
   reinicia exactamente en el momento en que el respaldo debía entrar en acción. Con modificaciones
   (diodo + segunda fuente, o un módulo con power path) se resuelve; de fábrica, no.
3. **Litio sin protección es un riesgo real.** Usar siempre baterías **con BMS/protección** y
   módulos de carga **con protección contra sobredescarga**. Nunca cargar packs multicinga con
   cargadores de una celda.
4. **Rango de la batería vs. regulador.** Una celda LiPo entrega 3.0–4.2 V. Un ESP32 necesita
   3.3 V regulados. Con un LDO común, por debajo de ~3.5 V el regulador deja de sostener 3.3 V y
   el sistema falla **con la batería aún a media carga**. Usar un **regulador buck-boost** o
   aceptar que se pierde el último tercio de la celda.
5. **Medición de nivel de batería.** Un divisor de tensión al ADC del ESP32 permite leer el
   estado. Recordar: **usar ADC1** (ADC2 pelea con el Wi-Fi) y calibrar, porque el ADC del ESP32
   es no lineal.
6. **Dimensionar la batería por el consumo real del sistema completo**, no del ESP32. Un nodo con
   SIM7600G en transmisión consume varios cientos de mA en picos; con Cat-M1 y deep sleep,
   microamperios. La diferencia entre días y años está en el módulo de comunicaciones, no en el
   sensor.

---

## Combinaciones recomendadas para un sistema de alarma

| Escenario | Comunicación | Posicionamiento | Energía | Costo aprox. de la capa |
|---|---|---|---|---|
| **Nodo fijo interior (puerta/ventana)** | Wi-Fi (ESP32 nativo) | No aplica | Batería + TP4056 (sin UPS estricto) | ~5 USD |
| **Nodo fijo con respaldo ante corte de luz** | Wi-Fi + **LoRa RYLR998** como enlace alterno | No aplica | **bq24074** + LiPo | ~35 USD |
| **Nodo exterior a batería (meses)** | **LoRa** (RYLR998 / E22) | No aplica | **SIM7080G-style low power** o LoRa + solar | ~40 USD |
| **Vehículo (rastreo + alarma)** | **4G A7670G** o **Heltec Wireless Tracker** | **Incluido** en el módulo | bq24074 + LiPo + 12 V del vehículo | ~60 USD |
| **Sitio remoto sin infraestructura** | **4G Cat-M1/NB-IoT (SIM7080G)** | Heltec Wireless Tracker (LoRa+GNSS) | Solar + DFRobot Power Manager | ~80 USD |
| **Espacio aislado por muros (bodega, sótano)** | **LoRa punto a punto** (EBYTE E32) | No aplica | Batería + bq24074 | ~30 USD |

**Nota sobre redundancia:** en un sistema de alarma, el escenario que hay que sobrevivir es
**corte de energía + corte de internet simultáneos**, porque es cuando ocurren las intrusiones.
Eso implica que el respaldo debe cubrir **dos capas independientes**: la energía (batería con
power path) y el enlace (LoRa o 4G como camino alterno al Wi-Fi del hogar). Un sistema con
batería pero sin enlace alterno protege el hardware y falla en la función.

---

---

# Nodos de Actuación: Pánico, Sirena y Video

Las secciones anteriores catalogan **sensores**: elementos que *miden* algo del
entorno. Los tres nodos de esta sección son distintos — **actúan** o **capturan**:

| Nodo | Función | Dirección del flujo |
|---|---|---|
| **Botón de pánico** | El usuario declara una emergencia | Entrada → sistema |
| **Sirena** | El sistema disuade o alerta | Sistema → entorno |
| **Video** | Evidencia y verificación visual | Sistema → usuario |

Por eso van aparte: tienen requisitos de **fiabilidad** (el pánico), de
**energía** (la sirena) y de **ancho de banda** (el video) que ningún sensor de
la tabla principal plantea.

---

## Tabla E — Nodo de botón de pánico

Un botón de pánico parece el nodo más simple del sistema. **Es el que más
cuidado exige**, porque su único propósito es funcionar en el peor momento
posible: cuando alguien tiene prisa, miedo o está siendo coaccionado.

| Elemento | Opción recomendada | Alternativas | Interfaz | Costo Unitario (USD) | Proveedor Confiable | Notas |
|---|---|---|---|---|---|---|
| **Pulsador** | Pulsador de seta con enclavamiento (tipo parada de emergencia) | Pulsador de membrana industrial (50.000+ pulsaciones) | GPIO (digital) | 1 – 4 | Listados reputados de material eléctrico | **La seta evita pulsaciones accidentales y es fácil de encontrar a ciegas.** Un pulsador de protoboard falla justo cuando importa. |
| **Carcasa** | Caja con tapa rompible o tapa abatible | Caja estanca IP54 con membrana | — | 1 – 3 | Listados reputados | La tapa **obliga a una acción deliberada**: evita falsas alarmas por un roce. Documentar cómo se abre. |
| **Retención del estado** | **Reed switch de enclavamiento (latching)** | Condensador de retención | GPIO | 0.30 – 0.80 | Listados reputados | **Aunque el ESP32 esté dormido o reiniciándose, el estado queda registrado.** Si no, un reinicio durante la pulsación pierde la alarma. |
| **Alimentación** | Pila de litio **no recargable** (CR123A o 2× AA litio) | LiPo + bq24074 (ver Tabla D) | — | 2 – 6 | Distribuidores autorizados | **Un pánico no debe depender de que haya luz eléctrica.** Pila primaria: dura años y no necesita cargarse. |
| **Confirmación local** | LED + zumbador piezo | Solo LED | GPIO | 0.50 – 2 | Listados reputados | El usuario **debe saber** que su pulsación se registró. Sin confirmación, pulsa dos veces o se queda con la duda. |
| **Micrófono (opcional)** | MAX9814 (con AGC) | INMP441 (I2S digital) | ADC1 o I2S | 2 – 6 | Adafruit · listados reputados | Permite **capturar audio tras la pulsación** como evidencia. Es la mejora más valiosa del nodo, y la que más debate legal genera (ver advertencias). |

### Advertencias críticas sobre el botón de pánico

1. **Un pánico con batería recargable es un pánico que puede estar descargado.**
   Es el único nodo del sistema donde una **pila primaria** (no recargable) es la
   opción correcta: dura años en reposo y no depende de que alguien se acuerde de
   cargarla. Reservar la LiPo para el resto del sistema.

2. **El consumo en reposo define el nodo.** Un ESP32 despierto consume decenas de
   mA y agota cualquier pila en días. El diseño debe ser **deep sleep con
   interrupción GPIO**: el pulsador despierta al chip, que transmite y vuelve a
   dormir. Consumo objetivo: **decenas de microamperios** en reposo.

3. **Anti-sabotaje: el nodo puede ser atacado.** Un botón de pánico alcanzable
   puede ser arrancado o tapado. Conviene detectar:
   - **Pérdida de continuidad** del cable del pulsador (circuito de supervisión
     con resistencia de fin de línea)
   - **Apertura de la carcasa** (switch de manipulación)
   - **Pérdida de latido**: si el nodo deja de reportar, el sistema debe avisar

4. **La confirmación al usuario no es opcional.** Un pánico sin LED ni zumbador
   deja al usuario sin saber si funcionó, y repetirá la pulsación. Eso genera
   falsos duplicados que confunden el registro de eventos.

5. **Enlace: no depender solo del Wi-Fi del hogar.** El escenario real de un
   pánico suele incluir corte de luz o de internet. Ver las tablas A y B: un
   enlace LoRa punto a punto es la redundancia natural, y es más importante aquí
   que en cualquier otro nodo.

6. **Sobre el audio como evidencia — leer antes de implementarlo.** Grabar audio
   en una vivienda tiene implicaciones legales distintas en cada país, y las
   personas presentes pueden no haber consentido. **No activar grabación sin
   revisar la normativa aplicable** y sin que el usuario lo sepa. Un sistema de
   alarma que graba a escondidas es un problema legal, no una función.

---

## Tabla F — Nodo sirena

La sirena es el único nodo del sistema cuyo trabajo es **consumir energía a
propósito**. Eso la vuelve incompatible con las decisiones de bajo consumo del
resto del catálogo.

| Elemento | Opción recomendada | Alternativas | Interfaz | Costo Unitario (USD) | Proveedor Confiable | Notas |
|---|---|---|---|---|---|---|
| **Sirena** | Sirena piezoeléctrica de 12 V, 110–120 dB | Zumbador piezo de 3.3 V (débil, no disuasorio) | Relé o MOSFET | 5 – 15 | Listados reputados de material de seguridad | **Los dB caen rápido con la distancia.** 120 dB a 1 m son ~90 dB a 10 m: suficiente para disuadir dentro, no para toda una manzana. |
| **Conmutación** | **Relé de estado sólido o MOSFET de canal N** | Relé mecánico (más lento, desgaste) | GPIO | 1 – 4 | Listados reputados | El ESP32 **no puede alimentar la sirena directamente** (ver advertencia 1). El MOSFET debe llevar **diodo de recirculación** si la carga es inductiva. |
| **Alimentación** | Fuente dedicada 12 V | Batería 12 V o pack 3S con convertidor | — | 5 – 15 | Listados reputados | **Circuito independiente del ESP32.** Una sirena que hunde la fuente compartida reinicia el controlador justo cuando está sonando. |
| **Respaldo** | Batería 12 V con cargador con *power path* | Pack 3S Li-ion + BMS | — | 15 – 40 | Ver Tabla D | Aplican las mismas advertencias de la Tabla D. **"Cargador" no es "UPS".** |
| **Sensor de presencia de sirena** | Supervisión por corriente (shunt) o por resistencia de fin de línea | — | ADC1 | 1 – 3 | Listados reputados | Detecta que **cortaron el cable de la sirena** antes de la intrusión. Sin esto, el atacante desactiva la disuasión sin que nadie lo sepa. |
| **Limitador de tiempo** | **Temporizador por hardware** (no solo por software) | Temporizador del propio ESP32 | — | 0.50 – 2 | Listados reputados | Ver advertencia 4: la sirena **debe** cortarse sola aunque el firmware se cuelgue. |

### Advertencias críticas sobre la sirena

1. **El ESP32 no puede manejar la corriente de una sirena.** Un GPIO entrega
   ~12 mA a 3.3 V; una sirena de 12 V consume **cientos de mA o amperios**. Hace
   falta conmutación por MOSFET o relé, con la fuente adecuada. Conectar la
   sirena directa a un GPIO no "funciona mal": **destruye el pin**.

2. **La sirena no comparte fuente con el controlador.** El pico de arranque de
   una sirena hunde la tensión, y un ESP32 con la tensión hundida se reinicia.
   El resultado es un sistema que se reinicia mientras suena — y deja de sonar.
   **Fuente separada, o al menos rama separada con condensador de reserva.**

3. **El cable de la sirena es el punto débil físico.** Está fuera del panel,
   accesible, y es lo primero que corta un atacante. Por eso la **supervisión de
   línea** (medir la resistencia del circuito) es más valiosa que la potencia de
   la propia sirena.

4. **Límite de tiempo obligatorio, y por hardware.** Una sirena que no se apaga
   por un fallo de firmware es una denuncia por ruido y un vecindario en contra.
   La normativa de muchos países limita la duración (típicamente 3–15 minutos).
   **El corte debe ser independiente del software.**

5. **La sirena no sustituye a la notificación.** Su función es **disuadir** y
   alertar a quien esté cerca. No avisa al dueño si está fuera. Es un complemento
   del push del portal, nunca un reemplazo.

6. **Distinguir sirena interior de exterior.** Una exterior necesita certificación
   IP y su nivel de dB está regulado. Una interior puede ser más discreta. **No es
   el mismo producto ni el mismo costo.**

7. **Cuidado con el lazo de realimentación.** Si el nodo que detecta la intrusión
   es el mismo que dispara la sirena, una falsa alarma genera un evento ruidoso e
   imborrable. Conviene un **tiempo de confirmación** o una segunda señal antes de
   disparar, configurable por el usuario.

---

## Tabla G — Nodo de video

El nodo de video **no es un sensor con cámara**: es un sistema de captura con
requisitos propios de ancho de banda, almacenamiento y energía que **exceden el
resto del catálogo por un orden de magnitud**.

| Elemento | Opción recomendada | Alternativas | Interfaz | Costo Unitario (USD) | Proveedor Confiable | Notas |
|---|---|---|---|---|---|---|
| **Módulo** | **ESP32-S3-WROOM-1 con PSRAM de 8 MB** | ESP32-S3 con cámara integrada (ESP32-S3-EYE, Freenove) | DVP o SPI | 8 – 25 | Espressif · listados reputados | ⚠️ **PSRAM obligatoria.** Sin ella no hay buffer para un fotograma JPEG. Un ESP32 clásico (sin S) **no sirve** para video. |
| **Cámara** | **OV2640** (JPEG por hardware, 2 MP) | OV5640 (5 MP, autofoco, más consumo) | DVP | 3 – 12 | Listados reputados | **El OV2640 comprime JPEG en el propio sensor**: el ESP32 solo mueve bytes. Sin eso, el chip no da abasto. |
| **Almacenamiento local** | **Tarjeta microSD por SPI** | Flash interna (insuficiente para video) | SPI | 3 – 8 (módulo) + 5–15 (tarjeta) | Listados reputados | **El video no cabe en la flash del ESP32.** Sin SD, cada captura debe salir por red o perderse. |
| **Conectividad** | Wi-Fi (nativo del ESP32-S3) | 4G (ver Tabla A) para sitios sin internet | — | — | — | **El video es lo que más ancho de banda consume del sistema.** Ver advertencia 2. |
| **Visión nocturna** | Módulo con LED IR 850 nm + filtro IR removible | Sin IR (inútil de noche) | GPIO (control de LED) | 2 – 8 | Listados reputados | Algunos módulos traen el **filtro IR fijo**: no sirven de noche. Verificar que sea removible (el clic audible al cambiar es la señal). |
| **Detección de movimiento** | El propio ESP32 comparando fotogramas | Sensor PIR (ver tabla principal) | — | 0 | — | **Comparar dos JPEG en el chip es suficiente** para decidir si vale la pena subir. Ahorra ancho de banda y batería. Preferible al PIR si ya hay cámara. |
| **Energía** | Alimentación fija (no batería) | Batería + panel solar (solo capturas espaciadas) | — | — | Ver Tabla D | Ver advertencia 3: **la batería y el video casi no se llevan.** |

### Advertencias críticas sobre el video

1. **Sin PSRAM no hay video.** Un ESP32 sin PSRAM no tiene memoria para un
   fotograma. Incluso con ella, la resolución práctica para envío continuo es
   **QVGA o VGA**, no 2 MP. El sensor puede más de lo que el chip puede mover.

2. **El ancho de banda es el límite real, no la cámara.** Un video fluido por
   Wi-Fi satura una conexión doméstica y consume el plan de datos si es 4G.
   Estrategia recomendada para una alarma: **no transmitir video continuo**.
   Enviar **un fotograma por evento**, y video solo si el usuario lo pide en ese
   momento. Es la diferencia entre un sistema viable y uno que gasta datos sin
   aportar más seguridad.

3. **El video y la batería son incompatibles en la práctica.** Un nodo que
   captura video consume cientos de mA durante la captura. Un nodo a batería con
   video dura **días, no meses**. Si el nodo debe ser autónomo, usar **PIR o
   radar para despertar** y capturar pocos fotogramas por evento.

4. **El almacenamiento es un problema de privacidad, no solo de espacio.** Las
   grabaciones de una vivienda son datos personales sensibles. Aplican las mismas
   consideraciones legales que el audio del botón de pánico, y con más motivo.
   **Definir política de retención** (cuánto se guarda) y avisar al usuario.

5. **La cámara en la nube cambia el modelo de confianza.** Subir video a un
   servicio externo significa que un tercero tiene imágenes del interior de la
   casa. Si el proyecto promete que los datos son del cliente, **el video debe
   quedarse en el servidor propio** o no salir del hogar.

6. **El nodo de video no debe ser también el de detección crítica.** Si la alarma
   depende de un nodo que además mueve video, la carga de trabajo del chip puede
   retrasar la detección. **Separar responsabilidades**: detección en un nodo
   ligero, evidencia visual en otro.

7. **Considerar si el video aporta o solo complica.** Para la mayoría de alarmas
   domésticas, un **fotograma al activarse** más el aviso push cubre la necesidad
   ("¿qué pasó?") a una fracción del costo, la energía y la complejidad legal de
   un flujo de video continuo. **Empezar por el fotograma** y añadir video solo si
   un requisito concreto lo justifica.

---

## Resumen: los tres nodos activos frente a los sensores

| Aspecto | Sensores (tabla principal) | Pánico | Sirena | Video |
|---|---|---|---|---|
| **Consumo** | Bajo, apto para batería | Muy bajo en reposo | **Alto** | **Muy alto** |
| **Batería viable** | Sí (varios modelos) | Sí, con pila primaria | No | No |
| **Ancho de banda** | Mínimo (unos bytes) | Mínimo | Ninguno | **El mayor del sistema** |
| **Riesgo principal** | Clon del componente | **Fallar cuando se necesita** | **Hundir la fuente** | **Privacidad y datos** |
| **Redundancia necesaria** | Recomendable | **Obligatoria** | Recomendable | Opcional |
| **Complejidad legal** | Ninguna | Media si graba audio | Baja (ruido) | **Alta** |

**Conclusión de prioridades:** si el sistema tiene que elegir, el orden de valor
por esfuerzo es **pánico → sirena → video**. El botón de pánico es barato,
insustituible y su ausencia es la que más se nota en una emergencia real. El video
es el más costoso en dinero, energía y responsabilidad legal, y el que menos
aporta si el push ya avisa y el fotograma ya muestra qué pasó.


## Limitaciones declaradas de este documento

- **Precios:** en las tablas de sensores son rangos de calle de fuente pública (julio 2026).
  En las Tablas A–D hay tres niveles de confianza, y conviene distinguirlos:
  - **Precio puntual verificado** (Heltec WiFi LoRa 32 V3.2 = 17.90; Wireless Tracker = 22.90;
    LILYGO T-Beam = 30.77) — fuente que declara verificación **1–5 de agosto de 2026** contra
    documentación del fabricante.
  - **Precio de comparativa** (módulos SIM: SIM7600G 32.50, A7670 31.16, SIM7080G 26.91,
    SIM800L 6.00, SIM868 11.22) — de tabla comparativa pública, sin fecha de verificación
    declarada por celda. **Tratar como orientativo.**
  - **Rango estimado** (REYAX, EBYTE, Adafruit, GPS, cargadores) — rango de mercado; el autor
    de la fuente declara explícitamente que **no publica precios porque cambian con frecuencia**.
- **Calificaciones de proveedor:** no verificadas una por una. Ver sección *Sobre el criterio
  4.5/5*.
- **La excepción:** las calificaciones de Amazon que sí aparecen citadas en este documento
  (REYAX RYLR998 4.6★/220 valoraciones, RFM95W 4.3★/52, BN-880 4.7★, NEO-6M 4.5★, ATGM336H 4.3★)
  provienen de la fuente citada en su fecha de consulta. **No las reverifiqué hoy.**
- **Disponibilidad:** los rangos asumen stock normal; la escasez de semiconductores puede
  alterar tanto precio como disponibilidad.
- **Celdas marcadas `No verificado`:** no se inventaron valores donde no hubo fuente.
- **Cobertura de precios 4G:** los módulos SIM7600G-H y A7670 en formato HAT o all-in-one
  (LILYGO, Waveshare) tienen precios muy superiores al módulo desnudo. Las cifras de la Tabla A
  corresponden al **módulo**, no a la placa ensamblada; verificar el producto concreto.
