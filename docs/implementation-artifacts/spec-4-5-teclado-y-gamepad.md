---
title: 'Historia 4.5: Teclado y gamepad'
type: 'feature'
created: '2026-10-30'
status: 'in-review'
baseline_commit: 'cfcda1b27d62dbdbbb812d98350f02320b3a55b4'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-4-pantallas-de-la-shell-web-y-capacidades.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** En el navegador el juego no recibe input: `plat_input_vblank` solo existe en `native` (`port/host/sdl/input.c`) y `crt.c` no lo llama en web, así que no se puede jugar con teclado ni gamepad (FR-8).

**Approach:** El hilo principal muestrea teclado (`KeyboardEvent.code`) y gamepads (`navigator.getGamepads`, mapeo estándar W3C) y publica la máscara de los 14 botones digitales, con número de muestra, en una palabra de `SharedArrayBuffer`; el hilo del juego la aplica con `port_pad_latch` en cada VBlank (mismo punto que `native`). Mapeo por defecto igual al nativo, documentado en `docs/port/controls.md`; opción "stick izquierdo como cruceta".

## Boundaries & Constraints

**Always:** el hilo del juego nunca toca APIs del DOM ni de gamepad, solo lee la palabra compartida en el latch del VBlank; la máscara usa el orden de bits de cable de `port_pad_latch` (select 0, start 3, arriba 4, derecha 5, abajo 6, izquierda 7, L2 8, R2 9, L1 10, R1 11, triángulo 12, círculo 13, cruz 14, cuadrado 15); teclado y gamepads se combinan (OR); el teclado cuenta solo con la página enfocada y se suelta todo en `blur`; conectar o desconectar gamepads no pausa el juego ni cambia el ritmo; en corridas de captura de Escena no se aplica input en vivo (igual que `native-headless`, para no cambiar capturas); un solo reloj virtual.

**Never:** remapeo y persistencia de opciones (4.6: la opción del stick vive en memoria y arranca desactivada); sticks analógicos como pad analógico (DualShock); vibración; cambios en `src/`; input de dos jugadores.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Teclado | cada tecla del mapeo, pulsar y soltar | el bit llega al valor de `PadRead` y se limpia al soltar | — |
| Gamepad | mando simulado, cada botón estándar 0-9 y 12-15 | idem; L2/R2 por `value > 0,5` | — |
| Combinación | tecla + botón distintos | ambos bits | — |
| Stick como cruceta | opción activa, eje izquierdo ±1 | bits de dirección; opción inactiva: nada | zona muerta 0,5 |
| Conexión en caliente | `gamepadconnected`/`disconnected` simulados | juego sigue, ritmo ±5 %, 0 resincronizaciones | mando ausente = sin bits |
| Latch | muestra con número n | aplicada en el primer latch de VBlank posterior a la escritura | — |
| Foco perdido | tecla pulsada y `blur` | bits de teclado en 0 | — |
| Captura de Escena | `?capture-mark` | sin latch en vivo | — |

</frozen-after-approval>

## Code Map

- `port/host/sdl/input.c` -- mapeo nativo de referencia (teclas y botones SDL → bits) y `plat_input_vblank` → `port_pad_latch`.
- `port/psyq/libetc.c:52-70` -- `port_pad_latch` (cable → `pad_buf`) y `PadRead` (cobra costo de reloj: no llamarlo desde tests). Agregar `port_pad_value()` sin costo para observar lo que devolvería `PadRead`.
- `port/psyq/crt.c:94-96` -- `plat_input_vblank` solo en `#ifndef __EMSCRIPTEN__`; habilitarlo en web (salvo `capture_config.inputs`).
- `port/host/plat.h` -- declarar `plat_input_vblank` también en web.
- `port/host/web/pace.c` -- precedente de bloque compartido exportado (`web_control_address`); el input usa su propio bloque.
- `port/web/dev/index.html` -- `boot()` y `pauseControls()`: lugar para arrancar el muestreo (rAF + eventos) y la opción del stick en `#options`.
- `port/web/tests/shell.spec.cjs` -- `running()`, `expectPaced()`, `sample()` para la prueba de conexión en caliente.
- `port/host/web/present_gl.c`, `showFrames()` en `index.html` -- desde 4.4 el hilo del juego postea cada frame a `#screen`; ninguna capa de input pasa por ahí.
- `port/psyq/scene.c` -- rama web de `__cyg_profile_func_enter`.
- `docs/port/controls.md` -- mapeo nativo; agregar la tabla web por defecto.
- Sin Pack el juego corre igual (4.4): el latch y `port_pad_value` se observan sin disco; que el juego reaccione a "Start" en el título requiere el Pack real (clase B).

