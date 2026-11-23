# Seguridad del Prototipo

## Modelo de amenazas

| Activo | Riesgo | Mitigación |
|--------|--------|------------|
| Tokens (`camera.token`, `web.token`) | Filtración → acceso a cámara/galería | Archivos fuera de repo, 600, comparación constante-tiempo |
| Evidencia (fotos/clips) | Acceso no autorizado | Autenticación obligatoria en todos los endpoints |
| Credenciales Wi‑Fi | Compromiso red local | Solo preferencia guardada; no se propaga automáticamente |
| MicroSD física | Extracción y lectura | Sin cifrado (prototipo); almacenar cámara en recinto seguro |
| Túnel Cloudflare | Intercepción HTTPS | TLS terminación en Cloudflare; origen HTTP local aislado |
| Firmware | Ingeniería inversa, modificación | Sin secrets en binario ideal; `setInsecure()` solo prototipo |

## Autenticación

### Cámara → Servidor
- Cabecera `X-Camera-Token: <token>`
- Token ≥32 chars, aleatorio, almacenado en `data/camera.token` (chmod 600).
- Verificación: `timingSafeEqual(sha256(token_recibido), sha256(token_esperado))`.
- Usada en: `POST /api/upload`, `POST /api/camera-status`, `GET /api/capture-command`, `POST /sd-recover`.

### Navegador → Servidor
- Cabecera `X-Web-Token: <token>` **o** query `?token=<token>`.
- Mismo token/comparación que cámara, archivo `data/web.token`.
- Usada en: todos los endpoints `/api/*`, `/health`, `/`, `/app.js`, `/manifest`.

### Buenas prácticas de tokens
- Generar con `openssl rand -hex 32` o equivalente.
- Rotar periódicamente; actualizar ambos lados simultáneamente.
- Nunca en logs, URLs permanentes, repositorio, ni firmware.
- `Cache-Control: no-store` en respuestas autenticadas.

## Red y aislamiento

```
Internet ── Cloudflare TLS ──► Servidor (127.0.0.1:8787) ◄── HTTPS saliente ── Cámara (192.168.10.10)
                                                      ▲
                                                      │ Sin acceso entrante a cámara
                                                      │ VLAN separada
```

- **Cámara**: solo conexiones **salientes** (HTTPS a servidor, DNS, NTP si configurado).
- **Servidor**: escucha solo en `127.0.0.1:8787` (o `0.0.0.0:8787` dentro de contenedor/host aislado).
- **Cloudflare Tunnel**: termina TLS, reenvía HTTP al servidor local.
- **VLAN**: servidor no puede iniciar conexión a `192.168.10.0/24`.
- **Alarma**: pila independiente en `/home/antonio/iot-alarma/` (Docker Compose), sin red compartida con prototipo.

## HTTPS y certificados

- **Público**: Cloudflare gestiona certificado válido para `camera.seguimiento360.com`.
- **Local (servidor)**: HTTP plano en `127.0.0.1:8787` — red de confianza.
- **Cámara**: `WiFiClientSecure` con `setInsecure()` → **no valida certificado** (prototipo).
  - Producción: cargar CA de Cloudflare o certificado pinned.
- **HSTS / CSP**: servidos por Cloudflare; servidor añade `Content-Security-Policy` en `/`.

## Almacenamiento y evidencia

### En servidor
- `data/evidence/*.jpg` + `*.jpg.json` (metadatos: `operationId`, `capturedAt`).
- `data/clips/clip-*/` con frames `f001.jpg`… y `metadata.json`.
- Permisos: `700` directorios, `600` archivos; propietario usuario del servicio.
- Borrado: `DELETE` endpoints autenticados; borra archivo + sidecar o directorio clip completo.

### En microSD (cámara)
- `/clips/clip-<uint64>/` — frames + `metadata.json` (`complete` flag).
- `/burst-<operationId>/` — temporal, se limpia tras subida exitosa.
- `/diagnostics/probe-*.txt` — sonda escritura/lectura.
- **Sin cifrado** (prototipo). Tarjeta extraíble → riesgo físico.

### Integridad
- Subida: `Content-Length` verificado; `201` solo si bytes coinciden.
- Sidecar JSON registra `operationId` y `capturedAt` (recepción servidor).
- Clip: `metadata.complete=true` solo tras subida total verificada.
- SHA-256 de archivos calculable a posteriori para cadena de custodia.

## Configuración y secretos

### Archivos sensibles (no versionar)
```
/home/antonio/camera-evidence-prototype/data/
├── camera.token      # 32+ chars, chmod 600
├── web.token         # 32+ chars, chmod 600
├── camera-config.json# Incluye wifiPassword (ofuscada en API)
```

### Contraseña Wi‑Fi
- Guardada solo como **preferencia** en `camera-config.json`.
- API devuelve `hasPassword: true/false` — **nunca el valor**.
- UI: campo `type=password`, placeholder “(guardada, sin cambios)”.
- Checkbox “Mostrar contraseña” **solo revela texto en edición**; al guardar se limpia y oculta.
- No se envía a la cámara salvo reinicio manual de credenciales (fuera de alcance del prototipo).

### Firmware
- `CAMERA_TOKEN` y `WEB_TOKEN` como literales en `.ino` **solo en despliegue**.
- Para versionar: plantilla `tokens.h.in` + inyección en CI, o archivo `secrets.h` en `.gitignore`.
- Binario compilado **no** debe contener tokens en claro si se distribuye.

## Validación y sanitización

- Entradas JSON: límites de tamaño (4 KB estado, 1 KB comando, 900 KB upload).
- `operationId` UUID v4 validado por regex.
- `evidenceId` / `clipId` / `frame` validados por regex estrictos.
- `activeProfile` ∈ `{day,night}`; `profiles.*` objetos, claves permitidas (`PROFILE_FIELDS`).
- Números: clamps en rangos definidos; strings: longitud máxima.
- Path traversal: IDs validados antes de `join()`; no rutas de usuario directas.

## Cabeceras de seguridad (servidor)

```http
Cache-Control: no-store
Content-Security-Policy: default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Referrer-Policy: same-origin
```

## Rotación y respuesta a incidentes

| Evento | Acción inmediata |
|--------|------------------|
| Token comprometido | Generar nuevo par, actualizar `data/*.token` y firmware, reiniciar ambos |
| Evidencia sustraída (microSD) | Revocar tokens, rotar credenciales Wi‑Fi, auditar acceso físico |
| Acceso no autorizado a galería | Revocar `web.token`, revisar logs `/prototype.log` y Cloudflare |
| Firmware modificado | Re-flashear versión conocida buena desde backup verificado |

## Auditoría

- Logs servidor: `prototype.log` (stdout/stderr), rotar semanal.
- Logs cámara: monitor serie (115200) — **no dejar abierto** en producción.
- Endpoints de diagnóstico autenticados: `/sd-status`, `/sd-recover`, `/clip-status`.
- `diagnostics/` con resultados de pruebas automatizadas (hashes SHA-256 de evidencia).

## Limitaciones conocidas (prototipo)

- `setInsecure()` en cámara → MITM posible en red local.
- Sin cifrado en microSD.
- Tokens en firmware como literales.
- Sin rotación automática de tokens.
- Sin firma digital de evidencia (solo hash a posteriori).
- Contraseña Wi‑Fi en config JSON (aunque no expuesta por API).

> **Este prototipo no cumple estándares forenses ni de producción**. Está diseñado para validación funcional aislada.