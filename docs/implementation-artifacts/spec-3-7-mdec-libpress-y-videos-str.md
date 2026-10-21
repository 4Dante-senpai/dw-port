---
title: 'Historia 3.7: MDEC, libpress y videos STR'
type: 'feature'
created: '2026-10-21'
status: 'in-progress'
baseline_commit: '273ee948d41d421243fe1d70503b0e72bcdcdbd3'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/xa.md'
  - '{project-root}/docs/port/reference.md'
  - '{project-root}/docs/port/stubs.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Los videos STR del port se ven negros: `libpress` es un stub de la Historia 2.14 (`DecDCTout` llena ceros, `DecDCTvlc2` escribe un stream vacío) y no hay modelo de MDEC. Desde 3.6 el stream de CD, el anillo `St*` y el audio XA ya son reales, así que solo falta la imagen.

**Approach:** Implementar `port/backend/mdec.{h,c}`: comandos de decodificación, tabla de cuantización y tabla de escala de psx-spx, IDCT y salida en 15/24 bits, adaptado del `backend/mdec.c` CC0 de DW2-PC pero sin tablas embebidas. Traducir `libpress` del asm USA (`DecDCTReset`/`MDEC_reset`, `DecDCTin`, `DecDCTout`, `DecDCToutCallback`, `DecDCTvlcBuild`, `DecDCTvlc2`) con las tablas `mdec_iq`, `mdec_coef` y `data_2` leídas de `SLUS_010.32` en el Pack (AD-12). Agregar las Escenas `movie0` y `movie0_skip`, que comparan cada frame decodificado de OP1 y el VBlank de salida con Start contra PCSX-Redux.

## Boundaries & Constraints

**Always:**
- Atribución CC0 a DW2-PC en el código adaptado. Comentario `asm:` en cada función traducida.
- Nada de PCSX-Redux (GPL-2.0): el modelo sale de psx-spx y DW2-PC.
- Ningún byte del exe en `port/` ni `tools/port/`. Las tablas se cargan como las de `libgte`, con offsets generados desde `config/us/`.
- Frames y diffs solo fuera del repo; en el repo, métricas y hashes.
- L1 sin disco en nativo, ASan/UBSan y web, con tablas sintéticas y valores esperados calculados aparte (IDCT de referencia en Python).
- Siguen verdes `scene_title`, `scene_title_compare`, `reference_newgame_state`, `scene_title_audio_compare` y `scene_xa_op1_compare`.
- `make compare` sigue idéntico.
- Todo cambio en `src/` va detrás de `DW1_PORT` y tiene su fila en `docs/port/src-changes.md`.

**Never:**
- Calibrar costos de CPU/CD/MDEC: es 3.10. Acá un costo es una hipótesis anotada en `port/psyq/clock_costs.h`.
- Ventana SDL3 o presentación de video en el Host (3.9).
- Renderer WebGL2 (5.1).
- Tablas de cuantización o escala embebidas en C.
- `Speed` > 1 en PCSX-Redux.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Macrobloque sintético | run-level conocido, tablas sintéticas, 15 y 24 bits, `stp` 0/1 | píxeles iguales a los de la IDCT de referencia en Python, dentro del redondeo del hardware según psx-spx | N/A |
| Stream con EOB temprano o coeficientes saturados | run-level con códigos `0xFE00` y niveles de ±10 bits | bloques completados con ceros y saturación de psx-spx | N/A |
| `DecDCTvlc2` con versión de STR desconocida | cabecera inválida | mismo valor de retorno y salida que el asm | N/A |
| Tablas del exe ausentes | Pack sin `@exe` o exe corto | arranque abortado con mensaje, como `libgte_load_tables` | `PORT_ASSERT`/error de carga |
| Callback reentrante | el callback llama a `DecDCTout` | sin recursión de pila; se encadena por evento del reloj | N/A |
| OP1 completo | `playMovie(0)` desde el arranque | 3637 frames que cumplen la tolerancia; `MOV_playMovie` devuelve 1 | N/A |
| Start en el VBlank k | `movie0_skip.toml` | sale del video en el mismo VBlank que la referencia; devuelve 0 | N/A |
| Referencia ausente | sin captura | código 77 (skip) | N/A |

