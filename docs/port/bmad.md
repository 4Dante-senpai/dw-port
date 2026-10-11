# BMAD en este proyecto

Instalado el 2026-10-11 con:

```sh
npx bmad-method install --directory . --modules bmm --tools claude-code --yes \
    --user-name Demian --communication-language Spanish --document-output-language Spanish \
    --output-folder docs --set bmm.user_skill_level=expert --set bmm.project_knowledge=docs \
    --all-stable
```

Versión: **BMad Core 6.12.2 + BMad Method (bmm) 6.12.2**. Configuración en `_bmad/bmm/config.yaml`:
artefactos de planificación en `docs/planning-artifacts/`, de implementación en
`docs/implementation-artifacts/`, conocimiento del proyecto en `docs/`. Comunicación y documentos
en español.

En esta versión los agentes y workflows se instalan como **skills** de Claude Code en
`.claude/skills/` (29). No hay comandos `/bmad:*` ni selección explícita de "track": el track
"BMad Method" (PRD + Arquitectura) es la secuencia de skills del módulo `bmm` de abajo. El
módulo de game dev de BMAD no se instala (apunta a Unity/Unreal/Godot).

## Nombres instalados (usar exactamente estos)

Agentes: `bmad-agent-analyst`, `bmad-agent-pm`, `bmad-agent-architect`, `bmad-agent-dev`,
`bmad-agent-ux-designer`.

Workflows del track BMad Method, en orden:

| Fase | Skill | Salida |
|---|---|---|
| Análisis | `bmad-product-brief` (alternativa: `bmad-prfaq`) | product brief |
| Planificación | `bmad-prd` | PRD |
| Planificación (opcional) | `bmad-ux` | solo para la shell web |
| Solución | `bmad-architecture` | arquitectura + ADRs |
| Solución | `bmad-create-epics-and-stories` | épicas e historias |
| Implementación | `bmad-sprint-planning` | `sprint-status` |
| Implementación | `bmad-build` (por historia: aclarar, planificar, implementar, revisar) | código + artefactos |
| Implementación | `bmad-code-review` | revisión adicional |
| Implementación (opcional) | `bmad-qa-generate-e2e-tests`, `bmad-retrospective`, `bmad-correct-course` | |

Utilidades: `bmad-help`, `bmad-project-context`, `bmad-spec`, `bmad-review`,
`bmad-advanced-elicitation`, `bmad-brainstorming`, `bmad-party-mode`, `bmad-deep-recon`,
`bmad-forge-idea`, `bmad-walkthrough`, `bmad-build-auto`, `bmad-customize`.
