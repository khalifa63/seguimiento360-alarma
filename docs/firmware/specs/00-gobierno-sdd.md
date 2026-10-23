# SPEC-00 — Gobierno SDD del Hub S360

## Principios
1. La especificacion es la fuente de verdad; el codigo deriva de ella.
2. Todo cambio inicia como edicion de spec (review primero, codigo despues).
3. Requisitos con ID estable: HUB-F (funcional), HUB-NF (no funcional),
   HUB-IF (interface). No se reutilizan IDs retirados.
4. Pruebas de aceptacion con ID AT-xxx; cada una traza a >= 1 requisito.
5. Matriz de trazabilidad en 01-requisitos.md (REQ -> modulo -> AT).

## Flujo de trabajo
1. Escribir/actualizar spec (este directorio)
2. Implementar en components/ respetando contratos de 02-interfaces.md
3. Ejecutar ATs de 03-pruebas-aceptacion.md contra servidor real
4. Registrar resultado (fecha, fw version, PASS/FAIL) en la AT
5. Commit con mensaje "spec: ..." o "feat(HUB-F-00X): ..."

## Versionado de specs
- version semver en cada archivo; bump minor al agregar requisitos,
  major al romper contratos (frame ESP-NOW, topics MQTT, NVS)
