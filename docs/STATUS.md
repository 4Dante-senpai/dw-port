# Estado del proyecto — DW1 Web Port

Última actualización: 2026-10-11

## Modo de operación

- Compilador matching (`cc_mips.dll`): **NO_DISPONIBLE** — modo sin compilador matching (regla 2 de `CLAUDE.md`).

## Hito actual

- M0 Entorno: **completo** (modo sin compilador matching).
- M1 Inventario: **completo** (`docs/research/`).

## Hecho

- Fase 0: repositorio inicializado desde `jype0/dw_decomp`, remotes configurados
  (`origin` = fork propio, `upstream` con push deshabilitado), `reference/dw2-pc` como submódulo,
  `.gitignore` extendido y hook pre-commit (`tools/port/hooks/`).

- Fase 1: imagen `dw-port` (emsdk 6.0.10, SDL3 3.4.16, Clang 18, CMake, Ninja); disco volcado
  (`SLUS_010.32` sha1 OK); `make regenerate` OK; `make TOOLCHAIN=gcc` OK; smoke test SDL3 nativo y
  web OK. Procedimiento en `docs/port/setup.md`.
- Fase 2: investigación en `docs/research/` (decomp-status, overlays, psyq-usage,
  hardware-access, dw2pc-architecture, web-constraints, risks) y scripts de métricas en
  `tools/port/metrics/`.

- Fase 3: BMAD 6.12.2 instalado (`docs/port/bmad.md`).
- Fase 4.1: product brief (`docs/planning-artifacts/briefs/brief-dw-port-2026-10-11/brief.md`).
- Fase 4.2: PRD aprobado por el dueño el 2026-10-11 con sus supuestos (speed-up queda para v2).

- Fase 4.3: arquitectura aprobada el 2026-10-11
  (`docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md`,
  AD-1..AD-18). AD-18 opción A: NFR-4 enmendado en el PRD. CI en el GitLab propio (AD-16).

- Fase 4.4: épicas e historias (`docs/planning-artifacts/epics.md`, 57 historias; Épica 2 = M2 …
  Épica 7 = M7, Épica 8 continua de decomp/upstream/CI).
- Fase 4.5: sprint planning (`docs/implementation-artifacts/sprint-status.yaml`, todo en `backlog`).

## En curso

- Fase 5: próxima historia 2.1 (esqueleto del build del port y lista generada de TUs).

## Bloqueos

- `cc_mips.dll` no disponible: sin `make` / `make compare` del build PS1.
- CI: falta definir cómo llega el repo de `origin` (GitHub) al GitLab propio; mientras tanto las
  clases A/B de AD-16 corren a mano en el contenedor `dw-port`.
- Emulador de referencia (PCSX-Redux / DuckStation headless) no instalado: planificado como
  historia de M3.

## Métricas

| Métrica | Valor |
|---|---|
| Funciones del juego en C / total (USA) | 2741 / 2741 |
| % descompilado del juego (bytes) | 100 % (931.800 B); PsyQ en asm: 612 funciones, 144.348 B (se reemplaza) |
| Funciones PsyQ usadas / cubiertas por DW2-PC | 202 / 81 |
| Archivos C del juego que compilan a wasm32 sin cambios | 97 / 120 |
| Escenas que funcionan (nativo) | 0 |
| Escenas que funcionan (web) | 0 |

## Notas

- El contexto Docker por defecto de esta máquina (`docker-server`) apunta a un host remoto; los
  comandos del port usan `DOCKER_CONTEXT=default` (daemon local) para que los bind mounts vean el
  repo. Durante la Fase 1 se lanzó por error un primer build contra el host remoto
  (192.168.0.172): quedó la imagen intermedia `dw-build` y `ubuntu:noble` en ese servidor.

- `origin` tenía una rama `master` previa con solo `LICENSE` (historia no relacionada); se dejó
  intacta y el trabajo se publica en `origin/main`.
