---
title: 'Historia 3.10: Calibración de costos y ritmo nativo'
type: 'feature'
created: '2026-10-24'
status: 'done'
route: 'dispatch'
baseline_commit: 'd79a22a49dda0c77b10478298c4b6c26555b3d0c'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-e3-ritmo-y-texto-sjis-nativo.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El reloj virtual (AD-8) usa costos hipotéticos y el port llega antes que la referencia a cada punto de control, cada vez con más ventaja: `runMainMenu` llega 41 VBlanks antes, `gameLoop` 359 y `randomLimit#17` 604 en `newgame`. El arranque hasta `MOV_playMovie` va unos 285 VBlanks adelantado. Hoy solo `movie0_skip` compara tiempos. Ninguna prueba mide la deriva de la ventana `native` en 10 minutos (NFR-1).

**Approach:** Medir en el port y en PCSX-Redux el VBlank de cada punto de control desde el frame 0, en `title`, `movie0` y una carga de mapa. Descomponer cada tramo por fuente de costo (CD, GPU, MDEC, CPU, tarjeta). Calibrar las constantes de `port/psyq/clock_costs.h` hasta que la diferencia sea 0 en cada punto. Agregar la prueba de 10 minutos de pared.

## Boundaries & Constraints

**Always:**
- Las constantes viven en `port/psyq/clock_costs.h`. Cada una lleva la Escena que la calibró y la medición de Redux que la respalda. Se seleccionan por estado del hardware (velocidad del CD, comando, tamaño), nunca por Escena.
- El reloj sigue determinista: dos corridas del mismo build dan la misma traza, y `native-headless`, `native-asan` y `native` dan los mismos VBlanks por punto de control.
- Sin regresiones: los hashes de video, audio y estado de todas las Escenas siguen pasando contra la referencia, o se vuelven a capturar y se explica el cambio. `movie0_skip` vuelve en el mismo VBlank que la referencia, sin `skip_vblank_delta`. `make compare` da idéntico.
- Los cambios en `src/` van detrás de `DW1_PORT` y se anotan en `docs/port/src-changes.md`.

**Never:**
- Tolerancia distinta de 0 en los puntos de control, constantes por Escena, o mover el latch del pad para tapar una diferencia de fase.
- Calibrar la web (épica 4), el modo acelerado (diferido) ni las métricas de audio no bloqueantes (siguen como reporte).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Punto de control igual | `run.py <id> --compare` | tabla `checkpoint ref port diff` con diff 0 y salida 0 | N/A |
| Punto de control distinto | diff ≠ 0 en algún punto | salida ≠ 0; nombra el primer punto distinto y los ciclos por fuente de costo del tramo, en ambos lados | N/A |
| Punto ausente en un lado | el símbolo no se alcanza | falla y nombra el punto | N/A |
| 10 minutos de pared | `native` con disco, 600 s × tasa de VBlank del reloj | \|VBlanks − pared × tasa\| < 1 y 0 resincronizaciones | falla con los dos valores |
| Pausa del host | host > 4 VBlanks atrasado | cuenta la resincronización; la prueba de 10 minutos falla si hay alguna | N/A |

## Decisions

- **Forma del modelo (dueño, 2026-10-24):** si ajustar números no alcanza para diferencia 0, se puede cambiar la forma del modelo cuando la descomposición por tramo lo justifique. Ejemplos: un seek por distancia, o un costo de CPU por función pesada medido en Redux. Cada cambio queda determinista, sin ajustes por Escena, y se registra en AD-8, que pasa a decir que la forma también se calibra.
- **Tasa de VBlank (dueño delegó; se eligió seguir al hardware):** la tasa de VBlank del reloj virtual es la que mide Redux en ciclos por VBlank. La ventana `native` se ritma a esa misma tasa, de modo que el audio (SPU a 44.100 Hz derivados de `CLOCK_HZ`) y la ventana no se separen. Si la medición difiere de 59,94 Hz, se corrige NFR-1 en el PRD, la arquitectura y `CLAUDE.md` con la medición como evidencia. Si coincide, nada cambia.
- **Entrada guionada insensible a la fase (dueño, 2026-10-25):** con la intro completa, `newgame` llegaba 120 VBlanks tarde a `setupNameSelectorBox#2`, mientras `map_load` daba 0 en ese punto. La diferencia venía de unos pocos ciclos en la fase de lectura del pad, que decidían qué pulsación de Start recibía el juego. Las Escenas mantienen Start (y las demás pulsaciones guionadas que dependan de la fase) presionado varios VBlanks, para que el port y Redux reciban la misma pulsación. Se vuelven a capturar las referencias afectadas. El criterio de diferencia 0 no cambia, y sigue prohibido mover el latch.

