---
title: 'Historia 4.7: Audio en el navegador'
type: 'feature'
created: '2026-10-31'
status: 'done'
baseline_commit: 'bd572cd9bda80f1fb6ef2a0208292f11834f7dae'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-6-remapeo-y-opciones-persistentes.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** En web el SPU genera PCM que nadie consume: el `AudioContext` creado por «Jugar» no tiene salida y el jugador no oye el Juego ni puede ajustar el volumen (FR-7).

**Approach:** El hilo del juego escribe el PCM en el anillo `AudioRate` en memoria compartida y `port/web/audio_worklet.js` lo consume y remuestrea a la frecuencia del dispositivo, con la lógica de `audio_rate_pull` (deriva dueña del Host, AD-10). El volumen general se aplica en el Host con un `GainNode`, se ajusta en la Shell y persiste en `PortSettings`.

## Boundaries & Constraints

**Always:** el worklet es el único consumidor y el hilo del juego el único productor; `port/web/audio_worklet.js` replica `audio_rate_pull` con resultado idéntico, muestra a muestra, al C con las mismas constantes (si la calibración exige cambiarlas en web, se documenta el valor y la medición); el tiempo virtual nunca depende del audio; la pausa manual, la pestaña oculta y el fin de la corrida no cuentan underruns; si `AudioContext` o `audioWorklet.addModule` fallan, el juego corre sin audio, con un aviso visible en la Shell; sin `AudioContext` (arranques por query de los tests existentes) no se escribe el anillo y esos tests no cambian; la migración `v2 → v3` conserva campos desconocidos y sigue las reglas de protección de registros de 4.6; «Restaurar valores por defecto» no toca `volume`, porque restaura controles.

**Never:** ejecutar wasm dentro del AudioWorklet; remuestrear o aplicar volumen en el Backend o la Capa PsyQ; cambios en `src/`; opciones de video (Épica 5); Memory cards (4.8); service worker (4.9); medición final en hardware de referencia (Épica 5, historia 5.6); portar el motor de Escenas `scene.c` a wasm.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Volumen en vivo | mover el control a 50 % durante la partida | la salida medida cae a la mitad; el juego no se detiene | — |
| Volumen persistente | 30 %, recargar, «Jugar» | control y `GainNode` en 0,30 | — |
| Volumen 0 | 0 % | silencio; el anillo se sigue consumiendo, sin underruns | — |
| Migración | registro v2 con campos extra | v3 con `volume: 1`, controles y extras intactos | — |
| Valor inválido | `volume` fuera de 0–1 o no numérico | registro protegido, valores por defecto y aviso (regla de 4.6) | no escribe |
| Restaurar | volumen 30 %, «Restaurar valores por defecto» | controles restaurados, `volume` sigue en 0,30 | — |
| Pausa | pausa 30 s, reanudar | `AudioContext` suspendido y reanudado; underruns sin cambios; sigue sonando | — |
| Pestaña oculta | ocultar y volver | igual que Pausa | — |
| Worklet no carga | `addModule` rechaza | el juego corre sin audio, con aviso | aviso; sin escrituras al anillo |
| Sin gesto en el contexto | el contexto queda `suspended` | sin underruns contados; el anillo no se desborda sin límite | contrapresión acotada |

</frozen-after-approval>

## Code Map