## Tasks & Acceptance

**Execution:**
- [x] `port/host/web/input.c`, `port/host/web/input.h` -- bloque compartido exportado (`web_input_address`: máscara, número de muestra; el hilo del juego publica número y VBlank del último latch y `port_pad_value`); `plat_input_vblank` web -- latch por VBlank (AD-8).
- [x] `port/psyq/libetc.c`, `port/psyq/crt.c`, `port/host/plat.h`, `CMakeLists.txt` -- `port_pad_value`, llamada web sin captura, fuente y export; CTest `web_input`.
- [x] `port/web/dev/index.html` -- muestreo en el hilo principal: estado de teclas por `code`, gamepads por rAF, OR, escritura de máscara y número; `blur` suelta teclas; opción "Stick izquierdo como cruceta" en `#options`; `Module.input()` para tests.
- [x] `port/web/tests/input.spec.cjs`, `run.py` -- suite `input` clase A con toda la matriz (gamepad simulado reemplazando `navigator.getGamepads`).
- [x] `port/web/tests/title.spec.cjs`, `port/psyq/scene.c` -- clase B: "Start" por teclado tras el clic en «Jugar» abre el menú principal; la señal es la marca `runMainMenu` que el hook web de `-finstrument-functions` publica en `Module.marks`, sin tocar `src/`.
- [x] `port/web/dev/index.html` -- `#pause` suelta el foco tras el clic: si no, `inputControls` ignora todo el teclado hasta el siguiente clic fuera del botón.
- [x] `port/web/tests/long.spec.cjs`, `port/web/tests/run.py` -- suite manual `long` (clase B, L4 de presentación): 10 minutos en Chromium y Firefox midiendo ritmo, frames pintados, memoria del navegador y recuperación tras bloquear 2 s el hilo principal; resuelve la entrada de contrapresión de `deferred-work.md`.
- [x] `port/host/web/present_gl.c`, `port/web/dev/index.html`, `port/web/tests/shell.spec.cjs`, `port/web/tests/storage.cjs`, `CMakeLists.txt` -- presentación por memoria compartida: `glReadPixels` en el hilo del juego y `showFrames` pinta el frame más nuevo en cada `requestAnimationFrame`; `web_shell` falla si el navegador crece en 30 s.
- [x] `docs/port/controls.md`, `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-5/` -- mapeo web por defecto, cómo correr, evidencia y pendientes.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_input` pasa en Chromium y Firefox y las suites existentes siguen verdes.
- Given el mapeo de `docs/port/controls.md`, when el test pulsa cada uno de los 14 botones por teclado y por gamepad, then `port_pad_value` refleja exactamente ese bit en el orden de `PadRead`.
- Given `DW1_DISC_DIR`, when el test pulsa Start en la pantalla de título, then el juego sale del título (`web_scene_title`, marca `runMainMenu`).
- Given una corrida de 10 minutos con el Pack real, when `run.py <build> long` termina, then se pinta más del 99 % de los frames, la memoria no crece más de 10 %, el ritmo se desvía menos de 0,1 % y el juego sigue tras un bloqueo de 2 s del hilo principal.

## Design Notes

Presentación: la prueba L4 no encontró cola, pero sí una fuga en Chromium de un frame por frame (6 GB en 10 minutos con `ImageBitmap`). Sondas de 40 s en `evidence/story-4-5/frame-leak-probe-chromium.jsonl`: cerrar el bitmap en el hilo que lo creó no pierde memoria; cerrarlo en el hilo principal, sí. La hipótesis coherente es que Chromium libera el recurso en el bucle del hilo emisor, que el juego nunca devuelve. Dibujar sin transferir también crece, en el proceso GPU, y `glReadPixels` lo evita. El contexto GL sigue en el hilo del juego (AD-9). Como solo se lee memoria compartida en `requestAnimationFrame`, no hay mensajes y nada puede encolarse. En Firefox `glReadPixels` espera al hilo principal: con él bloqueado el juego se detiene (0 VBlanks en 2 s) y sigue al soltarlo.

Bloque de input (u32): `mask`, `seq` (escribe el hilo principal; primero `mask`, después `seq` con `Atomics.store`); `latched_seq`, `latched_vblank`, `pad_value` (escribe el hilo del juego en el latch). El muestreo corre en `requestAnimationFrame` y en cada evento de teclado; con la pestaña oculta el juego ya está pausado (4.4).

## Verification

**Commands:**
- `cmake --build --preset web && python3 port/web/tests/run.py build/port/web input` -- expected: todos los casos pasan.
- `ctest --preset web` -- expected: verde; clase B saltada.
- `ctest --preset native-headless` -- expected: verde (cambios web-only salvo `port_pad_value`).

## Spec Change Log

- 2026-10-30, integración local: la rama cloud dejaba sin implementar la clase B y no conocía los cambios de 4.4. Se agregaron la señal `runMainMenu` sin tocar `src/`, `?frames=0` en los tests, la suelta de foco en Pausa y la suite `long`. La L4 reveló la fuga de `ImageBitmap`: la presentación pasó a memoria compartida. Reemplaza el camino descrito en las Design Notes de 4.4; AD-9 no cambia. KEEP: máscara, latch y orden de bits de la rama cloud.

## Review Triage Log

Revisión Codex (`codex-merged`) del diff completo contra `main` tras integrar la rama cloud.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| `input.c`: máscara y número se leen por separado | false | rechazado | Se lee `seq` antes que `mask` y el hilo principal escribe en orden inverso: la máscara aplicada nunca es más vieja que el número publicado, que es la garantía que usan los tests. |
| Pulsación y soltado entre dos VBlanks se pierden | low | rechazado | Mismo modelo que `native` (`sdl/input.c`): el spec congela una máscara por latch; una pulsación menor a 16,7 ms es rara y conservarla agrega estado. |
| El checkbox del stick retiene el foco | false | rechazado | La opción vive en `#launcher`, que queda oculto al arrancar: el foco vuelve a `body`. |
| Enter sobre Pausa se repite como Start después del `blur` | medium | patch | Reproducible al reanudar con Enter sostenido. `keydown` con `repeat` ya no publica; cubierto en `input.spec.cjs`. |
| `getGamepads` que lanza detiene el muestreo | low | rechazado | Exige una `Permissions-Policy` que el port no envía; no hay camino demostrado. |
| Las pruebas no verifican el primer VBlank elegible | medium | patch | Test nuevo que mide en la página la distancia entre el último latch y el que consume la muestra. |
| `port_pad_latch_applies` sin prueba propia | low | patch | `pad_irq_publication` en `test_clock.c` ahora comprueba `port_pad_latch_applies` y `port_pad_value` en el sondeo omitido. |
| La suite `long` no corre en CI | false | rechazado | AD-14 define L4 como disparo manual antes de cerrar M4 y M5. |
| La memoria suma procesos de otros navegadores | false | rechazado | En el contenedor corre un solo navegador por proyecto (`workers: 1`). |
| Lectura no atómica de `Module.input` | low | rechazado | Solo la usan los tests, sobre un botón estable; no afecta al juego. |
| `DW1_WEB_LONG_MINUTES` inválido | low | rechazado | Herramienta manual que falla ruidosamente. |
| Umbral 67 ms frente a cuatro frames | low | patch | `4000 / RATE`, sin ramas nuevas. |

Segunda revisión Codex, solo del cambio de presentación (`blind-hunter` y `edge-case-hunter`).

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| Pérdida del contexto WebGL deja la imagen congelada | low | rechazado | Tampoco se manejaba con `ImageBitmap`; ningún flujo del port la provoca hoy. |
| `ms` no incluye `glReadPixels` | low | rechazado | Es informativo y sin umbral, igual que en la revisión de 4.4; el rendimiento se evalúa en la 5.6. |
| Se eliminó la medición de latencia | false | rechazado | Sin mensajes no hay demora acumulable: cada animation frame lee el frame más nuevo. |
| El test de memoria pasa con el juego congelado | medium | patch | `shell.spec.cjs` exige más de 300 frames pintados durante la medición. |
| `seq` con signo en JavaScript | low | rechazado | Necesita 2^31 escrituras, unos 11 meses de corrida continua; solo afecta un contador de tests. |
| `getContext('2d')` devuelve null | false | rechazado | `#screen` no tiene otro contexto y Shell lo crea una sola vez. |
