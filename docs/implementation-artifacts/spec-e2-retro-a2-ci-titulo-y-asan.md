---
title: 'Título con disco y bajo ASan/UBSan en CI (acción A2 de la retro de la Épica 2)'
type: 'chore'
created: '2026-10-17'
status: 'done'
route: 'oneshot'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** el criterio de la 2.15 ("ASan y UBSan no reportan errores" en la corrida del título)
solo se verificó a mano. `native-disc` (`.gitlab-ci.yml`) no corre `scene_title` y ningún job
compila `native-asan`. Además, los tests de clase B salen con 77 (skip) sin disco y CTest los
cuenta como no fallidos, así que un job de clase B puede quedar verde sin correrlos (hallazgo F2
de `epic-2-retro-2026-10-17.md`; alcance de la historia 8.2).

**Approach:** `native-disc` agrega `scene_title`; un job nuevo de clase B, `native-asan-disc`,
compila `native-asan` y corre su suite completa con disco (sin `reference_title`, que no depende
del preset). Los dos jobs corren CTest con un script que falla si algún test seleccionado se
saltea.

</frozen-after-approval>

## Implementation Notes

- Archivos: `tools/port/ci/ctest_no_skip.sh` (CTest con `--no-tests=error`; falla si la salida
  dice `did not run`, que es como CTest informa los tests salteados o no corridos),
  `.gitlab-ci.yml` (`scene_title` en `native-disc`; job nuevo `native-asan-disc`).
- Verificación local en la imagen `dw-port` con el disco: `native-asan` completo sin
  `reference_title`, 39/39 (incluye `scene_title`, `pack_disc`, `gte_tables_disc`,
  `dw1_overlay`); selección de `native-disc`, 4/4. Sin `DW1_DISC_DIR` el script sale con 1
  (skip) y con una selección vacía sale con 8. Falta la corrida en GitLab, que se dispara con el
  push a `main`.

## Review Triage Log

- Selección vacía sale con 0 (Blind Hunter) — low, **patch**: `--no-tests=error`.
- Archivo temporal sin borrar (Blind Hunter) — low, **patch**: `trap` de limpieza y salida si
  falla `mktemp`.
