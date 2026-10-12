---
title: 'Historia 2.6: Linkeo completo y arranque hasta el frame 0'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: '10494df87634b092fa1a627627b1242276f742ce'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El Juego compila pero no existe un ejecutable: faltan unas 190 funciones PsyQ, el
crt, el punto de entrada del Host y los valores del linker de CodeWarrior que lee `initializeHeap`.

**Approach:** Un ejecutable `dw1` (`native-headless`) linkea los 119 objetos del Juego con la Capa
PsyQ del port, un stub generado en build para cada función PsyQ que todavía falta, el crt del port
(hilo del juego con stack estático en la ventana, snapshot de overlays) y un Host headless que
corre N frames.

## Boundaries & Constraints

**Always:** el `main` del Juego se renombra en build (`-Dmain=dw1_game_main` solo para
`src/main/main.c`); los cambios en `src/` van detrás de `DW1_PORT` con fila en `src-changes.md`;
`native-headless` y `native-asan` verdes; sin datos del disco (el Juego corre sin Pack).

**Never:** implementar las librerías (historias 2.7–2.14); Pack o lectura real del CD.

**Decisiones (autónomas):**
- Stubs: `tools/port/psyq/gen_stubs.py` cruza los símbolos sin resolver de los objetos del Juego
  con `tools/port/psyq/psyq_functions.tsv` (nombres y librería de las 612 funciones PsyQ, como en
  `config/us/symbols.txt`) y resta lo que define `dw1_psyq`. Cada stub registra su primera llamada
  en `$DW1_TRACE` y devuelve 0 o el valor de `stub_returns.tsv` cuando 0 colgaría al Juego.
- La `libc` de PsyQ queda con la del host; `setjmp`/`longjmp` se mapean a `_setjmp`/`_longjmp`
  (28 bytes, caben en el `jmp_buf` de 48 de PsyQ). `rand` determinista llega en la 2.9.
- `__rts_info_t__.h` del port renombra `_end`/`_stack_*`; sus valores PS1 salen de
  `config/us/main.ld` y `bss.yaml` (`tools/port/units/rts_info.py`).
- Frame = retorno de `VSync(0/n)`; `--frames N` sale con 0 al terminar el frame N-1. Watchdog: 30 s
  virtuales sin frame → sale con 3.
- Contador de cobertura PsyQ: lo imprime `gen_stubs.py` en el build (usadas / implementadas por
  `dw1_psyq` / libc del host / stubs), en lugar del comentario de fuente.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Frame 0 | `dw1 --headless --frames 1` | sale con 0 al retorno del primer `VSync` | N/A |
| Traza | ídem con `DW1_TRACE` | una línea por stub, en orden de ciclo | N/A |
| Cuelgue | el Juego espera algo que un stub no da | sale con 3 tras 30 s virtuales | mensaje con frame y ciclo |
| ASan | `native-asan` | frame 0 sin errores de ASan/UBSan | N/A |

</frozen-after-approval>

## Code Map