- `port/host/audio_rate.{c,h}` -- anillo `AudioRate` (`pcm`, luego atómicos `read`, `write`, `underruns`, `overflows`), `audio_rate_push`, `audio_rate_pull`. Reusar push y layout; `phase`, `ratio`, `ramp`, `started` y `last` pasan a ser estado del worklet.
- `port/host/sdl/audio.c` -- modelo a seguir: `produce` con contrapresión acotada a 100 ms y `plat_game_init` con `spu_set_output`. `plat_game_init` es débil en `port/psyq/crt.c:208`.
- `port/host/web/pace.c` -- `control[PAUSE]` compartido; espera con `emscripten_futex_wait`. `web_pace` no pausa si `port_capture_active()`.
- `port/host/web/main.c` -- argumentos web; `port/host/web/input.c:plat_input_vblank` -- lugar del replay de inputs (sale antes si hay captura activa).
- `port/backend/spu.h` -- `spu_set_output`: PCM 44.100 Hz estéreo s16; NULL descarta. Los callbacks no pueden reentrar al SPU.
- `port/web/dev/index.html` -- `boot` (recibe `audioContext`), `pauseControls.apply`, `play.onclick`, `MIGRATIONS`, `SETTINGS_VERSION`, `defaultSettings`, `validSettings`, restauración de defaults (hoy pisa todo lo que devuelva `defaultSettings`), `#options`, aviso `#settings-warning`.
- `CMakeLists.txt:693-709` -- objetivo web: `configure_file` de JS, fuentes y `EXPORTED_FUNCTIONS`. `:745` `web_scene_title`, modelo del test clase B. `:164-170` -- `libsnd.c` lleva `-finstrument-functions` solo en nativo.
- `port/psyq/scene.c:11,576` -- el motor de Escenas no existe en web (usa `dlsym`); la rama web son stubs. `port/psyq/crt.c:87` -- el límite `vblank_limit` y su salida.
- `tools/port/scenes/title_audio.toml`, `tools/port/scenes/run.py:run_audio` -- inputs relativos a marcas, checkpoints y segmentos; `--audio-segments` termina la corrida. `run_audio` exige referencia de Redux: la captura nueva no.
- `port/web/tests/run.py`, `playwright.config.cjs`, `long.spec.cjs`, `title.spec.cjs`, `storage.cjs` -- arnés, PulseAudio con sink nulo, suite L4 de 10 minutos (a mano, requiere `DW1_DISC_DIR`), y `seed`, `errors`.
- `tools/port/testing/deploy.sh:51`, `tools/port/testing/test_deploy.py:60` -- lista de archivos desplegados: agregar `audio_worklet.js`.
- `.gitlab-ci.yml:141` -- regex class B web del job `testing`.
- `docs/implementation-artifacts/deferred-work.md` -- pendientes de esta historia: pausa de audio web (4.4); underruns nativos `--frames 1200` y contrapresión (revisar en épica 4).
- No cambiar: `port/backend/spu.c`, SDL nativo y sus constantes, `src/`.

## Tasks & Acceptance

