# Registros privados de ingeniería

Estructura propuesta fuera del repositorio público. No contiene archivos CAD, secretos ni información de instalaciones reales.

## Esquema por componente

```text
<component-id>/
  electronics/bom/
  electronics/schematics/
  electronics/pcb/
  mechanical/source/fusion360/
  mechanical/source/solidcraft/
  mechanical/exports/
  mechanical/drawings/
  manufacturing/orders/
  manufacturing/quality/
  releases/builds/
  releases/installations/
  validation/evidence/
  continuity/
```

BOM: referencia, fabricante, parte, cantidad, revisión, alternativa aprobada y fuente. CAD: formato nativo confirmado, revisión, unidades, tolerancias y hash de exportación. Fabricación: orden, lote, revisión, material, cantidad, inspección y aceptación. Mantener costos y proveedores en privado. No crear carpetas reales ni subir diseños hasta aprobar almacenamiento y permisos.

## Continuidad por componente

Registrar fecha UTC, commit, estado de hardware, build, cambios, pruebas, pendientes, riesgos y siguiente acción segura. Nunca incluir credenciales. Ver [continuidad general](../desarrollo/CONTINUIDAD.md).
