# Sirena buzzer — manual técnico

Borrador basado en el README del firmware; revisión instalada no comprobada.

## Fuente y electrónica

Fuente: `firmware/buzzer/`. Placa: ESP32-C3 SuperMini.

Buzzer pasivo GPIO3; LED GPIO8 activo bajo. PWM 2700 Hz, duty 90 %.

## Integración

Consultar el [contrato SDD](../../firmware/specs/02-interfaces.md) y el [protocolo común](protocolo-comun.md). No modificar interfaces desde este manual.

## Aceptación

Comprobar activación temporizada, extensión de duración, parada, modo continuo y recuperación de enlace. Proteger la audición y avisar a las personas cercanas antes de probar.

Registrar commit, build, revisión electrónica, fecha, configuración, resultado y evidencia en almacenamiento privado. Estado inicial: no probado en esta revisión.

## Límites

No se garantiza nivel acústico, cobertura ni continuidad con pérdida de alimentación.

Aplicar [versionado y flasheo](../../infraestructura/versionado-y-flasheo.md) antes de cualquier actualización. No flashear sin autorización.
