# Documentación · Seguimiento 360

> Fuente única de verdad. Si un tema no está aquí, no existe.

**Proyecto:** plataforma IoT de alarma y monitoreo para hogar y vehículos.
**Última revisión:** 2026-10-22.
**Estado:** fase 3.2.3b completada (control de alarma en PWA). El detalle está en [Estado por fases](fases/RESUMEN_FASES.md).

## Por dónde empezar

| Si eres... | Empieza por |
|---|---|
| Nuevo en el proyecto | [Visión y alcance](00-vision.md) |
| Desarrollador de firmware | [Marco teórico del firmware](marco-teorico/marco-teorico-firmware.md) · [Plan de arranque](desarrollo/PLAN-DE-ARRANQUE.md) |
| Continuar el desarrollo | [Plan de arranque](desarrollo/PLAN-DE-ARRANQUE.md) · [Estado actual](desarrollo/README.md) |
| Backend / Node-RED | [Backend](backend/README.md) |
| Operaciones / servidor | [Estado actual de la infraestructura](infraestructura/ESTADO_ACTUAL.md) |
| Hardware | [Hardware](hardware/README.md) |
| Gestión / tesis | [Marco teórico](marco-teorico/portada.md) |
| Agente de IA | [AGENTS.md](../AGENTS.md) |

## Mapa de la documentación

### 1. Producto y visión

- [Visión y alcance](00-vision.md)
- [Manual general v1.2](01-manual-general.md)
- [Estado por fases](fases/RESUMEN_FASES.md)

### 2. Hardware

- [Índice de hardware](hardware/README.md)
- [Arquitectura del hub](hardware/ARQUITECTURA_HUB.md)
- [Protocolo ESP-NOW M2bis](hardware/PROTOCOLO_ESPNOW.md)
- [Validación M2bis](hardware/validacion-m2bis.md)
- [Infraestructura real](hardware/INFRA-REAL.md)
- Diseño A1–A8: [tareas FreeRTOS](hardware/A1-TAREAS-FREERTOS.md), [máquinas de estado](hardware/A2-MAQUINAS-ESTADO.md), [presupuesto de memoria](hardware/A3-PRESUPUESTO-MEMORIA.md), [modelo de seguridad](hardware/A4-MODELO-SEGURIDAD.md), [secuencias de integración](hardware/A5-SECUENCIAS-INTEGRACION.md), [registro de riesgos](hardware/A6-REGISTRO-RIESGOS.md), [hitos M1–M7](hardware/A7-HITOS-M1-M7.md), [preguntas abiertas](hardware/A8-PREGUNTAS-ABIERTAS.md)

### 3. Firmware

- [Índice de firmware](firmware/README.md)
- [Hub ESP32-S3](firmware/hub.md)
- Specs SDD: [gobierno](firmware/specs/00-gobierno-sdd.md), [requisitos](firmware/specs/01-requisitos.md), [interfaces](firmware/specs/02-interfaces.md), [pruebas de aceptación](firmware/specs/03-pruebas-aceptacion.md)

### 4. Backend y datos

- [Índice de backend](backend/README.md)
- [Base de datos PostgreSQL](backend/database.md)
- [Flujos Node-RED](backend/nodered-flows.md)
- [API de autenticación y usuarios](backend/api-auth.md)
- [Plantilla de `settings.js`](../nodered/data/settings.js.example)

### 5. Infraestructura y operación

- [Estado actual](infraestructura/ESTADO_ACTUAL.md)
- [Flujos Node-RED y MQTT](infraestructura/FLUJOS_NODERED.md)
- [Rutas de Cloudflare](infraestructura/CLOUDFLARE_RUTAS.md)

### 6. Patrones y decisiones

- [Patrones de diseño](patrones/PATRONES_DISENO.md)

### 7. Marco teórico

- [Marco teórico del firmware](marco-teorico/marco-teorico-firmware.md) — documento único de referencia para el desarrollo
- [Portada](marco-teorico/portada.md)
- [Índice general](marco-teorico/00_indice.md)
- [Introducción](marco-teorico/01_introduccion.md)
- [Estado del arte](marco-teorico/02_estado_del_arte.md)
- [Análisis de mercado](marco-teorico/03_analisis_mercado.md)
- [Arquitectura del sistema](marco-teorico/04_arquitectura_sistema.md)
- [Diseño de hardware](marco-teorico/05_diseno_hardware.md)
- [Diseño de software](marco-teorico/06_diseno_software.md)
- [Seguridad](marco-teorico/07_seguridad.md)
- [Despliegue y operación](marco-teorico/08_despliegue_operacion.md)
- [Plan de negocio](marco-teorico/09_plan_negocio.md)
- [Conclusiones](marco-teorico/10_conclusiones.md)
- [Referencias](marco-teorico/referencias.md)

### 8. Gobernanza

- [Prompts SDD](gobernanza/README.md)
- [Prompt maestro de firmware](gobernanza/PROMPT-SDD-MASTER.md)
- [Prompt maestro de servidor](gobernanza/PROMPT-SDD-SERVER.md)

### 9. Desarrollo

- [Plan de arranque](desarrollo/PLAN-DE-ARRANQUE.md)
- [Índice de desarrollo](desarrollo/README.md)
- [Roles de IA](desarrollo/AI-ROLES.md)

### 10. Archivo

- [Documentos históricos](_archivo/README.md)

## Reglas de la documentación

1. La documentación vive **solo** en `docs/`.
2. Un tema = un documento. No se duplica contenido.
3. Lo obsoleto se mueve a `docs/_archivo/`; no se borra.
4. No se publican secretos, tokens, contraseñas ni IPs privadas.
5. `mkdocs build --strict` y `scripts/check-docs.sh` deben pasar antes de fusionar.
