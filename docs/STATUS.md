# Estado del proyecto — DW1 Web Port

Última actualización: 2026-10-11

## Modo de operación

- Compilador matching (`cc_mips.dll`): **NO_DISPONIBLE** — modo sin compilador matching (regla 2 de `CLAUDE.md`).

## Hito actual

- M0 Entorno: en curso.

## Hecho

- Fase 0: repositorio inicializado desde `jype0/dw_decomp`, remotes configurados
  (`origin` = fork propio, `upstream` con push deshabilitado), `reference/dw2-pc` como submódulo,
  `.gitignore` extendido y hook pre-commit (`tools/port/hooks/`).

## En curso

- Fase 1: entorno (contenedor, toolchain, extracción del disco, `make regenerate`).

## Bloqueos

- `cc_mips.dll` no disponible: sin `make` / `make compare` del build PS1.

## Métricas

| Métrica | Valor |
|---|---|
| Funciones en C / total | pendiente (Fase 2) |
| % descompilado (bytes) | pendiente (Fase 2) |
| Escenas que funcionan (nativo) | 0 |
| Escenas que funcionan (web) | 0 |

## Notas

- `origin` tenía una rama `master` previa con solo `LICENSE` (historia no relacionada); se dejó
  intacta y el trabajo se publica en `origin/main`.
