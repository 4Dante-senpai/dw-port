---
title: 'Historia 2.10: Modelo GTE y macros inline'
type: 'feature'
created: '2026-10-13'
status: 'done'
baseline_commit: '1c84b90785f6a4574c7692a9d02bbda7e2befd6d'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** las 11 funciones `port_gte_*` a las que el `inline_n.h` del port mapea las macros
GTE del Juego son stubs generados (`gte_stubs=11`): la geometría 3D no se calcula. Además no
existe el preset `web` que exige AD-17.

**Approach:** adaptar el modelo C del GTE de DW2-PC a `port/backend/gte.{h,c}` con los comandos
que usa DW1, implementar las 11 funciones con la semántica exacta de cada macro, cubrirlo con
vectores propios derivados de psx-spx y agregar el preset `web` que compila los objetos del Juego
y el Backend sin linkear.

## Boundaries & Constraints

**Always:** cabecera `/* Adapted from DW2-PC (CC0): psyq/gte.c @ 816ed0d */` (y `gte_core.h`);
nombres del Backend con prefijo `gte_` (`gte_reset`, `gte_mtc2`, `gte_mfc2`, `gte_ctc2`,
`gte_cfc2`, `gte_command`), sin colisión con los nombres de macros PsyQ; `-Wall -Wextra -Werror`
limpio con clang `-m32` y con emcc; accesos a memoria con `memcpy` (punteros sin alinear); sin
cambios en `src/`/`include/`; `native-headless` y `native-asan` verdes; L0a OK.

**Never:** canal lateral subpíxel (`Gte_PreciseHook`, M5); `libgte` ni tablas (Historia 2.11);
linkear o correr el build `web` (Historia 4.1); arreglar los warnings de `dw1_psyq` bajo emcc
(fuera del preset `web` de esta historia); usar `gte_ref.bin` ni los vectores de DW2-PC.

**Decisiones (autónomas):**
- Comandos modelados = los que emiten las macros y `asm/us/main/psyq/libgte.s`: RTPS, RTPT,
  NCLIP, AVSZ3, AVSZ4, MVMVA, NCCS, NCCT (DW1 no usa NCDS, NCS ni GPF: no se portan). Un comando
  no modelado, o MVMVA con `mx=3`/`cv=2`, es `PORT_ASSERT`.
- RTPT = tres pasos RTPS sobre V0..V2 con FLAG borrado una vez; MAC0/IR0 del cue de profundidad
  solo del último vértice (psx-spx).
- Las macros traducen su asm de `include/gcc/inline_n.h`: `ldv0`/`ldv3` = `lwc2` de VXY/VZ;
  `stsxy` = `swc2` SXY2; `stsxy3` = SXY0/1/2; `stszotz` = `(int32_t)SZ3 >> 2` en 32 bits;
  `ldlzc` = `mtc2` LZCS; `stlzc` = `swc2` LZCR; `SetRotMatrix` = 5 palabras a RT0..RT4;
  `SetTransMatrix` = palabras en +20/+24/+28 a TRX/TRY/TRZ.
- Las operaciones GTE no avanzan el reloj virtual (son inline, no entradas PsyQ)
  [hipótesis: costo despreciable; se valida en la Épica 3]. Estado inicial: registros en cero,
  `LZCR = 32`, como `Gte_Reset` de DW2-PC.
- Esperados de los tests: calculados aparte con un script de una sola vez a partir de las
  fórmulas de psx-spx y escritos como constantes (como la 2.9).
- Preset `web`: toolchain `$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`,
  Debug, build preset con targets `dw1_game_objs` y `dw1_backend`. Medido: los 119 TUs del
  Juego ya compilan con emcc gracias al `inline_n.h` del port (2.1); la historia lo fija como
  preset reproducible.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Cargas | `ldv0`/`ldv3` con VZ negativo; `SetRotMatrix` con RT33 negativo | registros con extensión de signo de 16 bits | N/A |
| LZC | `ldlzc` de 0, 1, -1, `0x80000000`, `0x00FFFFFF`; `stlzc` | 32, 31, 32, 1, 8 | N/A |
| RTPS normal | cámara y vértice dentro de rango | SXY2, SZ3, IR1..3, MAC0, IR0 y FLAG=0 iguales a los esperados | N/A |
| RTPS saturación | IR1/IR2 fuera de rango; SZ3<0 y >0xFFFF; H ≥ 2·SZ3; SX2/SY2 fuera de ±0x400; IR0 fuera de 0..0x1000 | valor saturado y bit de FLAG correspondiente; bit 31 solo por bits de error | N/A |
| RTPT | tres vértices | FIFO SXY0..2 y SZ0..3 desplazadas; IR0/MAC0 del tercero; `stsxy3` y `stszotz` | N/A |
| NCLIP / AVSZ | triángulo horario y antihorario; ZSF3/ZSF4 con OTZ > 0xFFFF | signo de MAC0; OTZ saturado con FLAG bit 18 | N/A |
| MVMVA | formas de `libgte.s`: (1,0,0,3,0), (1,0,3,3,0), (0,0,0,3,0), (0,0,3,3,0) | MAC/IR esperados, `lm` respetado | N/A |
| NCCS / NCCT | luz y color con desborde | RGB0..2 de la FIFO saturados con bits 21..19 | N/A |
| Comando ajeno | `gte_command` con un código no modelado | abort | `PORT_ASSERT` |

