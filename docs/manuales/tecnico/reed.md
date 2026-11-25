# Sensor de apertura — manual técnico

Borrador basado en el README del firmware; revisión instalada no comprobada.

## Fuente y electrónica

Fuente: `firmware/reed-sensor/`. Placa: ESP32 DevKit V1.

Reed normalmente abierto entre GPIO4 y GND, pull-up interna; LED GPIO2. Muestreo 100 ms, dos muestras estables.

## Integración

Consultar el [contrato SDD](../../firmware/specs/02-interfaces.md) y el [protocolo común](protocolo-comun.md). No modificar interfaces desde este manual.

## Aceptación

Verificar apertura y cierre, estado reportado, rebote, pérdida y recuperación de enlace. Confirmar la conducta del buzzer y del portal según configuración.

Registrar commit, build, revisión electrónica, fecha, configuración, resultado y evidencia en almacenamiento privado. Estado inicial: no probado en esta revisión.

## Límites

No acredita protección contra manipulación ni distancia máxima del imán.

Aplicar [versionado y flasheo](../../infraestructura/versionado-y-flasheo.md) antes de cualquier actualización. No flashear sin autorización.
