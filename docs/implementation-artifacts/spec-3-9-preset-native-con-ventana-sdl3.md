---
title: 'Historia 3.9: Preset native con ventana SDL3'
type: 'feature'
created: '2026-10-22'
status: 'in-progress'
baseline_commit: '2fe8fab0a0100a6b19fde9688f07563b69dc56d8'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/setup.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El Build nativo solo existe sin ventana (`native-headless`). El desarrollador no puede jugar ni depurar a mano con video, audio, teclado y gamepad usando el mismo código que la web (FR-17, AD-17). Además, el Host todavía no tiene un dueño de la deriva entre el reloj de audio y el VBlank (AD-10).

**Approach:** Agregar el preset `native` (i686, `-m32`) con un Host SDL3 3.4.16 i686 compilado en la imagen `dw-port`. El hilo principal maneja ventana, eventos y presentación; el Juego sigue en su hilo propio, como en web. El display software se presenta a 1× escalado con nearest, y el ritmo de pared es de 59,94 Hz. El input se muestrea una vez por VBlank y se aplica con `port_pad_latch`. Las muestras del SPU pasan por un ring buffer con remuestreo, cuyo ratio se ajusta según el nivel del buffer, con banda muerta (AD-10). Ese módulo es C puro y la web lo reutiliza en 4.7.

## Boundaries & Constraints

**Always:**
- Mismas flags del Juego que los otros presets (AD-17). `native-headless` sigue sin SDL (AD-3), y sus tests y resultados no cambian.
- El remuestreo y el control del buffer viven en C puro, sin SDL, testeables en L1 y compilables a WASM.
- El Host es el único que corrige la deriva: el Juego y el reloj virtual nunca se ajustan por audio.
- Con `--inputs`, la Escena manda en el pad y el input en vivo se ignora, para que el resultado sea determinista.
- La CLI de `native` acepta las mismas opciones que la de `native-headless`, con un único parser compartido. `--headless` en `native` corre sin ventana ni audio.
- La captura (`--capture*`) es la misma ruta de `crt.c` que usa headless: con la misma Escena, el SHA-256 es igual.
- SDL3 i686 se fija a 3.4.16, igual que el nativo x64 y el web. La imagen se construye en la misma etapa de la imagen `dw-port`.

**Never:**
- Remapeo de controles, opciones persistentes ni volumen configurable (4.6). Escala de resolución o renderer GPU (Épica 5). Calibrar costos o medir 10 minutos de pared (3.10).
- Modificar `src/`, `port/backend/spu.c` (salvo usar `spu_set_output`) o el reloj virtual.
- Usar `SDL_SetAudioStreamFrequencyRatio` como controlador: el control tiene que ser el mismo módulo que usará la web.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Arranque normal | `dw1 --pack us.pack` | ventana 640×480 (2×), título con audio a 59,94 Hz | N/A |
| CI sin pantalla | `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` + Escena `title` | captura con SHA igual a `native-headless` | N/A |
| Sin dispositivo de audio | `SDL_OpenAudioDeviceStream` falla | sigue sin audio, aviso en stderr | sin abortar |
| Sin video | `SDL_Init(VIDEO)` falla | sale con 1 y mensaje | exit 1 |
| Dispositivo rápido/lento | consumo al 101 % / 99 % durante 10 min simulados | buffer dentro de la banda, 0 underruns tras el llenado inicial | L1 |
| Underrun | el productor se detiene | silencio sin clicks (rampa o último valor); el contador sube | N/A |
| Gamepad conectado en caliente | `SDL_EVENT_GAMEPAD_ADDED/REMOVED` | se abre o cierra sin reiniciar | N/A |
| Cerrar ventana | `SDL_EVENT_QUIT` | sale con 0; las tarjetas ya están commiteadas por comando (3.8) | N/A |

**Decisión del dueño (2026-10-22):**
1. **Ventana por Docker con WSLg (opción A):** `tools/port/docker_run.sh --gui` monta `/tmp/.X11-unix`, `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` y `PULSE_SERVER` de WSLg (`/mnt/wslg`), así que se juega desde el contenedor en el PC Windows del dueño sin instalar nada en el host. SDL3 i686 se compila con X11, Wayland y PulseAudio, y la imagen `dw-port` suma las dev libs `:i386` necesarias.

