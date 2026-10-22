# Desarrollo

Punto de entrada para continuar el desarrollo del firmware sin repetir trabajo.

## Orden de lectura

1. [Marco teórico del firmware](../marco-teorico/marco-teorico-firmware.md) — arquitectura, contratos, seguridad y pruebas.
2. [Plan de arranque](PLAN-DE-ARRANQUE.md) — estado actual, brechas y sprint de 10 días.
3. [API de autenticación](../backend/api-auth.md) — endpoints de usuarios y `curl`.
4. [Contratos](../firmware/specs/02-interfaces.md) — frame, topics y payloads.
5. [Pruebas de aceptación](../firmware/specs/03-pruebas-aceptacion.md) — criterios AT-001 a AT-010.

## Documentos ejecutivos

- [Plan de arranque (PDF)](PLAN-DE-ARRANQUE.pdf)
- [Marco teórico del firmware (PDF)](../marco-teorico/marco-teorico-firmware.pdf)
- [API de autenticación (PDF)](../backend/api-auth.pdf)

## Estado en una línea

La documentación y la API de usuarios están listas. El firmware del hub tiene WiFi/MQTT y el componente ESP-NOW v1, pero el nodo ultrasónico aún envía un payload de 7 bytes y el hub no usa esa ruta. La primera tarea es migrar el nodo al frame v1 y medir el ACK.

## Siguiente tarea

[FW-002 — Migrar el nodo ultrasónico al frame v1](PLAN-DE-ARRANQUE.md#4-backlog-priorizado), después de cerrar el payload en la SPEC-02.
