---
title: 'Corrección E3: gate de imagen en Escenas de estado y espera de audio en VSync'
type: 'bugfix'
created: '2026-10-26'
status: 'in-progress'
route: 'dispatch'
baseline_commit: '73ffac194c5fdc795ea175681fc6aaf9cd388e97'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/implementation-artifacts/epic-3-retro-2026-10-26.md'
  - '{project-root}/docs/implementation-artifacts/spec-e3-ritmo-y-texto-sjis-nativo.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** La retro de la Épica 3 encontró dos defectos que hay que cerrar antes de la Épica 4:
- **F2 (acción A1).** Con `--state`, `run.py` compara la imagen solo a modo informativo. Una regresión visual en `map_load` (y en las otras Escenas de estado) deja el test en verde.
- **F3 (acción A3).** `produce` en `port/host/sdl/audio.c` duerme hasta 100 ms por bloque dentro de `spu_sync`, también en escrituras de registros SPU. AD-8 permite pausar el hilo del juego por la pared solo en `VSync`.

**Approach:**
- **A1.** Con `--state`, la imagen pasa a ser un gate: hash o RGB555 exactos con las excepciones declaradas en el `.toml`. Una Escena cuya captura todavía no coincida lo declara explícitamente en su `.toml`, con motivo; nunca se cae en silencio a informativo.
- **A3.** El productor de audio deja de esperar. La pared se alinea en `VSync` (bloqueante y no bloqueante, `port_vsync_return`) y en las esperas bloqueantes de dispositivo: cuando el juego espera al CD, la GPU, el MDEC o la tarjeta y el reloj salta al próximo evento. En esas esperas no corre código del juego. El audio no debe desbordar.

Decisiones del dueño (2026-10-26, retro de la Épica 3):
- A1 se cierra dentro de la Épica 3.
- A3: la implementación encontró que `CdInit` avanza del VBlank 2 al 12 sin pasar por ningún `VSync` y desborda el audio (`evidence/story-e3/image-gate-and-vsync-status.md`). El dueño eligió la opción 1: alinear la pared también en las esperas bloqueantes de dispositivo y enmendar AD-8 ("solo en `VSync` y en esperas bloqueantes de dispositivo"). Siguen prohibidas las pausas en escrituras de registros y en `spu_sync`.
- A3, segunda parada: con la opción 1, `dw1 --frames 1200` todavía da `overflows=630 underruns=3`. `DecDCTvlcBuild`, llamado desde `MOV_playMovie`, cobra 6,7 M ciclos de CPU modelada (unos 200 ms) entre los VBlanks 356 y 368, sin pasar por ningún `VSync` ni por ninguna espera de dispositivo (`evidence/story-e3/device-wait-followup.md`). El dueño eligió la opción 2 (2026-10-26): la espera del productor de audio se conserva sin cambios y AD-8 la documenta como excepción aceptada (contrapresión de audio). Queda registrado en `deferred-work.md` pausar en los avances de costo de CPU. A3 se cierra como desvío aceptado: no hay cambio de código en audio ni en el reloj.

## Boundaries & Constraints

**Always:**
- El reloj virtual sigue determinista: las trazas de reloj y los VBlanks por checkpoint no cambian en `native-headless`, `native-asan` ni `native`, y en headless y en corridas de Escena nunca se pausa.
- Los gates existentes siguen pasando: `sdl_frame0` (`overflows=0`), `sdl_pacing` (`overflows=0 underruns=0`, relación 0,95–1,10) y `native_wall_10min` (error < 1 VBlank, 0 resincronizaciones).
- Para una Escena que no coincide hace falta una de dos cosas: excepciones de región ya justificadas con motivo, o la marca explícita de "imagen informativa" con motivo y medición en el `.toml`. No se inventan regiones para hacer pasar una captura.
- Los cambios en `src/`, si hicieran falta, van detrás de `DW1_PORT` y se anotan en `docs/port/src-changes.md`.