</frozen-after-approval>

## Code Map

- `CMakePresets.json` -- agregar configure/build/test `native` (hereda de `native-headless`, `binaryDir` `build/port/native`, `DW1_HOST=sdl`). El test preset `native` filtra los tests propios de SDL/audio, sin repetir toda la suite L1.
- `CMakeLists.txt:364-389` -- `dw1` nativo. Con `DW1_HOST=sdl`, enlazar `port/host/sdl/*.c` + SDL3 i686 (`find_package(SDL3 CONFIG)` con `CMAKE_PREFIX_PATH=$SDL3_NATIVE32_PREFIX`) en lugar de `headless/main.c`. `set_source_files_properties(... DW1_MANIFEST_SHA256)` también para el main nuevo.
- `port/host/headless/main.c` -- toda la CLI (pack, tablas, tarjetas, captura). Extraerla a un módulo compartido (p. ej. `port/host/native_cli.{c,h}`) que usen headless y SDL. No duplicar el parser.
- `port/psyq/crt.c:122-130,193-237` -- `port_frame_done` llama `plat_present(frames)` en el hilo del Juego. `port_run_game` hace `pthread_join` en nativo. El host SDL necesita el hilo principal libre para eventos y render: agregar un hook de plataforma (p. ej. `plat_main_loop()` débil) que corra en lugar del join.
- `port/host/plat.h` -- hoy solo `plat_present`. Agregar las declaraciones del host nativo con ventana sin romper web ni headless (headless conserva el inline vacío).
- `port/host/web/present_gl.c:43-60` -- patrón de presentación: `gpu_read_display(display,&w,&h)` (XRGB) y luego la textura. En SDL: el hilo del Juego copia el display a un doble buffer con mutex y espera la fecha límite de pared (acumulador 1001/60000 s; si se atrasa más de unos frames, resincroniza). El hilo principal sube una textura `SDL_PIXELFORMAT_XRGB8888` y presenta con `SDL_SetRenderLogicalPresentation` en letterbox entero o 4:3 y escalado nearest.
- `port/psyq/libetc.c:46` `port_pad_latch(wire)` y `port/psyq/scene.c:271-291` -- el latch de Escena usa el observador de VBlank. El input en vivo se aplica en el mismo punto: un observador del Host lee un `_Atomic uint16_t` que escribe el hilo principal, salvo que haya `--inputs`.
- `port/backend/spu.h:30-33` `spu_set_output(fn,arg)` -- productor: 44.100 Hz estéreo s16, se llama desde el hilo del Juego en `spu_sync`. El consumidor es el callback de `SDL_OpenAudioDeviceStream` a la frecuencia nativa del dispositivo.
- `port/host/audio_rate.{c,h}` (nuevo) -- ring SPSC lock-free + resampler lineal + controlador proporcional con banda muerta (objetivo ~3 VBlanks de latencia, ajuste máximo ±0,5 %). API: `push(pcm,frames)` y `pull(out,frames,device_rate)`, más contadores (`underruns`, nivel). Sin SDL.
- `tools/port/Dockerfile:64-81` -- SDL3 x64 y web. Agregar el build i686 (`-DCMAKE_C_FLAGS=-m32`, prefix `/opt/sdl3/native32`, `ENV SDL3_NATIVE32_PREFIX`) y las dev libs `:i386` de X11, Wayland y PulseAudio (decisión 1).
- `tools/port/scenes/run.py:598,531,668` -- `--build` acepta `native`. Para ese build no pasa `--headless`, y sí `SDL_VIDEODRIVER=offscreen`/`SDL_AUDIODRIVER=dummy`.
- `.gitlab-ci.yml:47-76` -- jobs `native` (clase A) y `native-disc` (clase B): agregar build y test del preset `native`.
- `docs/port/setup.md`, `docs/port/controls.md` (nuevo) -- cómo correr la ventana y el mapa de controles.

## Tasks & Acceptance