**Decisiones del dueño (2026-10-21):**
1. **Tiempo del MDEC (opción A):** el fin de `DecDCTout` es un evento del reloj (AD-8), con un costo hipotético por macrobloque en `port/psyq/clock_costs.h` que 3.10 calibra. Un bloque `DW1_PORT` en `MOV_waitForFlip` (`src/mov/mov.c:150`, ya clasificado `port_yield` en `isr_spins.tsv`) deja avanzar el reloj dentro del spin.
2. **Sincronía A/V (opción A):** en cada lado se mide el desfase entre el ciclo del último `PutDispEnv` de `MOV_playMovie` y el ciclo en que el decodificador XA entrega la última muestra del STR. Bloquea si |desfase_port − desfase_referencia| supera la duración media de un frame de video de la referencia (intervalo medio entre `PutDispEnv`).

</frozen-after-approval>

## Code Map

- `reference/dw2-pc/backend/mdec.{c,h}` -- base del MDEC: VLC MPEG-1, zigzag, IDCT entera, macrobloques 16×16 en 15/24 bits. Quitar las tablas `quant`/`scale` embebidas (`:193`, `:205`); vienen de los comandos 2/3 del MDEC. `Mdec_Vlc` no se usa: `DecDCTvlc2` se traduce del asm.
- `asm/us/main/psyq/libpress.s` -- fuente de la traducción. `MDEC_reset` (:12) envía `mdec_iq` y `mdec_coef` por `MDEC_in`. `DecDCTin` (:364) ajusta los bits 27/25 de la cabecera según el modo. `DecDCTvlc2` (:482) está escrita a mano, 0x33C bytes. `DecDCTvlcBuild` (:716) expande `data_2` en la tabla del juego. `DecDCTReset(0)` llama a `ResetCallback`. Las funciones `MDEC_*`/DMA se modelan en el Backend, no en registros.
- `asm/us/main/data/psyq/libpress.data.s` -- `mdec_iq` (:9), `mdec_coef` (:47), `data_2` (:157), sección `0x9BBD0` de `config/us/main.yaml:198`.
- `tools/port/psyq/gte_tables.py`, `port/psyq/libgte.c:62`, `CMakeLists.txt:94,206,449` -- patrón para cargar tablas del exe: generar el header de offsets, cargar al arrancar, test L0 y test clase B `*_tables_disc` por SHA-256 contra el exe. Generalizar o duplicar para `libpress`.
- `port/psyq/libpress.c` -- stubs a reemplazar. Conservar el encadenado sin recursión.
- `src/mov/mov.c:84-274` -- consumidor. `DecDCTin(...,3)` pide 24 bits con `stp`. Slices de 24 px vía `LoadImage` en `MOV_onSliceDecoded` (:84), que también llama a `StCdInterrupt`. Display `isrgb24` 320×240 en y=0/240. Salida con Start: `PadRead(1) & 0x800` (:265). Tabla de frames (:48-55): OP1 = 3637.
- `port/psyq/clock.{h,c}` -- `clock_schedule`/`clock_wait_until` para el evento de fin de MDEC (decisión 1). `port/psyq/clock_costs.h` todavía no existe: crearlo con la constante MDEC.
- `port/backend/gpu_soft/gpu_soft.c:660` -- `gpu_read_display` ya lee 24 bits. `tools/port/reference/capture.lua:325` saca una sola captura de 16/24 bits: extender a una captura por `PutDispEnv` dentro de la ventana `MOV_playMovie`, con clave = índice de frame decodificado. El Lua no está en la imagen, así que no hace falta reconstruirla.
- `tools/port/scenes/{run.py,xa_op1.toml,test_runner.py}` -- `compare_capture` (:69) es exacto. Agregar un modo de tolerancia por frame (máx ≤ 4/255 y media ≤ 0,5/255 por canal) que informe el peor frame y guarde su diff fuera del repo. Crear `movie0.toml` (base: `xa_op1.toml`) y `movie0_skip.toml` (Start en el VBlank k, salida medida por checkpoint).
- `port/psyq/tests/test_av_stubs.c`, `CMakeLists.txt:389` -- prueba del consumidor `MOV_playMovie(0)` con stubs: adaptarla al MDEC real con tablas sintéticas.
- `docs/port/stubs.md:25-30,65-69`, `tools/port/psyq/gen_stubs.py` -- quitar las filas de `libpress`.
- `docs/port/xa.md` -- referencia de estilo para el nuevo `docs/port/mdec.md`.

