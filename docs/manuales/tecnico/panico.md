# Botón de pánico — manual técnico

Borrador basado en el README del firmware; revisión instalada no comprobada.

## Fuente y electrónica

Fuente: `firmware/boton-panico/`. Placa: ESP32-S3 DevKit.

Pulsador normalmente abierto GPIO4 a GND, pull-up interna, LED GPIO2; antirrebote 50 ms y límite de un evento cada 3 s según README y fuente.

## Integración

Consultar el [contrato SDD](../../firmware/specs/02-interfaces.md) y el [protocolo común](protocolo-comun.md). No modificar interfaces desde este manual.

## Aceptación

Ensayar primera y segunda pulsación separadas, duplicados, pérdida de enlace, caída del backend y recuperación; comprobar evento, sirena y recepción en portal. No usar una emergencia real para probar.

Registrar commit, build, revisión electrónica, fecha, configuración, resultado y evidencia en almacenamiento privado. Estado inicial: no probado en esta revisión.

## Límites

El código envía una vez por pulsación sin ACK de datos y no encola sin enlace. No usar como único medio de pedir ayuda.

Aplicar [versionado y flasheo](../../infraestructura/versionado-y-flasheo.md) antes de cualquier actualización. No flashear sin autorización.
