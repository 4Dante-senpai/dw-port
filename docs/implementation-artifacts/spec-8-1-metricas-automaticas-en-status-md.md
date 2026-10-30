---
title: 'Historia 8.1: Métricas automáticas en STATUS.md'
type: 'feature'
created: '2026-10-30'
status: 'draft'
baseline_commit: 'cb6fbd4f8b9d5f620fd0518365aaa7f98c4f482b'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-8-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Las métricas de `docs/STATUS.md` se copian a mano y ya divergen del repo (la tabla dice 94 apariciones de `DW1_PORT`; hay 117 bloques y `src-changes.md` documenta 65 en tablas). Nada falla cuando un bloque `DW1_PORT` queda sin fila (NFR-10).

**Approach:** `tools/port/metrics/status_table.py` calcula las cinco métricas de la Historia 8.1, imprime una tabla Markdown, puede reescribir un bloque marcado de `STATUS.md` y sale con error si los bloques `DW1_PORT` no coinciden con las filas de `src-changes.md`. Se completa `src-changes.md` para que el chequeo pase y se registra como test de CTest de clase A.

## Boundaries & Constraints

**Always:**
- Bloque `DW1_PORT` = línea `#if`/`#ifdef`/`#ifndef`/`#elif` que nombra `DW1_PORT`, en cualquier archivo de `src/` e `include/`. Fila = fila de una tabla de `src-changes.md` cuya columna `Archivo` es un path entre backticks bajo `src/` o `include/`. La comparación es por archivo; el total se imprime.
- PsyQ: denominador = funciones de las tablas por librería de `docs/research/psyq-usage.md` (202). Implementada = definición C en `port/` (sin `tests/`) cuyo comentario inmediatamente anterior contiene `dw2-pc:`, `asm:` o `stub-m2:`; se imprime el desglose por marcador.
- Métrica no calculable en el entorno → valor `sin verificar (<motivo>)`, salida 0. Solo NFR-10 hace fallar.
- Código y comentarios en inglés; docs en español. Sin cambios en `src/` ni `include/`.

**Never:** correr `make regenerate`, el build PS1 ni Escenas desde la herramienta; leer el disco; inventar valores que no se midieron.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Repo coherente | bloques = filas por archivo | tabla con 5 métricas, exit 0 | N/A |
| Fila faltante | archivo con 3 bloques y 2 filas | tabla + `src/x.c: 3 bloques, 2 filas` en stderr | exit 1 |
| Fila sobrante | fila de un archivo sin bloques | igual que arriba con 0 bloques | exit 1 |
| Sin `asm/us/` | checkout sin `make regenerate` | `Funciones del Juego en C / total`: `sin verificar (falta asm/us/)` | exit 0 |
| Sin JUnit | sin `--junit` | Escenas nativo/web: `sin verificar (sin resultados de CTest)` | exit 0 |
| JUnit | `--junit` de `ctest --output-junit` | Escena pasa si `scene_<s>_compare` o `scene_<s>_state` (nativo) / `web_scene_<s>` (web) pasó; skipped o failed no cuenta | JUnit ilegible → exit 2 |
| `--update` | archivo con `<!-- status_table:begin -->`…`end` | reemplaza solo ese bloque | sin marcadores → exit 2; con NFR-10 en rojo no escribe |

</frozen-after-approval>

## Code Map

- `tools/port/metrics/decomp_status.py` -- `segment_status()` y `main()` suman C/asm por unidad desde `asm/us/` y `config/us/*.yaml`; extraer los totales a una función reutilizable sin cambiar su salida.
- `docs/research/psyq-usage.md` -- tablas `| \`Nombre\` | …` por librería: lista de las 202.
- `port/psyq/*.c`, `port/backend/` -- 187 definiciones de las 202 (las 15 restantes son libc del Host); 184 con marcador. Faltan en `port/psyq/libcd.c`: `CdReadSync` (un `extern` separa su comentario), `CdIntToPos`, `CdPosToInt`.
- `docs/port/src-changes.md` -- tabla 1 (columna `Archivo` en 2.ª posición), tabla de 4.1 (1.ª) y secciones en prosa (3.3, 3.7, 3.10) sin filas. Hoy 117 bloques vs 65 filas; difieren 20 archivos (p. ej. `src/main/sound.c` 14/0, `src/main/file.c` 6/0).
- `tools/port/scenes/*.toml` -- nombres de Escena; `CMakeLists.txt:598-668,728` -- tests `scene_<s>_compare`, `scene_newgame_state`, `web_scene_title`.
- `CMakeLists.txt:286` -- estilo `add_test(... python3 -I -B ... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`.
- `docs/STATUS.md` sección `## Métricas` -- fila manual de `DW1_PORT` desactualizada.

## Tasks & Acceptance

**Execution:**
- [ ] `tools/port/metrics/decomp_status.py` -- exponer `totals(version)` -> `{"game": [nc, bc, na, ba], "psyq": …}` y usarla en `main()` -- reutilización sin duplicar el parseo.
- [ ] `tools/port/metrics/status_table.py` -- CLI `[--root DIR] [--junit FILE]... [--update FILE]` con las cinco métricas, chequeo NFR-10 y códigos 0/1/2 -- AC de la historia.
- [ ] `tools/port/metrics/test_status_table.py` -- unittest sobre repos sintéticos en directorio temporal: cada fila de la matriz -- cubrir bordes sin disco.
- [ ] `port/psyq/libcd.c` -- comentario `asm:` en `CdIntToPos`/`CdPosToInt` y `extern` antes del comentario de `CdReadSync` -- convención de AD (fuente nombrada por función).
- [ ] `docs/port/src-changes.md` -- una fila por bloque para cada archivo que difiere, con motivo tomado de la prosa existente y verificado leyendo el bloque -- NFR-10 en verde.
- [ ] `CMakeLists.txt` -- tests `status_table_unit` y `status_table_repo` (corre la herramienta sobre el repo; falla con NFR-10 rojo) -- evidencia automática en clase A.
- [ ] `docs/STATUS.md` -- bloque con marcadores generado con `--update`, quitar la fila manual de `DW1_PORT` y listar las métricas sin verificar en este entorno -- pedido del dueño.

**Acceptance Criteria:**
- Given el repo en la rama, when se corre `status_table.py`, then imprime las cinco métricas, PsyQ da `187 / 202` con desglose y bloques = filas = 117, exit 0.
- Given CTest configurado en cualquier preset, when corre `status_table_repo`, then falla si se agrega un bloque `DW1_PORT` sin fila.

## Implementation Notes

## Spec Change Log

## Review Triage Log

## Verification

**Commands:**
- `python3 -I -B tools/port/metrics/test_status_table.py` -- expected: todos OK.
- `python3 -I -B tools/port/metrics/status_table.py` -- expected: exit 0, 117/117, PsyQ 187/202.
- `ctest -R status_table` (preset `native-headless`, si configura en el entorno) -- expected: 2/2.

**Sin verificar en este entorno** (sin disco, `asm/` ni `cc_mips.dll`): funciones del Juego en C / total (requiere `make regenerate`), Escenas que pasan (requieren disco y Redux; solo se prueba el parseo JUnit con datos sintéticos), `make compare` (no se toca `src/`).