**Execution:**
- [ ] `tools/port/Dockerfile` -- SDL3 3.4.16 i686 + dev libs `:i386` (X11, Wayland, PulseAudio; decisión 1); `tools/port/env_check` verifica que `SDL3Config` i686 linkee con `-m32`.
- [ ] `port/host/audio_rate.{c,h}` + `port/host/tests/test_audio_rate.c` -- módulo C puro y L1: dispositivo simulado al 101 % y al 99 % (y 100 %), 10 minutos simulados a 48 kHz, productor por VBlank exacto (44.100×1001/60000). Assert: nivel dentro de la banda después del primer segundo, 0 underruns, ratio acotado. También corre en `native-headless`, `native-asan` y `web` (Node).
- [ ] `port/host/native_cli.{c,h}` + `port/host/headless/main.c` -- parser y arranque compartidos; headless queda igual en comportamiento.
- [ ] `port/host/sdl/{main.c,video.c,input.c,audio.c}` (o menos archivos si alcanza) -- ventana, presentación con ritmo de pared, teclado + `SDL_Gamepad` según `docs/port/controls.md`, audio por `audio_rate`.
- [ ] `port/psyq/crt.c`, `port/host/plat.h` -- hook del loop principal y observador de input del Host.
- [ ] `CMakeLists.txt`, `CMakePresets.json` -- preset y target `native`. Tests: `sdl_frame0` (offscreen/dummy, `--frames 120`, sale con 0), `audio_rate`, `native_sdl_title_capture` (clase B, skip 77 sin disco: `run.py title --build native` con hash igual al de `title.toml`).
- [ ] `tools/port/scenes/run.py` + `test_runner.py` -- build `native`.
- [ ] `.gitlab-ci.yml` -- build/test `native` en clase A y B.
- [ ] `tools/port/docker_run.sh` -- opción `--gui` (WSLg: X11, Wayland, PulseAudio), ignorada con aviso si los sockets no existen.
- [ ] `docs/port/setup.md`, `docs/port/controls.md` -- uso con `--gui`, controles.

**Acceptance Criteria:**
- Given la imagen `dw-port` reconstruida, when `cmake --preset native && cmake --build --preset native`, then `build/port/native/dw1` es ELF i386 enlazado a SDL3 3.4.16 i686.
- Given `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` y el disco, when `run.py title --build native`, then el SHA-256 de la captura es igual al de `native-headless` (`title.toml`).
- Given `test_audio_rate`, when simula 10 minutos al 101 % y al 99 %, then 0 underruns y nivel dentro de la banda.
- Given los cambios, when corren `ctest --preset native-headless`, `native-asan` y `web`, then siguen verdes, y `make compare` sigue idéntico.

## Implementation Notes

- Imagen hecha por el orquestador (Codex no tiene Docker): `tools/port/Dockerfile` instala SDL3 3.4.16 i686 en `/opt/sdl3/native32` (`ENV SDL3_NATIVE32_PREFIX`), con X11, Wayland, PulseAudio, ALSA, offscreen y dummy, y las dev libs `:i386`. `dw-port:latest` ya está reconstruida. Del primer task falta solo el chequeo en `tools/port/env_check`.

## Spec Change Log

## Review Triage Log

## Design Notes

- **Controles por defecto** (como DuckStation, para no inventar): D-pad = flechas y W/A/S/D; Cross K, Circle L, Square J, Triangle I; Start Enter; Select Backspace; L1 Q, R1 E, L2 1, R2 3. En el gamepad, el mapeo posicional de `SDL_Gamepad` (South = Cross, East = Circle, West = Square, North = Triangle). Remapeo en 4.6.
- **Remuestreo lineal:** alcanza para depurar y es barato en WASM. Si 4.7 mide aliasing audible, se cambia el kernel sin tocar el controlador.
- **Ritmo:** el video se fija al reloj de pared en el hilo del Juego y el audio absorbe la diferencia con el ratio. Así hay un solo dueño de la deriva (AD-10). 3.10 mide los 10 minutos.

## Verification

**Commands:**
- `tools/port/docker_build.sh` y luego `tools/port/docker_run.sh sh -c 'cmake --preset native && cmake --build --preset native && ctest --preset native'` -- expected: verde.
- `tools/port/remote_run.sh sh -c 'for p in native-headless native-asan web; do cmake --build --preset $p && ctest --preset $p || exit 1; done'` -- expected: verde.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh --preset native -R native_sdl_title_capture` -- expected: pasa sin omisiones.
- `make compare` en `dw-build` -- expected: `Compiled binaries match with target binaries`.