**Never:**
- Descartar muestras, cambiar el remuestreo o las constantes de `audio_rate` para tapar el desborde, o agrandar el anillo hasta tolerar segundos de latencia.
- Pausar en `spu_sync`, en escrituras de registros o en cualquier punto que no sea `VSync` o una espera bloqueante de dispositivo.
- Cambiar los hashes de referencia sin volver a capturar ni explicar el cambio.

**Detener y reportar:** si alguna carga real avanza el reloj virtual sin pasar por ningún `VSync` ni por ninguna espera bloqueante de dispositivo, hace falta una decisión del dueño.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Imagen igual | `run.py map_load --state` | "reference match", salida 0 | N/A |
| Imagen distinta | captura con píxeles fuera de excepciones | salida ≠ 0, cantidad de píxeles y caja envolvente | N/A |
| Imagen informativa declarada | `.toml` con la marca y motivo | se imprime el diff y la salida no falla | `.toml` sin motivo: error de configuración |
| Carga sin presentar | `dw1 --frames 1200`, `SDL_AUDIODRIVER=dummy` | `overflows=0 underruns=0`; pared ≈ virtual | N/A |
| Corrida de Escena / headless | `port_capture_active()` | ninguna pausa de pared | N/A |

</frozen-after-approval>

## Code Map

- `tools/port/scenes/run.py:733-740` -- bloque `if args.state:` que envuelve `compare_capture` en `try/except` e imprime "informative frame comparison". `compare_capture` (`:72-110`) ya implementa hash, RGB555, excepciones y diff. Se reutiliza; no se duplica.
- `CMakeLists.txt:614-616,646-649,656-660` -- tests `--state`: `scene_map_load_compare`, `scene_newgame` y `scene_card_*_compare` (`card_newgame`, `card_continue`). `.gitlab-ci.yml:82` los corre con `scene_`.
- Estado conocido de las imágenes: `map_load` coincide con 0 píxeles (`evidence/story-3-11.log`). `newgame` difería en 403 píxeles, caja 87..217 46..121, por el modelo 3D (`evidence/story-3-3.log:16`). Medir de nuevo en el HEAD antes de decidir cada Escena; las tarjetas no tienen medición registrada.
- `tools/port/scenes/*.toml`, `tools/port/scenes/test_runner.py` -- formato de Escena y tests del runner. El campo nuevo de imagen informativa se valida y se prueba acá.
- `port/host/sdl/audio.c:10-18` -- `produce` con el bucle `SDL_Delay(1)` de hasta 100 ms. Se llama desde `spu_sync` (`port/backend/spu.c:69-90`) a través de `plat_audio_push`.
- `port/host/sdl/video.c:37-61` -- `pace()`: deadline exacto con `CLOCK_VBLANK_NUM/DEN` y resincronización con > 4 VBlanks de atraso. Hoy solo se llama desde `plat_native_present` (`VSync` bloqueante, `port/psyq/crt.c:155-158`) y desde `plat_native_wall_begin` (frame 0).
- `port/psyq/crt.c:143-152` -- `port_vsync_return`, el `VSync` no bloqueante, incluido el `set_alarm` de PsyQ (AD-15).
- `port/psyq/clock.c` -- donde una espera bloqueante salta al próximo evento (AD-8). Es el punto para pacear durante `CdInit` y las cargas. Evidencia del salto: `evidence/story-e3/vsync-audio-stop.log`.
- `port/host/plat.h:9-18` -- hooks débiles `plat_native_*`; hace falta uno para el `VSync` no bloqueante, o reutilizar uno existente.
- `port/host/audio_rate.h:6-9` -- `AUDIO_RATE_CAPACITY 8192` (186 ms), `TARGET`, `DEADBAND` y contadores `overflows`/`underruns`. No se cambian.
- `spec-e3-ritmo-y-texto-sjis-nativo.md:92,112` -- origen de la espera: sin ella, `sdl_frame0` daba `overflows=142443` y `dw1 --frames 1200` daba 223.055. Es la regresión a evitar.
- `tools/port/test_native_wall.py`, `tools/port/test_sdl_pacing.py`, `CMakeLists.txt:429-435` -- gates de pared y audio.