</frozen-after-approval>

## Code Map

- `port/backend/gte.h` -- hoy solo prototipos `port_gte_*`; se agrega la API `gte_*` y los
  índices de registros (de `reference/dw2-pc/psyq/gte_core.h`).
- `port/psyq/include/inline_n.h` -- mapeo de las 11 macros; no cambia.
- `reference/dw2-pc/psyq/gte.c` -- fuente: flags `:18`, MAC/IR/SZ/SXY/color `:51-123`, UNR
  `:125-154`, RTPS `:156`, NCLIP `:216`, MVMVA `:236`, NCS (base de NCCS) `:260`, dispatch
  `:278`, mtc2/mfc2 con LZC `:299-383`. Quitar `GteC_*`, `psyq_log`, `PreciseHook`, GPF, NCS.
- `include/gcc/inline_n.h:9,30,228,294,362,498,504,976,978,1152,1382` -- asm de cada macro.
- `asm/us/main/psyq/libgte.s` -- conjunto de comandos y formas MVMVA usadas.
- `port/psyq/port.h` -- `PORT_ASSERT` (el Backend ya lo incluye en `cdrom.c`).
- `CMakeLists.txt:123` -- `dw1_backend`; `:222` gen_stubs recibe solo `dw1_psyq`/`dw1_runtime`.
- `tools/port/psyq/gen_stubs.py` -- regla `port_gte_*`; docstring a actualizar.
- `CMakePresets.json` -- presets actuales; agregar `web`.

## Tasks & Acceptance

**Execution:**
- [x] `port/backend/gte.h`, `port/backend/gte.c` -- modelo adaptado + las 11 `port_gte_*`.
- [x] `port/backend/tests/test_gte.c` -- matriz completa; abort en hijo con `RLIMIT_CORE=0`.
- [x] `CMakeLists.txt` -- `gte.c` en `dw1_backend`; test `gte`; `dw1_backend` en la lista de
  libs del port de gen_stubs.
- [x] `tools/port/psyq/gen_stubs.py` -- docstring sin "until the GTE model exists".
- [x] `CMakePresets.json` -- configure/build preset `web`.

**Acceptance Criteria:**
- Given los dos presets nativos sin `DW1_DISC_DIR`, when `ctest`, then todo pasa, `gte` y
  `frame0` incluidos.
- Given el build, when corre `gen_stubs.py`, then imprime `stubs=169 gte_stubs=0`.
- Given la imagen `dw-port`, when `cmake --preset web && cmake --build --preset web`, then compila
  los 119 TUs del Juego y `dw1_backend` sin errores.
- Given `DW1_DISC_DIR`, when `ctest -R 'pack_disc|dw1_overlay'`, then siguen pasando.

## Implementation Notes

- Adaptación de `psyq/gte.c` y `psyq/gte_core.h` de DW2-PC `816ed0d`, con atribución CC0.
  Se conservan los registros y el divisor UNR; se agregan RTPT, AVSZ3/4 y NCCS/NCCT.
  Los comandos excluidos y los selectores MVMVA no modelados usan `PORT_ASSERT`.
- RTPT acumula FLAG entre vértices y calcula el cue de profundidad solo para V2.
  Las traslaciones negativas se multiplican por 4096 para evitar desplazamientos indefinidos
  bajo UBSan. El estado estático y `gte_reset` dejan LZCR en 32.
- Las once macros conservan su mapeo; sus operandos de memoria usan `memcpy`.
  No hay cambios en `src/`, `include/` ni `port/psyq/include/inline_n.h`.
- El test `gte` incluye vectores propios constantes, FIFOs, saturaciones y desbordes,
  selección `sf`/`lm`, memoria sin alinear, reloj sin avance y abortos en procesos hijos
  con `RLIMIT_CORE=0`. El cálculo independiente queda registrado en la evidencia.