</frozen-after-approval>

## Code Map

- `port/psyq/clock.h:15-29` -- `CLOCK_HZ`, la razón de VBlank, `CLOCK_PSYQ_ENTRY_CYCLES` 64, `CLOCK_LINES_PER_FRAME`, `CLOCK_GPU_WORD_CYCLES` 2 y `CLOCK_GPU_PIXEL_CYCLES` 1. Todos son hipótesis. Los costos van a `clock_costs.h`; la razón de VBlank queda en `clock.h`.
- `port/psyq/clock_costs.h` -- MDEC por macrobloque, `CdlPause` 1×/2×/idle y tarjeta (comando y sector). Son el destino de todas las constantes.
- Constantes literales que hay que mover a `clock_costs.h`:
  - `port/psyq/libcd.c:23-25`: `CDROM_SEEK_CYCLES` y `SECTOR_CYCLES_1X/2X`, usadas en `:115`, `:177`, `:299` y `:346`.
  - `port/psyq/libcd_stream.c:138`: el sondeo de `StGetNext`, marcado "provisional, 3.10".
  - `src/mov/mov.c:168`: `clock_advance(1024)` del spin `MOV_waitForFlip`, bajo `DW1_PORT`.
  - `port/psyq/libspu.c:62,385`: DMA del SPU, 4 ciclos por byte.
