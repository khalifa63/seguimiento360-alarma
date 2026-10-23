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
