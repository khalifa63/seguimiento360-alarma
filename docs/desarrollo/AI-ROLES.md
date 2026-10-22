# Roles de IA para el desarrollo

> Matriz de asignación de modelos por área para Seguimiento 360. El agente
> orquestador de DeepSeek Harness lee este archivo al iniciar el desarrollo y
> deriva cada tarea a un subagente con el modelo indicado.

## Orquestador (agente principal de DSH)

| Campo | Valor |
|---|---|
| Rol | Coordinar, leer documentación, planificar, editar y ejecutar comandos |
| Proveedor | `openrouter` |
| Modelo | `~deepseek/deepseek-flash-latest` |
| Configuración | `agent-default-model` en `cordis.patch.yml` |

El orquestador corre las tareas mecánicas y le delega el trabajo que requiera
razonamiento profundo a los subagentes de la matriz. Los comandos de build y
flasheo se ejecutan contra el servidor Ubuntu, ya sea por SSH o pegándolos por
terminal.

## Matriz de modelos

| Área | Tarea | Modelo | Alternativa económica | Uso |
|---|---|---|---|---|
| Orquestación | SSH, `idf.py`, logs, edición | `~deepseek/deepseek-flash-latest` | — | Siempre |
| Firmware del hub | FreeRTOS, FSM, registros, punteros | `~deepseek/deepseek-pro-latest` | `~deepseek/deepseek-flash-latest` | Escribir y editar |
| Firmware del hub — revisión | Seguridad, concurrencia, OTA | `anthropic/claude-sonnet-5.5` | `~deepseek/deepseek-pro-latest` | Solo revisión crítica |
| Nodos ESP32-C3 | Batería, deep sleep, ESP-NOW | `~deepseek/deepseek-pro-latest` | `~deepseek/deepseek-flash-latest` | Escribir y editar |
| Web / panel admin | Frontend, usabilidad | `anthropic/claude-sonnet-5.5` | `~deepseek/deepseek-flash-latest` | Solo si es React/Next; la PWA actual es vanilla |
| Análisis y estrategia | Leer marco teórico, PDFs, validación | `google/gemini-4-argon` | `openai/gpt-6-sol` | Documentos largos |
| Tareas mecánicas | Refactors, boilerplate, migraciones | `~deepseek/deepseek-flash-latest` | `recursive/flint-v1.0:free` | Trabajo simple |

## Slugs de modelos (OpenRouter)

- `~deepseek/deepseek-flash-latest` — DeepSeek V4.1 Flash (rápido y barato).
- `~deepseek/deepseek-pro-latest` — DeepSeek V4 Pro (razonamiento profundo).
- `anthropic/claude-sonnet-5.5` — revisor de firmware/seguridad.
- `google/gemini-4-argon` — contexto de 1M para documentos largos.
- `openai/gpt-6-sol` — análisis exploratorio económico.
- `recursive/flint-v1.0:free` — tareas mecánicas sin costo.

> Verifica el slug exacto en https://openrouter.ai/models antes de usarlo, pues
> OpenRouter puede cambiarlo o retirar variantes gratuitas.

## Reglas de orquestación

1. Separa **autor** y **revisor**: escribe con DeepSeek, revisa lo crítico con Claude.
2. No uses Claude para todo; es el modelo más costoso.
3. Un subagente por unidad de trabajo; no mezcles varios temas en uno.
4. Antes de tocar código, lee `AGENTS.md` y `docs/desarrollo/PLAN-DE-ARRANQUE.md`.
5. Siempre plan antes de código y cita `archivo:línea`.
6. Nunca envíes secretos, `.env`, `settings.js` reales ni `build/` a un modelo remoto.
7. Al terminar, actualiza la spec o la documentación que cambió.
