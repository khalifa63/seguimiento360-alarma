# Política de seguridad

## Reportar una vulnerabilidad

No abras un issue público con credenciales, tokens o detalles explotables. Escribe a:

**antonio.califa@gmail.com**

Incluye: versión o commit, pasos para reproducir, impacto y evidencia (sin datos de terceros).

## Credenciales

- Las credenciales reales viven en `.env` o en el gestor de secretos del servidor. Nunca en Git.
- `.env.example`, `settings.js.example` y `secrets.h.example` solo contienen valores falsos.
- Si una credencial se filtra: **rótala primero**, luego elimina el archivo del árbol y del historial. Borrar el archivo no invalida la credencial.
- Los modelos `:free` de OpenRouter pueden usar los prompts para entrenamiento. No envíes secretos ni datos de clientes a modelos gratuitos.

## Alcance

Este repositorio contiene firmware, flujos Node-RED, PWA y documentación. Quedan fuera de alcance los servidores de producción, Cloudflare y los proveedores de notificaciones; para esos casos contacta al responsable técnico.
