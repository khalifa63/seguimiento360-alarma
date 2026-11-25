# Plantillas y registros

Campos mínimos propuestos. Usar `null` o pendiente cuando no haya evidencia. Los registros operativos reales son privados.

## Ficha por componente

ID, propósito, fuente, revisión electrónica y mecánica, firmware, build, protocolo, dependencias, configuración, manual, requisitos, pruebas, riesgos y próximo paso.

## Compilación y flasheo

Build único, commit completo, herramientas, hash y tamaño de artefactos, fecha UTC, resultado. Flasheo: dispositivo, build, autorización, respaldo, verificación, prueba funcional, recuperación. Conservar historial. Aplicar [procedimiento existente](../infraestructura/versionado-y-flasheo.md).

## Compatibilidad y sistema

Versión integrada, componentes exactos, protocolo, revisiones y configuraciones. Combinación compatible, incompatible o no probada; referencia de prueba y aprobación.

## Manual

ID, versión, idioma, firmware y hardware compatibles, estado y fuente. Incluir instalación, vinculación, operación, indicadores, advertencias, diagnóstico y mantenimiento.

## Fabricación e instalación

Orden, revisión CAD, lote, hashes, calidad, aprobación. Instalación: identificación privada, inventario, pruebas, manual entregado, mantenimiento y retiro.

## Continuidad

Fecha, componente, commit, cambios, pruebas ejecutadas, pendientes y próximo paso seguro. Referenciar [continuidad](../desarrollo/CONTINUIDAD.md).
