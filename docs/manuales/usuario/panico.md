# Botón de pánico — guía de usuario

Borrador para prototipo; pendiente de aceptación física y aprobación editorial.

## Uso

Solicita una alarma al pulsarlo. En la configuración documentada el sistema alterna activación y silencio mediante Node-RED. No asumir que el sonido o un destello confirma entrega remota.

## Preparación e instalación

- Confirmar con el instalador la alimentación y revisión del equipo.
- Ubicar en interior seco; no asumir resistencia al agua ni autonomía.
- Vincular al HUB siguiendo el procedimiento del instalador.
- Confirmar en el portal que el dispositivo está asociado al sitio correcto.

## Prueba y mantenimiento

Ensayar primera y segunda pulsación separadas, duplicados, pérdida de enlace, caída del backend y recuperación; comprobar evento, sirena y recepción en portal. No usar una emergencia real para probar.

Si no funciona, revisar alimentación y conexión desde el portal. Contactar al instalador si persiste. No abrir ni modificar el dispositivo energizado.

## Límites

El código envía una vez por pulsación sin ACK de datos y no encola sin enlace. No usar como único medio de pedir ayuda.

Consultar el [manual técnico](../tecnico/panico.md).
