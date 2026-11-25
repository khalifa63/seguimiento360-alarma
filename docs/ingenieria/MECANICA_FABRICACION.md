# Diseño mecánico y fabricación tercerizada

Estándar propuesto para HUB y nodos diseñados en Fusion 360 o SolidCraft.

## Almacenamiento privado por componente

- `source/fusion360/` y `source/solidcraft/`: originales editables.
- `exports/step/`, `exports/stl/`, `exports/3mf/`: exportaciones aprobadas.
- `assemblies/`, `drawings/`, `renders/`: ensamblajes y planos.
- `manufacturing/orders/`, `manufacturing/quality/`: órdenes y recepción.

Conservar revisión mecánica independiente y compatibilidad electrónica. No subir diseños propietarios a este repositorio público. Considerar Git LFS en almacenamiento privado autorizado. Confirmar formato nativo de SolidCraft.

## Fabricación

Asignar orden única, revisión, hashes, cantidad, material, proceso, unidades, dimensiones, tolerancias y acabado acordados. Enviar solo los archivos necesarios al proveedor. Registrar costos, proveedor y lote en privado. Inspeccionar cantidad, defectos, ajuste PCB, conectores, montaje, resistencia y funcionamiento. Registrar evidencia y aprobación. Una reimpresión sin cambios conserva revisión y recibe orden nueva. Vincular lote, dispositivo y manual compatible.