- `port/psyq/libgpu.c:21-29,60-88` -- `account()` cobra 1 ciclo por píxel. `ClearImage` ya es asíncrono y `DrawSync` espera `busy_until`. El asm USA de `_clr` usa GP0(60h) para 480×480 (`build/verification-3-7/report.md`). Redux vuelve de `DrawSync` 245 ciclos después y el port 230.336 después. La tasa de llenado de rectángulo se calibra con Redux; no se cambia la ruta a fast-fill.
- `port/psyq/libetc.c:46-58`, `port/psyq/scene.c:271-292` -- latch y `PadRead`. El latch está bien: la fase difiere porque el bucle llega antes a 119 (frame 56 contra 45; primer MDEC en el VBlank 9 contra 29). Se arregla con los costos de los tramos anteriores, no acá.
- `port/psyq/scene.c:172-200,484-492`, `tools/port/reference/capture.lua:130-135,237,291,305-330` -- cada lado registra el VBlank del punto de control relativo a la marca. Redux lo registra absoluto, BIOS incluido, y el port no registra su frame 0. Hay que registrar el VBlank de cada punto relativo al frame 0 en ambos lados.
- `tools/port/scenes/run.py:184-245,411-457`, `tools/port/scenes/movie.py:107-110`, `test_runner.py:650` -- `compare_state` no compara tiempos (decisión 5 de 3.3), y las Escenas de video (`run_audio`) no registran puntos de control. Hay que agregar la comparación por punto de control a los dos caminos y quitar `skip_vblank_delta`.
- `tools/port/scenes/newgame.toml` -- llega a `gameLoop`. La carga de mapa entra con `loadMap` (`src/main/map.c:4029`). El fin hay que confirmarlo en Redux: candidato `checkArenaMap` (es una hipótesis). Si `loadMap` no ocurre dentro de `newgame`, se crea `map_load.toml` a partir de él.
- `tools/port/scenes/title.toml` -- no tiene puntos de control; se le agrega la marca `runLandingScreen` medida desde el frame 0.
- `port/host/sdl/video.c:46-60`, `tools/port/test_sdl_pacing.py`, `port/psyq/crt.c:144` -- ritmo de pared: acumulador exacto y resincronización con más de 4 VBlanks de atraso. Hay que contar las resincronizaciones y medir la pared desde el ancla, no desde el arranque del proceso.
- `docs/implementation-artifacts/deferred-work.md:38-40` -- la traza difiere entre `native-headless` y `native-asan`. Sigue abierto y bloquea la calibración. Las líneas 60-62 (`ClearOTagR`) y 100-102 (anillo de `StCdInterrupt`) entran solo si la descomposición muestra que mueven VBlanks.
- `CMakeLists.txt:576-645`, `.gitlab-ci.yml:63-100` -- tests de Escena (clase B, código 77 sin disco, `RESOURCE_LOCK`) y jobs `native-disc` y `native-asan-disc`.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/*` (según la causa) -- encontrar el primer evento distinto entre las trazas de `native-headless` y `native-asan` y hacer que el tiempo virtual no dependa del build. Cerrar la entrada de `deferred-work.md`.
- [x] `port/psyq/scene.c`, `crt.c`, `tools/port/reference/capture.lua` -- registrar en ambos lados el VBlank de cada punto de control y de la marca relativo al frame 0, con los ciclos acumulados por fuente de costo de cada tramo (CD, GPU, MDEC, tarjeta, entradas PsyQ y espera). En Redux, la descomposición sale de breakpoints en los comandos de CD, GPU y MDEC.
- [x] `tools/port/scenes/run.py`, `movie.py`, `test_runner.py` -- comparación de tiempos con salida según la matriz, en `--compare` y en las Escenas de video; quitar `skip_vblank_delta`. Tests unitarios con trazas sintéticas para el punto igual, el distinto y el ausente.
- [x] `tools/port/scenes/{title,movie0,newgame}.toml` (o `map_load.toml` nuevo) -- puntos de control de tiempo, incluida la carga de mapa.
- [x] `port/psyq/clock_costs.h`, `clock.h`, `libcd.c`, `libcd_stream.c`, `libgpu.c`, `libspu.c`, `src/mov/mov.c` -- mover las constantes y calibrarlas. Cada una lleva un comentario con su Escena y su medición de Redux, y pierde la marca `[hypothesis]` solo si quedó calibrada.
- [x] `docs/port/clock-calibration.md` (nuevo) -- tabla por Escena y tramo con los ciclos por fuente de costo en el port y en la referencia, antes y después.
- [x] `port/host/sdl/video.c`, `port/host/native_cli.c` -- contador de resincronizaciones y opción `--vblanks N`, que al salir imprime los VBlanks y la pared desde el ancla.
- [x] `tools/port/test_native_wall.py`, `CMakeLists.txt`, `.gitlab-ci.yml` -- prueba `native_wall_10min` (clase B, preset `native`, `SDL_VIDEODRIVER=offscreen`, `RUN_SERIAL`, TIMEOUT 900) dentro de `native-disc`.
- [x] `port/psyq/clock.h` -- medir en Redux los ciclos por VBlank y fijar la razón de VBlank del reloj a esa medición. La ventana de `video.c` toma la tasa del reloj, sin un 60000/1001 propio.
- [x] `docs/port/src-changes.md`, arquitectura (AD-8 con la forma calibrable y memlog con la tasa medida), PRD NFR-1 y `CLAUDE.md` si la tasa cambia -- registrar los cambios.

**Acceptance Criteria:**
- Given `title`, `movie0`, `movie0_skip` y la carga de mapa, when `run.py <id> --build native-headless --compare`, then cada punto de control da diferencia 0 y los hashes pasan.
- Given todas las demás Escenas (`newgame`, `naming`, `card_*`, `title_audio`, `xa_op1`), when corren contra la referencia, then siguen pasando. Las diferencias de tiempo se reportan sin bloquear, salvo en los puntos que comparten con las Escenas calibradas.
- Given `native_wall_10min`, when corre, then cumple la fila de 10 minutos de la matriz.
- Given los cambios, when corren `ctest` en `native-headless`, `native-asan`, `native` y `web`, más la clase B sin omisiones, then todo queda verde y `make compare` da idéntico.

## Implementation Notes

## Spec Change Log

## Review Triage Log

| # | Origen | Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|---|---|
| 1 | blind, edge, vgap | `psyq_strrchr` no encuentra `'\0'` | low | patch | `port/psyq/libc.c:37` recorre solo hasta el terminador. El juego busca solo `'\\'`, pero la corrección es directa. |
| 2 | blind, edge | `psyq_strrchr` compara un `signed char` con un `int` positivo | low | patch | Con `value` > 127 la comparación falla. Misma función; se corrige comparando como `unsigned char`. |
| 3 | blind, vgap, edge | `native_wall_10min` valida contra la tasa que imprime el propio ejecutable | medium | patch | `verify()` usa `rate_num`/`rate_den` del log. `main()` ya lee `clock.h` y no lo compara, así que una ventana coherente a otra tasa pasa. |
| 4 | blind, vgap, edge | El DMA GPU en modo lista enlazada registra `words` a partir del BCR | medium | patch | En `hardware.lua:159-179`, con `control & 0x400`, el BCR no aplica. Los resúmenes de calibración atribuyen palabras falsas. Ningún test cubre esa rama. |
| 5 | edge | `hardware.py` rechaza un DMA que empieza antes del frame 0 y termina después | false | rechazo | Falla en voz alta, y ninguna captura actual lo alcanza: las 4 suites pasan. No es un defecto demostrado. |
| 6 | blind, edge | `timing.py` no valida el orden de los puntos de control | low | rechazo | Las filas ya exigen ciclos monótonos. El orden solo afecta los costos de diagnóstico, y el arreglo agrega una guarda. |
| 7 | edge | `valid_timing_pair` no atrapa `KeyError` ni `TypeError` | low | patch | Si una captura en caché tiene otro esquema, aborta en vez de regenerarse. El arreglo es directo: ampliar el `except`. |
| 8 | blind | `clock-calibration.md` sigue mostrando ASan, SDL y suites como pendientes | low | patch | Las suites finales pasaron: headless 82/82, ASan 80/80, native 92/92, web 39/39 y la prueba de 10 minutos. Falta registrarlo. |
| 9 | blind | La tabla de antes y después mezcla guiones de entrada distintos | low | patch | La columna "Antes" usa los guiones viejos. Va una nota en el documento. |
| 10 | blind | `memcpy`/`memset` cobran el costo antes de copiar | maybe-false | defer | Si una IRQ despachada durante el avance lee el destino, ve un estado distinto. Se resuelve buscando callbacks que lean buffers en copia. |
| 11 | blind | Falta verificar aisladamente la geometría ISO y la caché de directorios | maybe-false | rechazo | No nombra un daño concreto. Los puntos de control en 0 cubren las rutas que usan las Escenas. |
| 12 | blind | Los cargos del código del juego no se comparan entre nativo y WASM | false | rechazo | El intento excluye calibrar la web (Never). `web_scene_title` compara con nativo y pasa. |
| 13 | orquestador | La fila "Pausa del host" no tiene un test que provoque un atraso real | medium | patch | `video.c:50` cuenta las resincronizaciones, pero ningún test fuerza un atraso de más de 4 VBlanks. `test_timing.py` solo prueba el análisis del log. |

## Design Notes

- **La referencia es Redux, no el hardware:** calibrar contra Redux puede dar costos que no son los del hardware (por ejemplo, que Redux termine la GPU casi enseguida). Eso es lo que pide la épica. Cada constante deja anotada la diferencia conocida con psx-spx para revisarla si cambia el emulador de referencia.
- **Descomponer antes de ajustar:** con una sola diferencia total por tramo no se distingue CD de CPU. La tabla por fuente evita compensar un error con otra constante, y es la evidencia que exige la regla 5.

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'for p in native-headless native-asan native web; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p || exit 1; done'` -- expected: verde.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh --preset native-headless -R 'scene|reference'` -- expected: sin omisiones, diferencia 0 en las Escenas calibradas.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/docker_run.sh ctest --preset native -R native_wall_10min` -- expected: \|Δ\| < 1 VBlank y 0 resincronizaciones.
- `make compare` en `dw-build` -- expected: `Compiled binaries match with target binaries`.