## Tasks & Acceptance

**Execution:**
- [ ] `tools/port/scenes/run.py` -- con `--state`, que una diferencia de imagen haga fallar la corrida salvo que el `.toml` declare imagen informativa con motivo. Validar ese campo al cargar la Escena -- cierra F2.
- [ ] `tools/port/scenes/{newgame,card_newgame,card_continue}.toml` -- medir la captura en el HEAD. Si coincide, no se agrega nada. Si no, agregar la marca con el motivo y los píxeles y la caja medidos -- cada Escena queda gate o excepción explícita.
- [ ] `tools/port/scenes/test_runner.py` -- probar con capturas sintéticas: igual (sale 0), distinta (falla), informativa declarada (sale 0) e informativa sin motivo (error).
- [ ] `port/host/sdl/audio.c` -- quitar la espera de `produce`.
- [ ] `port/host/sdl/video.c`, `port/host/plat.h`, `port/psyq/crt.c`, `port/psyq/clock.c` -- pacear también en el `VSync` no bloqueante y en las esperas bloqueantes de dispositivo del reloj (el punto único donde una espera salta al próximo evento), con el mismo acumulador de deadline, y respetar headless y `port_capture_active()` -- cierra F3.
- [ ] `docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md` (AD-8, "Reloj de pared") -- enmendar: la pared pausa solo en `VSync` y en esperas bloqueantes de dispositivo, nunca en escrituras de registros -- decisión del dueño.
- [ ] `port/host/tests/` o `tools/port/test_sdl_pacing.py` -- un test que falle si el productor de audio vuelve a dormir fuera de `VSync`, y verificación de `dw1 --frames 1200` con `overflows=0 underruns=0`.
- [ ] `docs/implementation-artifacts/epic-3-retro-2026-10-26.md` (solo las filas A1 y A3 de Acciones), `docs/port/src-changes.md` si corresponde -- registrar el cierre con la evidencia.
- [ ] `docs/implementation-artifacts/evidence/story-e3/` -- log de la corrida de las Escenas `--state`, `sdl_frame0`, `sdl_pacing`, `native_wall_10min` y `dw1 --frames 1200`.

**Acceptance Criteria:**
- Given el disco USA, when corren `scene_map_load_compare`, `scene_newgame` y `scene_card_*_compare`, then cada una o exige la imagen exacta y pasa, o declara la excepción con motivo y medición.
- Given el preset `native` con `SDL_AUDIODRIVER=dummy`, when corren `sdl_frame0`, `sdl_pacing`, `native_wall_10min` y `dw1 --frames 1200`, then pasan con `overflows=0 underruns=0` y 0 resincronizaciones, y `rg SDL_Delay port/host/sdl/audio.c` no encuentra nada.
- Given `native-headless` y `native-asan`, when corren las suites con disco, then las trazas de reloj y los VBlanks por checkpoint son idénticos a los de `main` antes del cambio.

## Implementation Notes

2026-10-26: A1 implementada y probada sintéticamente; aceptación con disco pendiente.
Se ensayó A3 con la decisión actualizada y se encontró la nueva condición de parada:
trabajo CPU al arrancar la película entre VBlanks 356 y 368 sin un punto permitido
para pausar, con desborde antes del siguiente pacing. `--frames 1200` termina con
`overflows=630 underruns=3`. El ensayo se retiró; se mantienen A1 y la enmienda AD-8.
Evidencia, comandos y verificaciones pendientes: [device-wait-followup.md](evidence/story-e3/device-wait-followup.md).

## Spec Change Log

## Review Triage Log

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh --preset native-headless -R "scene_|reference_"'` -- expected: 0 fallos, 0 omitidos.
- `tools/port/remote_run.sh sh -c 'DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh --preset native'` -- expected: 0 fallos, incluidos `sdl_frame0`, `sdl_pacing` y `native_wall_10min`.
- `tools/port/docker_run.sh sh -c 'SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy build/port/native/dw1 --pack game_rom/us.pack --frames 1200'` -- expected: `overflows=0 underruns=0`.
