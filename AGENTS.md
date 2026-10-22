# AGENTS.md

Reglas para agentes de IA que trabajen en este repositorio.

## Antes de tocar nada

1. Lee [docs/README.md](docs/README.md).
2. Lee [docs/gobernanza/README.md](docs/gobernanza/README.md).
3. Si vas a tocar firmware, lee [docs/firmware/specs/00-gobierno-sdd.md](docs/firmware/specs/00-gobierno-sdd.md).
4. Si vas a tocar backend, lee [docs/backend/README.md](docs/backend/README.md).

## Reglas de documentación

- La documentación vive **solo** en `docs/`.
- No crees `.md` fuera de `docs/` salvo `README.md`, `AGENTS.md`, `SECURITY.md` y `CONTRIBUTING.md`.
- Un tema = un documento. No dupliques contenido entre archivos.
- Lo obsoleto se mueve a `docs/_archivo/`; no se borra.
- Cada documento empieza con un H1 y un párrafo de propósito.
- Enlaces relativos, nunca rutas absolutas.
- Idioma: español.

## Reglas de seguridad

- No escribas secretos, tokens, contraseñas, IPs privadas ni claves en el repo.
- Usa `.env` y variables de entorno; los ejemplos van en `.env.example` con valores falsos.
- No versiones `build/`, `sdkconfig`, `settings.js` reales ni binarios de flasheo.
- Si encuentras una credencial, repórtala según [SECURITY.md](SECURITY.md); no la uses.

## Reglas de código

- No inventes APIs, endpoints ni campos que no existan. Si falta información, escribe `TODO(fuente): ...`.
- Cambios pequeños y atómicos. Un commit por tema.
- No reformatees archivos completos si no es necesario.
- Respeta la estructura existente de `firmware/`, `backend/`, `frontend/`.

## Comandos de verificación

```bash
npx --yes markdownlint-cli2 "**/*.md"
bash scripts/check-docs.sh
mkdocs build --strict
```

## Estructura

```text
docs/                 documentación canónica
firmware/             código ESP32
backend/              flujos Node-RED y SQL
frontend/             PWA
nodered/data/         plantilla de settings.js
infrastructure/       configuración de servidor (pendiente)
hardware/             BOM/PCB (pendiente)
scripts/              operación
tests/                pruebas
```
