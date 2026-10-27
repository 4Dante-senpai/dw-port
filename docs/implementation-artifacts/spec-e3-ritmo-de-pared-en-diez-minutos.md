---
title: 'Corrección E3: native_wall_10min vuelve a 0 resincronizaciones'
type: 'bugfix'
created: '2026-10-26'
status: 'done'
route: 'dispatch'
baseline_commit: '0352b300bc1aadc9e7cce616a3c7b7c6cdc4d2a2'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-e3-gate-de-imagen-y-espera-de-audio-en-vsync.md'
  - '{project-root}/docs/port/clock-calibration.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `native_wall_10min` falla con el código de `81fb7e2` (historia 3.10). Desde ese commit no cambió nada en `port/` ni en `src/`.
- **En este servidor:** `docker_run.sh` con `DOCKER_CONTEXT=default`, corrida aislada: 9 resincronizaciones con 67–72 ms de atraso (el tope es 4 VBlanks, 66,7 ms) y `underruns=20`. Las resincronizaciones caen en pares, cerca de los VBlanks 7694/7712, 8294, 15827, 16426, 23954, 24551, 32075 y 32672: cada ~8130 VBlanks.
- **En el PC del dueño:** 1 a 2 resincronizaciones.
- **Antes:** la 3.10 registró 0 resincronizaciones en una corrida aislada (`docs/port/clock-calibration.md:206-208`).

NFR-1 y el gate `native-disc` exigen 0 resincronizaciones.

**Approach:** encontrar con evidencia qué hace llegar tarde al hilo del juego en esos VBlanks y corregirlo en la causa, hasta que `native_wall_10min` dé 0 resincronizaciones y error < 1 VBlank en este servidor, de manera repetible.

Hipótesis sin verificar, para descartar primero: la contrapresión de audio aceptada en A3. `produce` (`port/host/sdl/audio.c`) duerme hasta 100 ms por bloque. Con `SDL_AUDIODRIVER=dummy`, que consume en bloques, la espera puede pasarse, por ejemplo cuando un video cobra CPU modelada de golpe (`DecDCTvlcBuild`, unos 200 ms).

Decisión del dueño (2026-10-26, retro de la Épica 3): la Épica 3 se cierra cuando este test pase. La contrapresión de audio sigue siendo la excepción aceptada de AD-8: se puede ajustar su forma (cuánto y hasta cuándo espera), pero no quitarla ni moverla de lugar sin una nueva decisión del dueño.

## Boundaries & Constraints

**Always:**
- El reloj virtual sigue determinista: los checkpoints, las trazas de reloj y los hashes de Escenas no cambian en `native-headless`, `native-asan` ni `native`.
- Siguen pasando `sdl_frame0` (`overflows=0`), `sdl_pacing` (`overflows=0 underruns=0`, relación 0,95–1,10) y el resto de la suite `native`.
- El criterio de `native_wall_10min` no se toca: ≥ 600 s, error < 1 VBlank y 0 resincronizaciones.
- Antes de corregir, la evidencia muestra la causa: qué hilo y qué función consumen la pared de más en el VBlank de cada resincronización.

**Never:**
- Subir el umbral de resincronización de 4 VBlanks, cambiar la tasa de VBlank o los costos de `clock_costs.h`, o saltear el test.
- Descartar muestras o agrandar el anillo de audio para esconder el atraso.

**Detener y reportar:** si la causa resulta ser la máquina (por ejemplo, el scheduler o el contenedor limitado a 2,5 CPUs) y no el port, se reporta con evidencia antes de cambiar el entorno del test.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Diez minutos | `ctest --preset native -R ^native_wall_10min$`, aislado, en este servidor | 36006 VBlanks, error < 1 VBlank, 0 resincronizaciones | falla con los valores |
| Repetición | dos corridas consecutivas | las dos cumplen lo anterior | N/A |
| Escena / headless | `port_capture_active()` o headless | sin cambios en las trazas | N/A |

</frozen-after-approval>

## Code Map

- `port/host/sdl/video.c:37-61` -- `pace()`: deadline con `CLOCK_VBLANK_NUM/DEN`. Imprime `dw1: pacing resynchronization vblank=… late_ns=…` cuando el atraso pasa de 4 VBlanks. Se llama desde `plat_native_present` (`VSync` bloqueante) y desde `plat_native_wall_begin/finish`.
- `port/host/sdl/audio.c:10-18` -- `produce`, con contrapresión de hasta 100 ms. Se llama desde `spu_sync` (`port/backend/spu.c:69-90`). `consume` es el callback del dispositivo. `audio_rate.h:6-9`: capacidad 8192, objetivo 2208, banda muerta 384.
- `tools/port/test_native_wall.py`, `CMakeLists.txt:429-432` -- el test (`RUN_SERIAL`, `SDL_VIDEODRIVER=offscreen`, `SDL_AUDIODRIVER=dummy`, TIMEOUT 900). `dw1 --vblanks N` (`port/host/native_cli.c`) permite corridas más cortas que lleguen a la primera resincronización (VBlank ~7700).
- `docs/implementation-artifacts/evidence/story-e3/wall-10min-local-2026-10-26.log` y `wall-10min-remote-2026-10-26.log` -- las fallas que hay que reproducir.
- `docs/implementation-artifacts/evidence/story-e3/device-wait-cpu-stop.log` -- `DecDCTvlcBuild` cobra 6,7 M ciclos al arrancar un video (VBlanks 356–368). Sirve como pista para ver si las resincronizaciones coinciden con los arranques de video del ciclo del título.
- `port/psyq/game_clock.c`, sondas `DW1_DEVICE_PROBE` y `DW1_VSYNC_PROBE` -- para instrumentar sin cambiar el comportamiento.
- `tools/port/docker_run.sh` -- en este servidor hace falta `DOCKER_CONTEXT=default`, porque el contexto activo es otro host.

