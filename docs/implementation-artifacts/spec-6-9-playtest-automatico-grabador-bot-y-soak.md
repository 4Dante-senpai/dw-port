---
title: 'Historia 6.9: Playtest automático: grabador, bot y soak'
type: 'feature'
created: '2026-11-11'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
baseline_commit: 'e1ed792b2b6b607a2964bd110584f7d462086f47'
context:
  - '{project-root}/docs/implementation-artifacts/epic-6-context.md'
  - '{project-root}/docs/port/scenes.md'
  - '{project-root}/docs/port/testing-env.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El port solo se prueba en 10 Escenas cortas (título, partida nueva, carga de mapa, nombres, película, Memory card). Las caídas en combate, mundo y minijuegos aparecen recién cuando el dueño juega a mano, como pasó con el scratchpad en `BTL_drawTMD`.

**Approach:** Un playtest automático (nivel L5) sobre lo que ya existe: reproducción de inputs por VBlank, build headless sin reloj de pared y ASan/UBSan. Suma un digest de corrida igual en todos los builds, un grabador de inputs, un bot que genera inputs desde una semilla, un runner con detectores de fallo una pasada corta por merge en el CI y un soak largo que corre en el host de desarrollo (`docker-server`), no en el runner (decisión del dueño, 2026-11-11).

## Boundaries & Constraints

**Always:**
- El digest y el grabador no cambian el tiempo virtual, el orden de eventos ni la VRAM: una corrida con ellos activos da las mismas capturas y trazas que sin ellos.
- El digest solo usa datos independientes del build: área de display en RGB555, máscara del pad aplicada, contador de VBlanks, cantidad de llamadas a `rand` y su último valor, y overlay cargado. Nunca punteros nativos.
- Un solo formato de inputs (`<vblank> <máscara hex de 4 dígitos>` por cambio) y un solo origen de VBlank para nativo y web.
- La Memory card de entrada se copia antes de correr; la original no se modifica.
- Memory cards, grabaciones de sesiones reales y capturas quedan fuera del repo. El CI publica solo conteos, horas, hashes y semillas.
- Los tests con disco son clase B y salen con 77 si falta `DW1_DISC_DIR`.

**Never:**
- No cambiar `src/` ni `include/`.
- No comparar contra PCSX-Redux.
- No agregar lógica de juego al bot (lectura de memoria, objetivos, navegación): solo inputs aleatorios con pesos después de un prefijo fijo.
- No dividir `port/web/dev/index.html` en módulos (acción de retrospectiva aparte); el grabador web agrega lo mínimo.
- No correr el soak en el runner del CI ni agregarle un job: corre con `tools/port/remote_run.sh` en `docker-server`. Esta historia deja el script; la programación nocturna (cron) la instala el orquestador después.
- No depender de Memory cards externas: el dueño no tiene ninguna (2026-11-11). Los puntos de partida son el frame 0 con prefijo y las sesiones grabadas; el runner acepta cards opcionales.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Corrida limpia | Inputs del bot, `--vblanks N` | Salida 0; informe con digest final, overlays visitados, VBlanks y horas virtuales | N/A |
| Caída | Señal o salida distinta de 0 | Fallo `crash` con señal o código | Guarda semilla, inputs, card de entrada, último VBlank y cola de stderr |
| Assert | `PORT_ASSERT` en stderr | Fallo `assert` con archivo y línea | Igual que caída |
| Sanitizer | `ERROR: AddressSanitizer` o `runtime error:` | Fallo `sanitizer` con la primera línea | Igual que caída |
| Cuelgue | El digest no avanza en T segundos de pared | Fallo `hang`; el proceso se mata | Igual que caída |
| Imagen congelada | Mismo hash de display durante más de M VBlanks, o display apagado | Aviso en el informe; no falla | N/A |
| Misma semilla | `bot.py --seed S` dos veces | Archivos de inputs idénticos byte a byte | N/A |
| Inputs inválidos | VBlanks no crecientes o máscara fuera de rango | El host sale con 2 y el runner lo informa como error de uso, no como fallo del juego | N/A |
| Sin disco | Falta `DW1_DISC_DIR` | Los tests clase B salen con 77 | N/A |

</frozen-after-approval>

## Code Map