- `web` construye únicamente `dw1_game_objs` y `dw1_backend`; no construye `dw1_psyq`
  ni linkea o ejecuta el juego. No se incorporan funciones ni tablas de `libgte`.

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Si `dw1_backend` sale de la lista de gen_stubs, los stubs `port_gte_*` vuelven a satisfacer al Juego sin que falle nada (blind + verification gap) | medium | Verificado: `test_gte` enlaza el modelo directo y `frame0` no mira geometría. **patch**: `gen_stubs.py` sale con error si generaría un stub `port_gte_*`; probado quitando el Backend (rc=1). |
| 2 | El preset `web` no corre en CI (blind + verification gap) | medium | Verificado: `.gitlab-ci.yml` solo tiene `native`. **patch**: job de clase A `web-objects`. |
| 3 | El test MVMVA con `v=3` carga los mismos valores en V0 e IR: leer V0 por error pasaría (blind) | low | Verificado (`test_gte.c:328`). Corrección directa. **patch**: V0 distinto cuando `v=3`. |
| 4 | Con `NDEBUG`, `PORT_ASSERT` no aborta (comandos ajenos, tests de abort) (blind, edge ×2) | false | Todos los presets son Debug; mismo criterio que la 2.8 (#9) y la 2.9 (#8). |
| 5 | `gte_mtc2/mfc2/ctc2/cfc2` sin chequeo de rango de `reg` (blind, edge) | low | Solo se llaman con constantes del enum; el guard agrega ramas. Rechazado. |
| 6 | `GTE_CMD_MVMVA` sin validar el rango de sus argumentos (edge) | low | Solo se usa con constantes 0..3/0..1. Rechazado. |
| 7 | Operandos NULL en cargas y stores de las macros (edge ×2) | low | El Juego no pasa NULL; en la consola también fallaría. Rechazado. |
| 8 | `waitpid` fallido deja `status` sin inicializar en el test (edge) | low | Solo test; el test falla igual por el primer `CHECK`. Rechazado. |
| 9 | Falta RTPS con `sf=0` y cociente no nulo (blind) | low | DW1 solo emite RTPS/RTPT con `sf=1` (`inline_n.h`, `libgte.s`). Rechazado. |
| 10 | Saturaciones de color solo con los tres canales juntos: un cambio de bits pasaría (blind) | low | Riesgo bajo; requiere vectores nuevos. Rechazado. |
| 11 | Faltan casos UNR con SZ3=1, (H,SZ3)=(0,0) y más normalizaciones (blind) | low | La rama de overflow y la normalización ya están cubiertas (`near_equal`, `unr*`); vectores nuevos. Rechazado. |
| 12 | NCLIP/AVSZ no verifican que otros registros queden intactos (blind) | low | Requiere estado y asserts nuevos sin defecto observado. Rechazado. |

Post-patch: `native-headless` 24/24 + 2 skips, `native-asan` 22/22 + 2 skips, `stubs=169 gte_stubs=0`,
preset `web` OK. Los parches no tocan el camino de clase B (`pack_disc`/`dw1_overlay` ya pasaron).

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde, `gte_stubs=0`
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web'` -- expected: sin errores
- ídem nativo con `DW1_DISC_DIR` montado -- expected: `pack_disc` y `dw1_overlay` pasan
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK

**Resultados (2026-10-14 UTC, dentro del contenedor existente):**
- `cmake --preset native-headless && cmake --build --preset native-headless`: exit 0;
  `env -u DW1_DISC_DIR ctest --preset native-headless`: 24 pasan, 2 skips de clase B.
- `cmake --preset native-asan && cmake --build --preset native-asan`: exit 0;
  `env -u DW1_DISC_DIR ctest --preset native-asan`: 22 pasan, 2 skips de clase B.
- `gte`, `frame0`, `preproc_check` (L0a) y `test_preproc_check`: pasan en ambos presets.
- Ambos builds nativos imprimen `stubs=169 gte_stubs=0`.
- `cmake --preset web && cmake --build --preset web`: exit 0; 119 objetos del Juego y
  los 2 TUs del Backend. `gte.c` limpio con `-Wall -Wextra -Werror` en los tres presets.
- `DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset <nativo> -R 'pack_disc|dw1_overlay'`:
  2/2 pasan tanto en `native-headless` como en `native-asan`, sin skips.
- `git diff --check` y comprobación de ausencia de cambios en `src/`, `include/` y
  `port/psyq/include/inline_n.h`: exit 0.

Evidencia y cálculo independiente de vectores: [story-2-10.log](evidence/story-2-10.log).
No se crearon commits. Sin pendientes dentro del alcance; la calibración temporal y paridad
contra el emulador siguen en la Épica 3, `libgte` en 2.11 y el linkeo/ejecución web en 4.1.