## Tasks & Acceptance

**Execution:**
- [ ] `port/backend/mdec.{h,c}` -- MDEC con comandos 1/2/3, IDCT y salida 15/24 bits; atribución CC0.
- [ ] `port/backend/tests/test_mdec.c` y un script Python de valores esperados -- L1 de la matriz (sintético, EOB, saturación, `stp`).
- [ ] `tools/port/psyq/` y `CMakeLists.txt` -- generar offsets de `mdec_iq`/`mdec_coef`/`data_2`; test L0 y test clase B de SHA-256 contra el exe.
- [ ] `port/psyq/libpress.c` -- traducción del asm, carga de tablas desde el Pack y evento de fin de MDEC (decisión 1).
- [ ] `port/psyq/clock_costs.h` -- costo MDEC como hipótesis, con referencia a 3.10.
- [ ] `src/mov/mov.c`, `docs/port/src-changes.md`, `tools/port/metrics/isr_spins.tsv` -- yield `DW1_PORT` en `MOV_waitForFlip`; el build PS1 no cambia.
- [ ] `port/psyq/tests/test_av_stubs.c` -- consumidor con el MDEC real.
- [ ] `tools/port/reference/capture.lua`, `capture.py` -- captura por frame decodificado y métrica de sincronía.
- [ ] `tools/port/scenes/{run.py,movie0.toml,movie0_skip.toml,test_runner.py}` -- comparación con tolerancia, sincronía A/V y salida con Start.
- [ ] `CMakeLists.txt`, `.gitlab-ci.yml` -- L1 en nativo/ASan/web; `reference_movie0`, `scene_movie0_compare` y `scene_movie0_skip_compare` como clase B (lock, skip 77, timeout de 2 h).
- [ ] `docs/port/mdec.md`, `docs/port/stubs.md`, `docs/port/reference.md`, `gen_stubs.py` -- modelo, atribución, hipótesis y métricas.

**Acceptance Criteria:**
- Given `run.py movie0 --compare`, when corre OP1 contra la referencia, then todos los frames cumplen máx ≤ 4/255 y media ≤ 0,5/255 por canal, y la sincronía A/V cumple la decisión 2.
- Given `movie0_skip.toml`, when se pulsa Start en el VBlank k, then `MOV_playMovie` vuelve en el mismo VBlank que en la referencia.
- Given los stubs de video reemplazados, when corren `title`, `newgame`, `title_audio` y `xa_op1`, then siguen pasando contra sus referencias.
- Given dos capturas de referencia de `movie0`, when se comparan, then los frames son idénticos byte a byte.
- Given el build web, when corre el L1, then pasa igual que en nativo.

## Implementation Notes

## Spec Change Log

## Review Triage Log

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless'` -- expected: verde; igual con `native-asan` y `web`.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh` (nativo y ASan, vía `tools/port/remote_run.sh`) -- expected: clase B verde sin omisiones, incluidas `scene_movie0_compare` y `scene_movie0_skip_compare`.
- `make compare` en `dw-build` -- expected: `Compiled binaries match with target binaries`.