- `port/psyq/crt.h` (`PortCapture`), `port/host/native_cli.c`, `port/host/web/main.c` -- parseo de flags; agregar acá `--digest FILE`, `--digest-every N` y `--record-inputs FILE`. `vblank_limit` (`--vblanks`) ya corta la corrida.
- `port/psyq/scene.c` -- solo nativo (`#ifndef __EMSCRIPTEN__`): `--inputs`, `--inputs-from frame0|mark`, `latch()`, y `port_scene_resolved_input()` que ya escribe `<vblank> <máscara>` con `--resolved-inputs`. Reutilizar ese escritor como grabador nativo; no duplicarlo.
- `port/psyq/libetc.c:53-62` -- punto donde se resuelve la máscara del pad en cada latch; lugar para alimentar digest y grabador en todos los builds.
- `port/host/web/input.c` -- `web_input_replay()` y `plat_input_vblank()`: la reproducción web usa VBlank absoluto (`clock_vblanks()`); igualar el origen con el de nativo y verificarlo con test.
- `port/host/sdl/input.c`, `port/host/sdl/main.c` -- input en vivo del build `native`.
- `port/psyq/overlay.c:62` (`port_overlay_loaded`) -- gancho para registrar el overlay en el digest.
- `port/psyq/libc.c` (`rand`) y `port_rand_trace` -- contador de `rand`.
- `port/backend/gpu.h` -- lectura del área de display; `port/host/pack/sha256.h` -- SHA-256 a reutilizar.
- `port/psyq/port.h` -- `PORT_ASSERT` imprime `PORT_ASSERT archivo:línea` y aborta.
- `tools/port/scenes/run.py`, `tools/port/scenes/*.toml` -- runner de Escenas y sus `inputs`; `newgame.toml` y `card_continue.toml` sirven de prefijo del bot. No cambiar su comportamiento.
- `port/web/dev/index.html:118,235` y `port/web/tests/` -- `?replay=` carga `/__fixture/<nombre>.replay.txt`; `run.py` de web arranca PulseAudio y los navegadores.
- `CMakeLists.txt:648-672` -- patrón de tests clase B; `.gitlab-ci.yml` -- jobs `native-disc`, `native-asan-disc`, `web-disc`.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/playtest.c`, `port/psyq/playtest.h` -- digest encadenado por VBlank compilado en todos los builds; cada `--digest-every` VBlanks escribe una línea `<vblank> <overlay> <hash display> <digest>` y al terminar la línea final -- oráculo entre builds y señal de avance para el detector de cuelgues.
- [x] `port/psyq/crt.h`, `port/host/native_cli.c`, `port/host/web/main.c`, `port/psyq/libetc.c`, `port/psyq/overlay.c`, `port/psyq/libc.c` -- flags y ganchos del digest; `--record-inputs` sobre el escritor existente -- sin efecto cuando los flags no se pasan.
- [x] `port/host/web/input.c`, `port/web/dev/index.html` -- mismo origen de VBlank que nativo en la reproducción; grabación de cambios de máscara en el hilo del Juego y botón de la Shell que la descarga como archivo; exponer el digest final a la página -- sesiones del dueño reutilizables como tests.
- [x] `tools/port/playtest/bot.py` -- genera inputs desde `--seed`, `--vblanks` y `--prefix <escena.toml>`; pesos y duraciones como constantes con nombre -- sin estado ni lectura del juego.
- [x] `tools/port/playtest/run.py` -- corre uno o varios builds con inputs, card copiada y límite de VBlanks; aplica los detectores de la matriz; escribe `report.json` y un directorio de reproducción por fallo; con varios builds compara los digests finales; modo `--soak` con lista de semillas, prefijos, sesiones grabadas y cards opcionales, paralelo con `--jobs` -- un solo punto de entrada para CI y para el dueño.
- [x] `tools/port/playtest/test_playtest.py` -- L1 sin disco: cada fila de la matriz con ejecutables falsos, determinismo del bot y comparación de digests distintos.
- [x] `port/psyq/tests/test_playtest.c` -- L1: el digest no cambia el reloj, es igual en dos corridas y cambia si cambia un píxel, la máscara o el contador de `rand`.
- [x] `CMakeLists.txt` -- tests L1 y clase B `playtest_smoke` (bot desde el frame 0 con prefijo de partida nueva) en `native-headless` y `native-asan`.
- [x] `port/web/tests/playtest.spec.cjs` -- la misma corrida corta en Chromium y Firefox da el digest de `native-headless`; el archivo grabado en web se reproduce en nativo con el mismo digest.
- [x] `.gitlab-ci.yml` -- `playtest_smoke` dentro de los jobs con disco; sin job de soak.
- [x] `tools/port/playtest/soak.sh` -- construye `native-headless` y `native-asan` y corre `run.py --soak` mediante `tools/port/remote_run.sh`; deja el informe fechado en `build/port/playtest/soak/` (fuera de Git) -- apto para cron.
- [x] `docs/port/playtest.md`, `docs/port/build.md`, `docs/STATUS.md` -- cómo grabar, reproducir un fallo, agregar una sesión grabada o una card, correr el soak en `docker-server`; velocidad medida del headless (VBlanks por segundo de pared) y meta de horas ajustada.

**Acceptance Criteria:**
- Given una sesión grabada en `native` con ventana o en web, when se reproduce en `native-headless`, then el digest final coincide con el de la sesión grabada.
- Given la corrida corta del bot, when corre en `native-headless`, `native-asan` y web, then los tres digests finales son iguales.
- Given las Escenas existentes, when corren con y sin `--digest`, then capturas y trazas de reloj son idénticas.
- Given un fallo detectado, when se ejecuta el comando impreso en su directorio de reproducción, then el fallo se repite en el mismo VBlank.
- Given un merge a `main`, when corre el CI, then `playtest_smoke` corre en los jobs con disco; given `tools/port/playtest/soak.sh`, when corre en `docker-server`, then el informe lista horas virtuales, overlays visitados y fallos.

## Implementation Notes

- Implementación en Codex (sesión `01a1ccca`, 50 min). Archivos nuevos: `port/psyq/playtest.{c,h}`, `tools/port/playtest/{bot.py,run.py,soak.sh,test_playtest.py}`, `port/web/tests/playtest.spec.cjs`, `port/psyq/tests/test_playtest{,_runtime}.c`, `docs/port/playtest.md`. `crt.c` centraliza el corte por `--vblanks` contado desde el frame 0 en nativo y web, y cierra digest y Escena antes de salir.
- El oráculo encontró en la primera corrida larga una divergencia real entre `native-headless` y `native-asan` (VBlank 8.640). Como la spec prohíbe cambiar `src/`, la corrección (`adv` sin inicializar en `drawString2`) se hizo fuera de la historia, en `main` (`83601d8`), y la línea base de esta spec se movió a ese commit. Después, los cuatro builds dan `7614dc25…` en 29.435 VBlanks.
- Velocidad medida del headless Debug: 72 VBlanks/s (ASan 63). Es poco para soaks largos; un preset optimizado queda para después (`deferred-work.md`).
- `fnptr_inventory` registraba números de línea de `script_textbox.c`; la corrección `83601d8` los corrió y se actualizó el inventario en `e1ed792` (`main`).
- `sdl_pacing` falla en el contenedor local (CPU i5-6600K con 5 CPUs) por resincronizaciones en la carga sintética de GPU: `main` sin esta historia da 7-8 y la rama 5-9 en las mismas condiciones. No es regresión de la historia.
- `web_playtest` falló en Firefox local tras los parches: la prueba en vivo soltaba `k` al llegar al VBlank 300 y, con la CPU lenta, Enter se había soltado en el 356, así que `k` nunca se latcheó. El test ahora mantiene cada tecla hasta verla en la grabación y corre 1200 VBlanks.
- Cinco comparaciones anteriores (`scene_title_audio_compare`, `scene_xa_op1_compare`, `scene_movie0_compare`, `scene_movie0_skip_compare`, `scene_card_continue_compare`) necesitan referencias privadas que no están en los contenedores locales; las cubre el CI.

## Spec Change Log

## Review Triage Log

Pasada 1 (Codex `codex-merged`, 2026-11-11). Hallazgos repetidos entre capas en una sola fila.

| # | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|
| 1 | `web/tests/run.py` corre el smoke nativo sin `--pack` (BH1, ECH10) | high | `run.py:193` usa `native-headless/us.pack`; el job `web-disc` solo compila `native-headless` y el fixture `disc_pack` escribe el pack del build web: `web_playtest` fallaría en CI | patch |
| 2 | `soak.sh` no crea `us.pack` (BH2, ECH11) | medium | el script compila y corre `run.py` sin generar el pack; en `docker-server` recién preparado no hay pack | patch |
| 3 | `--inputs` con `--session`/`--prefix` apaga el bot en todos los casos (BH3, ECH3) | low | `run.py:221` decide por `a.inputs` global; corrección directa | patch |
| 4 | `resolve_prefix` da `KeyError` sin `checkpoints` (BH4, ECH1) | medium | `movie0.toml` y `xa_op1.toml` son `inputs_from = "mark"` sin `after` y `bot.py:51` indexa `scene['checkpoints']` | patch |
| 5 | Ruta rápida toma `capture_vblank` relativo a la marca como fin absoluto (BH5, ECH2) | medium | `title.toml` no declara `inputs_from`, tiene `mark = runLandingScreen` y `capture_vblank = 60`: el bot empezaría antes del título | patch |
| 6 | `--hang-seconds nan` desactiva el detector (BH6, ECH4) | low | `nan <= 0` es falso y ninguna comparación vence; corrección directa | patch |
| 7 | `summary.json` viejo tras la excepción de `main()` (BH7, ECH5) | low | la rama `except` solo reescribe `report.json`; el CI publica `summary.json` | patch |
| 8 | El estado del frame 0 no entra en la cadena (BH8, ECH12) | false | la cadena es la misma en todos los builds y empieza en el mismo VBlank; el primer `tick` ya incluye el display y la máscara aplicada en el frame 0 | reject |
| 9 | La Shell guarda digest y grabación sin límite (BH9, ECH9) | low | ~1,3 MB por hora de digest y menos de grabación; nadie lo encuentra en una sesión normal y la corrección agrega almacenamiento | reject |
| 10 | `STATUS.md` contradice a `playtest.md` (BH10) | medium | `STATUS.md` todavía dice divergencia sin causa | patch (orquestador) |
| 11 | Cierre de SDL con grabación sin test (VG1) | medium | hueco verificado por la capa: `sdl_quit` corre sin flags de grabación | patch |
| 12 | Descarga a mitad de partida sin test (VG2) | medium | hueco verificado: ambos tests descargan después de `Module.finished` | patch |
| 13 | Digest independiente del intervalo sin test (VG3) | medium | hueco verificado: los tests fijan siempre el mismo intervalo | patch |
| 14 | `--record-inputs` igual a `--inputs` trunca la sesión (ECH6) | low | requiere pasar el mismo archivo a dos flags; la guarda agrega comparación de inodos | reject |
| 15 | `--digest` igual a otro archivo (ECH7) | low | mismo caso que 14 | reject |
| 16 | `record-inputs` sin `digest` en la Shell deja el botón sin efecto (ECH8) | low | la instantánea solo la publica el tick del digest; corrección de una línea | patch |
| 17 | Los hosts ignoran inputs antes del primer retorno de `VSync` (ECH13) | false | es el origen común pedido por la spec (`<vblank>` desde el frame 0 en nativo y web); antes del frame 0 el Juego no consulta el pad y `port_vsync_return` latchea el primer estado | reject |

## Design Notes

El estado por variables de `state_vars.txt` necesita `dladdr` y no existe en web. Por eso el oráculo entre builds es el digest: el área de display ya es idéntica entre nativo y web en el título, y junto con el pad y `rand` detecta una divergencia de lógica en cuanto se ve o altera el azar.

El bot es deliberadamente tonto. Su cobertura viene del punto de partida: el prefijo de partida nueva y las sesiones grabadas por el dueño, que se reproducen como prefijo antes de los inputs aleatorios. Qué overlays alcanza se mide en el informe; llegar a los 16 es trabajo de la Historia 6.10.

## Verification

**Commands:**
- `tools/port/docker_run.sh cmake --build --preset native-headless && ctest --preset native-headless` -- expected: todo verde, incluidos `playtest_unit` y el test C del digest.
- `tools/port/docker_run.sh ctest --preset native-asan` -- expected: todo verde.
- `DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native-headless -R 'playtest_smoke|scene_'` -- expected: verde, sin skips.
- `python3 -I -B port/web/tests/run.py playtest` (imagen `dw-port:remote`) -- expected: digest web igual al nativo en Chromium y Firefox.
- `tools/port/ci/preproc_check.sh` y `make compare` -- expected: sin cambios en `src/`; binarios idénticos.