**Execution:**
- [x] `port/host/web/audio.c`, `port/host/web/audio.h`, `CMakeLists.txt` -- crear el productor web: `AudioRate` estático, `web_audio_address`, `web_audio_enable` y `plat_game_init` que llama `spu_set_output` solo si se habilitó; contrapresión como nativo (hasta 100 ms por bloque sobre `TARGET + DEADBAND`, durmiendo con futex, sin spin); modo sin pérdida para replay (espera sin tope, nunca descarta); exportar funciones y copiar `audio_worklet.js`.
- [x] `port/web/audio_worklet.js` -- crear `AudioWorkletProcessor` con la lógica de `audio_rate_pull` en una función pura exportable a Node; recibe por `processorOptions` el `SharedArrayBuffer` y la dirección; lee el layout, salida en `Float32`; si la palabra de pausa vale 1, emite silencio sin leer ni contar; con `tap`, envía por `MessagePort` los frames crudos leídos del anillo, antes de interpolar.
- [x] `port/web/dev/index.html` -- crear nodo worklet + `GainNode` en `boot` antes de `callMain`; `Module.audioStats()` con `underruns`, `overflows`, nivel y frames escritos; `suspend`/`resume` en `apply()` y al terminar; `?audio=1` espera un clic para crear el contexto; `PortSettings` v3 con `volume` 0–1; control de volumen 0–100 % en Opciones que pasa a `#game` al arrancar y hace `blur` tras cada cambio para no capturar teclas del juego; persistir al soltar; aviso si falla; quitar «audio llega en próximas versiones».
- [x] `port/host/native_cli.c`, `port/psyq/scene.c`, `port/psyq/crt.h` -- agregar `--audio-pcm FILE` (todo el PCM de `spu_set_output`, crudo) y `--resolved-inputs FILE` (`<vblank absoluto> <máscara>` por cada cambio aplicado) en la corrida nativa.
- [x] `port/host/web/main.c`, `port/host/web/input.c`, `port/psyq/crt.c` -- agregar `--replay FILE` y `--vblanks N` en web: aplicar la máscara resuelta en el latch del VBlank absoluto y terminar con `web_finish(0)`; con replay no entra input en vivo.
- [x] `tools/port/scenes/pcm.py` -- crear: corre `native-headless` con `title_audio` (inputs, checkpoints y segmentos como `run_audio`, sin referencia de Redux ni tarjetas), deja `pcm`, inputs resueltos, VBlank final y SHA-256 en un directorio fuera del repo. Reusar la generación de archivos de `run_audio` extrayéndola.
- [x] `port/web/tests/audio.spec.cjs`, `port/web/tests/audio_rate_parity.cjs`, `port/host/tests/audio_rate_vectors.c`, `CMakeLists.txt` -- clase A: toda la matriz con `--audio-ramp` (rampa conocida en lugar del SPU, para orden y pérdida) en Chromium y Firefox; `web_audio`; paridad C/JS con vectores que incluyan deriva, underrun y arranque, en 44.100, 48.000 y 96.000 Hz, `web_audio_rate_parity`.
- [x] `port/web/tests/title_audio.spec.cjs`, `port/web/tests/run.py`, `CMakeLists.txt`, `.gitlab-ci.yml` -- clase B: `web_scene_title_audio` con fixture del PCM nativo; compara el `tap` contra `pcm` en toda la longitud nativa, con el PCM web igual o más largo; informa offset de la primera diferencia.
- [x] `port/web/tests/long.spec.cjs` -- arrancar con gesto y `?audio=1`; exigir `underruns == 0` y `overflows == 0` en los 10 minutos; guardar los contadores en el JSON.
- [x] `tools/port/testing/deploy.sh`, `tools/port/testing/test_deploy.py` -- desplegar `audio_worklet.js`.
- [x] `docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md`, `docs/port/controls.md`, `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-7/` -- enmendar AD-8 para extender la contrapresión a web; `PortSettings` v3, cómo correr, constantes, evidencia y pendientes.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_audio`, `web_audio_rate_parity` y las existentes pasan en Chromium y Firefox.
- Given el disco y `native-headless`, when corre `web_scene_title_audio` en Chromium y Firefox, then el PCM crudo del worklet coincide byte a byte con el nativo en toda su longitud.
- Given `run.py build/port/web long` con audio, when pasan 10 minutos de pared en Chromium y Firefox, then `underruns` y `overflows` son 0 y los criterios de ritmo de 4.4 siguen cumpliéndose; los umbrales fallan el test.
- Given el juego arrancado con «Jugar» en una sesión real, when suena el título, then `AudioContext` está `running`, el nivel del anillo está dentro de la banda y hay señal no nula en la salida.

## Implementation Notes

- Codex implementó en dos corridas cortadas por el límite de 90 min; sin informe final. Resultados verificados en disco por Claude.
- Calibración web: `AUDIO_RATE_TARGET=6144`, `AUDIO_RATE_DEADBAND=1536` (latencia 139,3 ms contra 50 ms nativos); nativo sin cambios (`#ifndef` en `audio_rate.h`).
- `dw1_backend` en web compila con `-O2`; sin eso Firefox se atrasaba en cada corrida.
- Handshake: el productor espera el primer callback real del worklet, porque `AudioContext.state` se adelanta a la entrega.
- Firefox L4: pasa con 20 CPU (2/2) y falla 1 de 2 con el tope local de 2,5 CPU. Ver `evidence/story-4-7/README.md`.
- Fuera del Code Map previsto: `port/psyq/libetc.c` (hook débil de inputs resueltos), `port/web/dev/serve.py`, `port/web/testing/Dockerfile`.

## Spec Change Log

## Review Triage Log

