---
title: 'Historia 8.1: Métricas automáticas en STATUS.md'
type: 'feature'
created: '2026-10-30'
status: 'done'
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
- [x] `tools/port/metrics/decomp_status.py` -- exponer `totals(version)` -> `{"game": [nc, bc, na, ba], "psyq": …}` y usarla en `main()` -- reutilización sin duplicar el parseo.
- [x] `tools/port/metrics/status_table.py` -- CLI `[--root DIR] [--junit FILE]... [--update FILE]` con las cinco métricas, chequeo NFR-10 y códigos 0/1/2 -- AC de la historia.
- [x] `tools/port/metrics/test_status_table.py` -- unittest sobre repos sintéticos en directorio temporal: cada fila de la matriz -- cubrir bordes sin disco.
- [x] `port/psyq/libcd.c` -- comentario `asm:` en `CdIntToPos`/`CdPosToInt` y `extern` antes del comentario de `CdReadSync` -- convención de AD (fuente nombrada por función).
- [x] `docs/port/src-changes.md` -- una fila por bloque para cada archivo que difiere, con motivo tomado de la prosa existente y verificado leyendo el bloque -- NFR-10 en verde.
- [x] `CMakeLists.txt` -- tests `status_table_unit` y `status_table_repo` (corre la herramienta sobre el repo; falla con NFR-10 rojo) -- evidencia automática en clase A.
- [x] `docs/STATUS.md` -- bloque con marcadores generado con `--update`, quitar la fila manual de `DW1_PORT` y listar las métricas sin verificar en este entorno -- pedido del dueño.

**Acceptance Criteria:**
- Given el repo en la rama, when se corre `status_table.py`, then imprime las cinco métricas, PsyQ da `187 / 202` con desglose y bloques = filas = 117, exit 0.
- Given CTest configurado en cualquier preset, when corre `status_table_repo`, then falla si se agrega un bloque `DW1_PORT` sin fila.

## Implementation Notes

- Implementado directamente, sin Codex (pedido del dueño). Rama `claude/historia-8-1-epics-401pfj`.
- `decomp_status.py`: en lugar de `totals(version)` quedaron `unit_rows(version)` (las filas por unidad que ya usaba `main()`) y `totals(rows)`; la salida de `main()` no cambia. `status_table.py` fija `decomp_status.ROOT` a `--root` antes de llamarlas.
- El conteo del Code Map era incompleto: el parser real encuentra 81 filas (no 65) y difieren 8 archivos (36 bloques sin fila), no 20. La fila 4.1 de `src/main/main.c` decía `checkCameraMovement (retorno)`, pero el bloque de `main.c` (en `initializeLoadedMap`) declara `memcpy`; se corrigió el símbolo.
- Filas nuevas en `src-changes.md`: tablas al final de las secciones en prosa 3.3 (dos), 3.7 y 3.10 (dos), una fila por bloque, con motivo y verificación tomados de la prosa existente y del código de cada bloque.
- PsyQ: 187/202 con marcador; las 15 restantes son libc del Host (no definidas en `port/`). `ClearOTagR`/`DrawOTag` llevan `dw2-pc:` pero el Juego no las llama directamente, por eso `dw2-pc:` da 0.
- `libcd.c`: solo comentarios y el `extern` movido; el código sin comentarios es idéntico al de la base (comparado por tokens). No hay headers i686 en el contenedor para compilarlo.
- CTest: el preset `native-headless` no configura acá (sin multilib i686); los tests se registraron y corrieron en un configure del host sin `-m32` (`status_table_unit` y `status_table_repo` pasan). Prueba negativa: un `#ifdef DW1_PORT` agregado a `src/main/fish.c` hace fallar `status_table_repo` con `src/main/fish.c: 2 DW1_PORT blocks, 1 rows`.
- JUnit verificado contra la salida real de CTest 3.28 (`status="run"`, `fail` con `<failure>`, `notrun` con `<skipped>`).

## Spec Change Log

## Review Triage Log