- `src/main/main.c:3371` `main`, `:3532` `initializeHeap` (`_end`, `_stack_addr`, `_stack_size`).
- `config/us/main.ld:197-209` -- `_stack_addr = LONG(0x801FFF00)`, `_stack_size = LONG(0x40)`, `_end = LONG(main_BSS_END)`.
- `src/main/file.c` `readFile` -- `while (CdControl(...) == 0);`, `CdRead`, `CdReadSync`.
- `asm/us/main/psyq/libgs.s` -- `GsSetWorkBase`/`GsGetWorkBase` (`GsOUT_PACKET_P`).
- `src/main/efe.c:120` -- `char MAIN_D_8012342C[16]` sin terminador (ASan).

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/psyq/{list_functions.py,psyq_functions.tsv,gen_stubs.py,stub_returns.tsv}`
- [x] `port/psyq/{crt.c,crt.h,stub.c,libgs.c,libgs_data.c}`, `port/psyq/include/__rts_info_t__.h`, `tools/port/units/rts_info.py`
- [x] `port/host/headless/main.c`
- [x] `port/psyq/include/port_game.h` -- `setjmp`/`longjmp`.
- [x] `src/main/efe.c` -- bloque `DW1_PORT`.
- [x] `CMakeLists.txt`, `CMakePresets.json` -- `dw1`, test `frame0`, ASan con runtime compartido.

**Acceptance Criteria:**
- Given los dos presets, when `ctest`, then todo pasa, incluido `frame0`.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde

## Implementation Notes

- Frame 0 en el ciclo 565.044 (primer VBlank), con 53 stubs llamados antes (`frame0_trace.log`). Cobertura en el build: `psyq: used=199 implemented=4 host_libc=13 stubs=182 gte_stubs=11` (199 y no 202: el conteo de `psyq-usage.md` sale del asm original; este, de los objetos del port).
- Iteraciones hasta el frame 0: `CdControl`/`CdRead` devuelven 1 y `CdReadSync` 0 (`stub_returns.tsv`); `GsSetWorkBase`/`GsGetWorkBase` traducidos del asm (con NULL el Juego escribía primitivas en la dirección 0).
- **ASan** encontró `MAIN_D_8012342C` sin terminador (`src/main/efe.c`), corregido detrás de `DW1_PORT`.
- **UBSan** (`-fsanitize=function`) marca llamadas por puntero con tipo distinto (`world_object.c:101`). Inocuas en i686, pero en wasm `call_indirect` aborta: registrado como pregunta en la memlog de la arquitectura y como AC de la Historia 4.1; `native-asan` corre con `-fno-sanitize=function`.
- **Ventana de OT:** con ASan la imagen no entraba en 16 MB (texto instrumentado de 8,8 MB, runtime estático en `.bss`). La ventana nativa pasa a empezar en `__data_start` (el código no necesita estar dentro) y `native-asan` usa `-shared-libasan`. AD-5 actualizado.
- `_end`/`_stack_addr`/`_stack_size` verificados contra `SLUS_010.32` (rom `0x83AB4` = `0x801BFB64`, `0x83A9C` = `0x801FFF00`, `0x83AA4` = `0x40`).
- Evidencia: `docs/implementation-artifacts/evidence/story-2-6.log` (`native-headless` 18/18, `native-asan` 16/16).

## Spec Change Log

## Review Triage Log

Primera ronda de revisores cortada por el límite de sesión; se relanzó completa.

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `dw1` no verifica que su imagen entera entre en la ventana (base ahora dinámica) | medium | Verificado. **patch**: `port_run_game` exige `game_stack`, `Ps1_Ram` y `_end` en la ventana; fatal en todo build. |
| 2 | El mapeo `setjmp`→`_setjmp` no lo ejercita ningún test | medium | Verificado (el intérprete de scripts no corre antes del frame 0). **patch**: `test_setjmp` con canario detrás de un `jmp_buf` de PsyQ. |
| 3 | `rts_info.py` sin test; `bss.yaml end` = `main_BSS_END` sin verificar | medium | **patch**: `test_rts_info.py`; valores verificados contra `SLUS_010.32`. |
| 4 | El watchdog (salida 3) nunca se ejercita | medium | **patch**: `test_watchdog` (Juego falso que nunca termina un frame). |
| 5 | Comentario de `__rts_info_t__.h` dice que el heap queda en la arena; `InitHeap3` recibe la dirección PS1 cruda | medium | Verificado. **patch**: comentario corregido; requisito anotado para la 2.9. |
| 6 | `defined_by_port` no incluye `dw1_runtime`; resta de definidos incluye símbolos locales | low | **patch**: ambas librerías y `--extern-only`. |
| 7 | `nm` fijo; TSV con líneas en blanco; nombres desconocidos en `stub_returns.tsv` ignorados | low | **patch**: `${CMAKE_NM}`, filtro de líneas vacías, error si un nombre no existe. |
| 8 | `--frames` no numérico o ≤ 0 corre sin límite | low | **patch**: rechazo con salida 2; test `dw1_bad_args`. |
| 9 | `fopen` de la traza falla en silencio | low | **patch**: mensaje en stderr. |
| 10 | `frame0` no verifica el mensaje ni ausencia de reportes de ASan/UBSan; orden estable | low | **patch**: el test exige `reached frame 0`, ningún `ERROR: AddressSanitizer`/`runtime error`, y `sort -s`. |
| 11 | `GsIDMATRIX` en cero | low | **patch**: identidad (`ONE` = 4096) con `_Static_assert` de tamaño. |
| 12 | rpath del runtime de ASan fijo en el preset | low | **patch**: CMake lo obtiene con `clang -print-runtime-dir`. |
| 13 | Bloque de `setjmp` separando el comentario de `PS1_RAM_ADDR`; vale solo para glibc | low | **patch**: movido al final, solo fuera de Emscripten, marcado [hipótesis] para web. |
| 14 | Stubs `long f()` sin prototipo fallarían en wasm | low | **patch** en `epics.md`: la AC de la 4.1 incluye stubs con firma; comentario `ponytail:` en el generador. |
| 15 | Menciones viejas de la ventana fija en `epics.md`/`STATUS.md`/contexto | low | **patch**. |
| 16 | `psyq_functions.tsv` y `sprint-status.yaml` fuera del diff revisado | false | Los excluí del diff a mano (tamaño); están en el commit. |
| 17 | `rand` del host (RAND_MAX distinto) rompe `randomLimit` | medium | **defer** a la 2.9, que implementa el `rand` de PsyQ; antes del frame 0 no se llama. |
| 18 | Stubs que devuelven NULL (p. ej. `malloc3`) esconden fallos | medium | **defer** a la 2.9 (`malloc3`) y 2.14 (stubs con comportamiento definido). |
| 19 | Watchdog no salta si el Juego hace un spin en C puro | low | Rechazado: el inventario `isr_spins` (2.2) cubre esos loops y `ctest` tiene `TIMEOUT`. |
| 20 | `-fno-sanitize=function` quita el único detector de llamadas por puntero con tipo distinto | medium | **defer**: registrado como AC de la Historia 4.1 y pregunta en la memlog. |