Revisión Codex (`codex-merged`) del diff contra `bd572cd`.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| `resume()` pendiente mantiene la pausa (blind, gap, edge) | high | patch | `index.html:279` nunca llega a `audioReady`, cuyo temporizador es el único límite; el juego queda retenido sin aviso. |
| `addModule` que no termina impide `callMain` | medium | patch | `setupAudio` espera sin plazo antes de arrancar. Misma corrección que la fila anterior. |
| Confirmación inicial vencida conserva `audioNode` | medium | patch | Tras el fallo, `apply()` repite `resume` y `audioReady`: 5 s retenido al arrancar y en cada reanudación. |
| Contrapresión sin consumidor sin prueba de progreso | medium | patch | Código real: `audio.c:44-55` espera 100 ms por bloque, y hay varios bloques por VBlank; sin consumidor el juego casi se detiene. La prueba solo mira overflow. |
| Pausa atendida dentro del productor deja deadline vencido | medium | patch | `audio.c:47` no pasa por `hold()`; el próximo `web_pace` cuenta una resincronización falsa. |
| `blur()` en cada `input` rompe las flechas del volumen | medium | patch | La segunda flecha va al juego. Lo pedía el spec; la Shell ya ignora teclas con un `input` enfocado, así que basta el `blur` al soltar el puntero. |
| `long.spec` acepta una corrida silenciosa | medium | patch | Sin productor, `underruns` y `overflows` dan 0; falta exigir que `written` avance. |
| `strcpy` en `audio_open` con rutas largas | low | rechazado | Requiere más de 4096 bytes en un argumento de una herramienta local de captura. |
| Replay espera sin tope si cae el consumidor | low | rechazado | Solo en tests: el timeout de Playwright lo corta. |
| Contadores `after_block_*` sin aserción | low | rechazado | El bloqueo es artificial y la recuperación ya se exige con `shown_after_block`. |
| Replays inválidos sin test | low | rechazado | `web_input_replay` los rechaza; el archivo lo genera `pcm.py`. |
| `pcm.py` comparte directorio entre corridas | low | rechazado | CI corre en serie y un candado agrega complejidad sin caso real. |
| `audio-pcm` y `resolved-inputs` al mismo archivo | low | rechazado | Error de uso de una herramienta local, que `pcm.py` nunca comete. |

## Design Notes

**Escena en web sin motor de Escenas.** Web no tiene `scene.c`. En vez de portarlo, la corrida nativa de `title_audio` resuelve las marcas y guarda los inputs por VBlank absoluto; web los reproduce. Ambos lados son deterministas, así que el PCM debe coincidir desde el primer frame. Web corre con ritmo de pared y el tiempo de la Escena es real: fijar timeouts según lo medido.

**Riesgos a verificar, no suponer.** (1) `libsnd.c` lleva instrumentación solo en nativo: si esos hooks cobran ciclos, el tiempo virtual diverge. (2) Firefox y `Pulse` entregan cuantos en ráfagas: `AUDIO_RATE_TARGET` (50 ms) puede quedar corto en web; es la perilla de calibración, sin tocar la nativa. (3) Los 2 underruns nativos con `--frames 1200` tienen causa desconocida: si reaparecen en web hay que encontrarla, no filtrarlos.

**Contrapresión.** Sin ella, `CdInit` y `DecDCTvlcBuild` adelantan el reloj virtual sin pasar por `VSync` y desbordan el anillo, como se midió en nativo. Web adopta la excepción de AD-8 aceptada por el dueño el 2026-10-26; el dueño aprobó su extensión a web el 2026-10-31. El pendiente de quitarla sigue en `deferred-work.md`.

## Verification

**Commands:**
- `cmake --build --preset web && ctest --preset web --output-on-failure` -- expected: verde; clase B saltada sin disco.
- `DW1_DISC_DIR=... ctest --preset web -R 'web_scene_title_audio'` -- expected: PCM idéntico en ambos navegadores.
- `DW1_DISC_DIR=... python3 port/web/tests/run.py build/port/web long` -- expected: `underruns=0`, `overflows=0` en ambos navegadores; JSON en `build/port/web/`.
- `ctest --preset native-headless --output-on-failure` y `make compare` -- expected: verde; sin cambios en el build PS1.
