# Hardware

Diseño del hub, los nodos sensores y el enlace local ESP-NOW.

| Documento | Qué contiene |
|---|---|
| [Arquitectura del hub](ARQUITECTURA_HUB.md) | Rol, tareas FreeRTOS, tópicos MQTT, provisioning y alimentación |
| [Protocolo ESP-NOW M2bis](PROTOCOLO_ESPNOW.md) | Trama, CRC, anti-replay y ACK |
| [Validación M2bis](validacion-m2bis.md) | Resultados AT-02: latencia y entrega de frames |
| [Infraestructura real](INFRA-REAL.md) | Hardware y red usados en las pruebas |
| [A1 · Tareas FreeRTOS](A1-TAREAS-FREERTOS.md) | Reparto de tareas y prioridades |
| [A2 · Máquinas de estado](A2-MAQUINAS-ESTADO.md) | Estados de conexión y alarma |
| [A3 · Presupuesto de memoria](A3-PRESUPUESTO-MEMORIA.md) | RAM/Flash por componente |
| [A4 · Modelo de seguridad](A4-MODELO-SEGURIDAD.md) | Amenazas y controles del nodo |
| [A5 · Secuencias de integración](A5-SECUENCIAS-INTEGRACION.md) | Flujos entre nodo, hub y nube |
| [A6 · Registro de riesgos](A6-REGISTRO-RIESGOS.md) | Riesgos técnicos y mitigaciones |
| [A7 · Hitos M1–M7](A7-HITOS-M1-M7.md) | Plan de validación |
| [A8 · Preguntas abiertas](A8-PREGUNTAS-ABIERTAS.md) | Decisiones pendientes |

## Código relacionado

- `firmware/hub/`: firmware del hub ESP32-S3.
- `firmware/node_base/`, `firmware/sensor-*/`: firmware de nodos.
- `hardware/`: reservado para BOM, PCB, esquemáticos y carcasa (aún vacío).

---

## Catalogo de nodos sensores

**[nodos-sensores-esp32.md](nodos-sensores-esp32.md)** — referencia de los nodos
que se van a construir para ampliar el sistema.

Es un catalogo de **17 tipos de sensores** (contacto magnetico, presencia mmWave,
PIR, vibracion, distancia laser, gas, calidad de aire, temperatura, peso...) con
el modulo concreto, la interfaz, el nivel logico y las trampas de cada uno.

Incluye ademas cuatro tablas transversales:

| Tabla | Contenido |
|---|---|
| A | Modulos de comunicacion 4G/LTE |
| B | Modulos LoRa |
| C | Modulos GPS/GNSS |
| D | Cargadores de bateria de respaldo (UPS) |

Y combinaciones recomendadas por escenario (nodo fijo, exterior a bateria,
vehiculo, sitio remoto).

**Aviso importante del documento:** los precios son rangos de fuente publica, no
verificados celda por celda. Las celdas sin fuente dicen `No verificado` en lugar
de un numero inventado. Lo mismo con las calificaciones de proveedor: explica por
que el criterio "4.5/5" no sirve en estos mercados.

### Lo que hay que leer antes de comprar

1. **Nivel logico:** el ESP32 es de 3.3 V y **no tolera 5 V** en GPIO. Todo modulo
   de 5 V necesita divisor o level shifter.
2. **"Cargador" no es "UPS".** El TP4056 solo carga: al cortarse la luz, la salida
   cae a ~2.5 V y reinicia el ESP32. Un respaldo real necesita *power path*.
3. **Frecuencia LoRa:** 915 MHz en America, 868 MHz en Europa. Es una decision
   legal, no tecnica.
4. **Riesgo de clon:** el BME280 falsificado es comun; el DS18B20 impermeable casi
   siempre lleva chip clonado.
5. **Bateria:** el calentador del MQ-2 y los radares mmWave no son compatibles con
   alimentacion a bateria.