## Tasks & Acceptance

**Execution:**
- [x] Reproducir la primera resincronización con `dw1 --vblanks 9000` en `native` (`offscreen`/`dummy`) y medir, en el VBlank de cada resincronización, qué hilo y qué función ocupan la pared: espera de `produce`, trabajo nativo del juego, render o lectura del Pack. Guardar en `docs/implementation-artifacts/evidence/story-e3/` -- evidencia de la causa.
- [x] Corregir en el archivo donde está la causa (`port/host/sdl/*.c`, `port/host/audio_rate.*` o el que muestre la evidencia), dentro de los límites de la spec -- cierra la falla.
- [x] `tools/port/test_sdl_pacing.py` o `port/host/tests/` -- un test corto que falle con la causa encontrada y pase con el arreglo, sin esperar 10 minutos.
- [x] `docs/port/clock-calibration.md` -- registrar la causa, el arreglo y las dos corridas de 10 minutos.
- [x] `docs/implementation-artifacts/epic-3-retro-2026-10-26.md` -- agregar el hallazgo F14 (`native_wall_10min` en rojo en `main` desde la 3.10) con su disposición.

**Acceptance Criteria:**
- Given este servidor, when `native_wall_10min` corre aislado dos veces, then las dos dan 0 resincronizaciones y error < 1 VBlank.
- Given las suites `native-headless`, `native-asan` y `native` con disco, when corren, then pasan sin omitidos y con las mismas trazas de reloj que antes.

## Implementation Notes

Causa y arreglo en [wall-pacing-cause.md](evidence/story-e3/wall-pacing-cause.md): en las
transiciones del título, el hilo del juego gastaba unos 19 ms de CPU por VBlank. De eso, el
19,8 % se iba en `next_due`, que recorría los 32 slots de la cola en cada consulta de VSync.
Ahora `next_due` cachea el mínimo y lo invalida cuando la cola cambia. Codex agotó sus 120
minutos mientras corría las suites locales (2,5 CPUs); se volvieron a correr en el PC del
dueño: `native-headless` 83/83, `native-asan` 81/81 y `native` 94/94, con `native_wall_10min`
en 600,22 s (`evidence/story-e3/wall-suite-*.log`). Las dos corridas aisladas de 10 minutos en
este servidor dan 0 resincronizaciones (`wall-10min-fixed-1.log`, `wall-10min-fixed-2.log`).

## Spec Change Log

## Review Triage Log

Review `codex-merged` (blind-hunter, verification-gap, edge-case-hunter) sobre `build/review/e3-wall.diff`.

| # | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|
| 1 | La prueba corta de `test_sdl_pacing.py` no limita el error de pared (blind-hunter, edge-case-hunter) | low | Solo exigía 0 resincronizaciones: un atraso de hasta 4 VBlanks pasaba. El criterio de la spec es error < 1 VBlank. | patch: misma fórmula que `test_native_wall.py` |
| 2 | `queue_mutations` cancela el evento anterior al mínimo cacheado antes de que venza (blind-hunter, verification-gap) | low | Cierto en `test_clock.c:263-269`: no se comprobaba que una inserción anterior reemplazara el mínimo cacheado. | patch: un evento anterior al mínimo cacheado se despacha a su vencimiento |
| 3 | `gpu_reset(1)` deja el display apagado y la carga sintética omite la conversión del framebuffer | low | La conversión del display fue 7 de 373 muestras (`wall-pacing-cause.md`); la prueba ya reproduce la falla sin ella (5 resincronizaciones antes del arreglo). | rechazado |
| 4 | La textura y el destino de dibujo comparten VRAM en `(0,0)` | low | El quad sigue texturando 320×240 en cada iteración: la carga no desaparece, solo cambian los texels. | rechazado |
| 5 | Quitar las llamadas `gpu_gp0` no hace fallar la prueba | low | Requiere borrar la carga a propósito; la evidencia antes/después (`wall-short-before.log`, `wall-short-after.log`) fija que reproduce la causa. | rechazado |
| 6 | `DW1_TEST_GPU_LOAD` heredado del entorno contamina las corridas anteriores | low | Variable interna de la prueba; nada la define fuera de `test_sdl_pacing.py`. | rechazado |

Tras los patches: `ctest --preset native -R "clock|^sdl_pacing$"` 2/2 en este servidor.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native -R "^native_wall_10min$" --output-on-failure'` (dos veces) -- expected: 0 resincronizaciones.
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native -R "sdl_|native_" --output-on-failure'` -- expected: 0 fallos.
