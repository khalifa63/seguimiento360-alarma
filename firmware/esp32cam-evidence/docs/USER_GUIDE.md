# Guía de Usuario — Galería PWA de Evidencia

Acceso: `https://camera.seguimiento360.com?token=<web-token>`

## Pantalla principal

```
┌─────────────────────────────────────────────┐
│ Evidencia visual          [Actualizar]      │
│ Prototipo aislado · gestión de evidencia    │
├─────────────────────────────────────────────┤
│ Captura remota                              │
│ Perfil guardado para próximas capturas: Día │
│ [Capturar imagen] [Capturar ráfaga] [Clip]  │
├─────────────────────────────────────────────┤
│ Configuración de la cámara                  │
│ Perfil activo: [Día ▼]  Editando: [Día ▼]   │
│ [Ajustes de perfil]  [Comunes]  [Guardar]   │
├─────────────────────────────────────────────┤
│ Evidencias                                  │
│ 12 evidencia(s)                             │
│ ┌─────┐ ┌─────┐ ┌─────┐                     │
│ │ 📷  │ │ 🎞  │ │ 📷  │  ...                │
│ └─────┘ └─────┘ └─────┘                     │
└─────────────────────────────────────────────┘
```

## Captura remota

| Botón | Acción | Uso típico |
|-------|--------|------------|
| **Capturar imagen** | Una sola foto con perfil activo | Verificación puntual |
| **Capturar ráfaga** | `burstCount` fotos (default 5) a `burstIntervalMs` | Secuencia de movimiento |
| **Capturar clip** | Secuencia JPEG por `clipDurationSeconds` | Vigilancia continua |

- Los botones se **bloquean** durante una captura activa.
- Panel inferior muestra: estado, progreso, evidencias recibidas.
- Al terminar, la galería se actualiza y el panel se oculta.

## Configuración

### Selectores de perfil
- **Perfil activo para próximas capturas**: el que usará la cámara al pulsar un botón de captura.
- **Perfil que estás editando**: borrador visual; cambiarlo **no** activa nada.

### Ajustes de perfil (por Día / Noche)
| Ajuste | Recomendación |
|--------|---------------|
| Resolución | `SVGA` (800×600) — equilibrio detalle/tamaño |
| Calidad JPEG | 10–12 — menor = más calidad, archivos mayores |
| Compensación poca luz | `auto` día / `night` noche |
| Ganancia máxima | 2× día / 8× noche (techo auto) |
| Flash | **Off** día / **On** noche (imagen y ráfaga) |
| Flash en clips | Normalmente **Off** |
| Estabilización flash | 120 ms (no tocar salvo pruebas) |
| **Avanzado** (detalle) | |
| Modo exposición | `auto` día / `manual` noche |
| Exposición manual | 200–300 día / 200 noche (unidades sensor, **no ms**) |
| Ganancia manual | 0–4 día / 4 noche (índice driver, no ISO) |

> **Regla práctica**: más exposición = más luz, pero más estela de movimiento. Ganancia alta = ruido.

### Ajustes comunes (ambos perfiles)
- Fotos por ráfaga (1–10), intervalo ráfaga (200–5000 ms).
- Intervalo clip (200–5000 ms), duración clip (10/30/60 s).
- SSID Wi‑Fi y contraseña (solo preferencia; no reinicia la cámara).
- Captura al evento / captura manual: preferencias guardadas, integración pendiente.

### Guardar
1. Elegir **perfil activo** y **perfil a editar**.
2. Ajustar valores.
3. **Mostrar contraseña** (checkbox) si se está tecleando.
4. Pulsar **“Guardar perfiles y configuración”**.
5. Mensaje “Perfiles guardados ✓ · Activo: Día/Noche”.
6. La contraseña se limpia y oculta automáticamente.

## Galería de evidencias

### Tarjeta de imagen
- Miniatura + metadatos (fecha, bytes, operationId).
- **Descargar**: JPEG original.
- **Eliminar**: confirmación → borra del servidor y microSD si aplica.

### Tarjeta de clip
- Miniatura del primer frame + metadatos (frames, intervalo, completo/incompleto).
- Si `complete:false` → controles deshabilitados.
- **Reproducir**: abre visor con navegación frame a frame.
- **Descargar**: ZIP con todos los frames originales.
- **Eliminar**: borra clip completo (frames + metadata).

### Estados de clip
| Etiqueta | Significado |
|----------|-------------|
| (sin etiqueta) | Clip completo, subida confirmada |
| **Incompleto** | Falta frames o subida no confirmada |

## Flujo típico de trabajo

1. **Verificar microSD**: abrir `/sd-status` (o botón “Actualizar” en UI) → `ready:true`, `write_read_verified:true`.
2. **Seleccionar perfil** según iluminación (Día/Noche).
3. **Posicionar/iluminar** escena; probar **Capturar imagen**.
4. Si hay movimiento esperado → **Capturar ráfaga** (5 fotos ~5 s).
5. Para vigilancia continua → **Capturar clip** (30 s, 1 foto/s).
6. Revisar galería → descargar / borrar según necesidad.
7. **Borrar pruebas** periódicamente para liberar microSD.

## Atajos y consejos

- **Ctrl+F5** / “Actualizar” → recarga galería y configuración.
- **Mostrar contraseña** solo revela lo que se está tecleando; lo guardado queda oculto.
- Ráfaga y clip **usan microSD** como buffer; si falla → “MicroSD no montada”.
- Clip sin flash → requiere iluminación ambiente suficiente.
- La cámara **no** se reinicia al guardar configuración; los cambios aplican en la siguiente captura.
- Tokens en URL (`?token=...`) para compartir acceso puntual sin cabecera.

## Limitaciones conocidas

- No hay vídeo contenedor (MP4/WebM); clips = secuencia JPEG.
- Exposición manual en unidades de sensor, no milisegundos.
- Ganancia manual es índice 0–30 del driver, no dB ni ISO.
- Flash: 120 ms = estabilización inicial; LED se apaga tras adquirir frame.
- Sin sincronización horaria precisa; `capturedAt` = recepción en servidor.
- MicroSD 1-bit; escritura lenta → intervalos mínimos reales ~1 s.
- Sin autenticación de certificado en cámara (`setInsecure()` prototipo).