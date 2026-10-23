# Gobernanza

Contratos y prompts que definen cómo se diseña y se valida el sistema.

## Precedencia

De mayor a menor:

1. `PROMPT-SDD-MASTER.md` y `PROMPT-SDD-SERVER.md` (contratos §6 gemelos).
2. Specs aprobadas en `docs/firmware/specs/`.
3. Documentación de implementación: `docs/hardware/`, `docs/patrones/`, `docs/fases/`.
4. Operación: `docs/infraestructura/`.
5. Documentos históricos: `docs/_archivo/`.

Si dos documentos se contradicen, gana el de mayor precedencia y se corrige el otro.

## Documentos

- [Prompt maestro de firmware](PROMPT-SDD-MASTER.md)
- [Prompt maestro de servidor](PROMPT-SDD-SERVER.md)

## Regla para agentes

Antes de tocar código, un agente debe leer `AGENTS.md`, este archivo y el spec correspondiente. Si falta una decisión, se propone y se registra; no se inventa.