Pasada 1 (subagentes Claude en lugar de Codex, por pedido del dueño). BH = blind hunter, VG = verification gap, EC = edge case hunter.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| BH-1 / EC-11 | low | rechazado | Real: el chequeo NFR-10 compara cantidades por archivo y no el símbolo de cada fila (la fila `checkCameraMovement` de `main.c` se encontró a mano). Es lo que pide el frozen (cantidad). Validar símbolos por fila agrega lógica de parseo de celdas heterogéneas (`#include`, `15 extern`, …). |
| BH-2 / EC-10 | low | rechazado | El conteo por línea `#if/#elif` es la definición del frozen. No hay guardas con continuación `\` ni dentro de comentarios en `src/`/`include/` (grep sin resultados). |
| BH-3 | medium | defer | Real: nada compara el bloque de `STATUS.md` con la salida actual. Requiere un modo nuevo de la herramienta; anotado en `deferred-work.md`. |
| BH-4 / EC-5 | low | rechazado | Las 15 no definidas en `port/` son la libc del Host (documentada en `stubs.md`); el denominador 202 lo fija la historia. Listarlas en cada corrida sería ruido. |
| BH-5 | false | rechazado | La fila manual "cubiertas por DW2-PC" mide si DW2-PC define el nombre (`psyq-usage.md`), no el comentario del port; las métricas miden cosas distintas. `ClearOTagR`/`DrawOTag` llevan `dw2-pc:` pero el Juego no las llama. |
| BH-6 / EC-6 | low | rechazado | Verificado: ninguna de las 202 tiene más de una definición en `port/` fuera de `tests/`. Un comentario separado por otra línea se reporta en stderr y ahora hace fallar `status_table_repo`, no es silencioso. Una definición con el tipo de retorno en otra línea sí se reconoce (prefijo opcional). |
| BH-7 / EC-7 | medium | patch | Real: native, native-headless y native-asan registran los mismos `scene_*`; con último-gana el orden de `--junit` cambiaba el resultado. Ahora gana el peor estado (failed > passed > skipped); test con ambos órdenes. |
| BH-8 / EC-8 | low | patch | Hoy ninguna Escena tiene `_compare` y `_state` a la vez, pero la regla debía exigir que no falle ninguna compuerta. Corregido y testeado. |
| BH-9 | low | rechazado | Las tablas nuevas están bajo el título de la sección con la historia (3.3, 3.7, 3.10); la fecha está en el historial de git. |
| BH-10 / EC-13 | low | patch | Escritura atómica con `mkstemp` + `os.replace`, conservando el modo del archivo. El párrafo de `STATUS.md` ya no lleva fecha fija fuera del bloque. |
| BH-11 | low | rechazado | Los helpers `pos_to_int`/`int_to_pos` ya citaban el asm de esas mismas funciones; los comentarios nuevos solo nombran la fuente en la entrada pública, como pide la arquitectura. El código sin comentarios es idéntico. |
| BH-12 | low | rechazado | `decomp_status.ROOT` global funciona y su único importador es `status_table.py`. La salida incompleta de splat ya no rompe la herramienta (EC-2). |
| VG-1 | medium | patch | Real: el fixture no tenía subsegmentos `psyq/`. Agregado `psyq/libcd` y aserción sobre `totals(...)["psyq"]`. |
| VG-2 | medium | patch | Real: un marcador perdido no fallaba en CTest. `FAIL_REGULAR_EXPRESSION "without a source comment"` en `status_table_repo`; prueba negativa moviendo el `extern` de `CdReadSync` → falla. |
| EC-1 | low | patch | Un archivo de entrada faltante daba traceback con exit 1 (confundible con NFR-10). Ahora `OSError` → exit 2; test. |
| EC-2 | low | patch | `asm/us/` parcial ahora da `sin verificar (salida de splat incompleta: …)`; test. |
| EC-3 | low | patch | En la imagen PyYAML viene del venv (`requirements.txt`), pero el import de `decomp_status` pasó a ser diferido para no depender de él sin `asm/`. |
| EC-4 | low | patch | `encoding="utf-8"` en todas las lecturas y escrituras. |
| EC-9 | low | rechazado | CTest siempre escribe `name`; agregar la guarda no corrige ningún caso real. |
| EC-12 | low | patch | El marcador de fin se busca después del de inicio; test con un `end` suelto antes. |
| EC-14 | low | rechazado | Desvío ya anotado en Implementation Notes: `unit_rows(version)` + `totals(rows)`; no hay otros llamadores. |
| EC-15 | false | rechazado | La historia pide cinco métricas; Escenas se imprime en dos filas (nativo y web), igual que en la tabla manual de `STATUS.md`. |

## Verification

**Commands:**
- `python3 -I -B tools/port/metrics/test_status_table.py` -- expected: todos OK.
- `python3 -I -B tools/port/metrics/status_table.py` -- expected: exit 0, 117/117, PsyQ 187/202.
- `ctest -R status_table` (preset `native-headless`, si configura en el entorno) -- expected: 2/2.

**Sin verificar en este entorno** (sin disco, `asm/` ni `cc_mips.dll`): funciones del Juego en C / total (requiere `make regenerate`), Escenas que pasan (requieren disco y Redux; solo se prueba el parseo JUnit con datos sintéticos), `make compare` (no se toca `src/`).
