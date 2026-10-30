---
title: 'Historia 4.5: Teclado y gamepad'
type: 'feature'
created: '2026-10-30'
status: 'in-review'
baseline_commit: 'bf856c03c697c7e3c2bc894a40286850ffa7558c'
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
- `docs/port/controls.md` -- mapeo nativo; agregar la tabla web por defecto.
- Sin Pack el juego corre igual (4.4): el latch y `port_pad_value` se observan sin disco; que el juego reaccione a "Start" en el título requiere el Pack real (clase B).

## Tasks & Acceptance

**Execution:**
- [x] `port/host/web/input.c`, `port/host/web/input.h` -- bloque compartido exportado (`web_input_address`: máscara, número de muestra; el hilo del juego publica número y VBlank del último latch y `port_pad_value`); `plat_input_vblank` web -- latch por VBlank (AD-8).
- [x] `port/psyq/libetc.c`, `port/psyq/crt.c`, `port/host/plat.h`, `CMakeLists.txt` -- `port_pad_value`, llamada web sin captura, fuente y export; CTest `web_input`.
- [x] `port/web/dev/index.html` -- muestreo en el hilo principal: estado de teclas por `code`, gamepads por rAF, OR, escritura de máscara y número; `blur` suelta teclas; opción "Stick izquierdo como cruceta" en `#options`; `Module.input()` para tests.
- [x] `port/web/tests/input.spec.cjs`, `run.py` -- suite `input` clase A con toda la matriz (gamepad simulado reemplazando `navigator.getGamepads`).
- [ ] `port/web/tests/title.spec.cjs` -- clase B: "Start" por teclado en el título cambia de escena -- PENDIENTE: requiere el Pack real y una señal de escena posterior al título; ver evidencia.
- [x] `docs/port/controls.md`, `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-5/` -- mapeo web por defecto, cómo correr, evidencia y pendientes.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_input` pasa en Chromium y Firefox y las suites existentes siguen verdes.
- Given el mapeo de `docs/port/controls.md`, when el test pulsa cada uno de los 14 botones por teclado y por gamepad, then `port_pad_value` refleja exactamente ese bit en el orden de `PadRead`.
- Given `DW1_DISC_DIR`, when el test pulsa Start en la pantalla de título, then el juego sale del título (pendiente sin disco).

## Design Notes

Bloque de input (u32): `mask`, `seq` (escribe el hilo principal; primero `mask`, después `seq` con `Atomics.store`); `latched_seq`, `latched_vblank`, `pad_value` (escribe el hilo del juego en el latch). El muestreo corre en `requestAnimationFrame` y en cada evento de teclado; con la pestaña oculta el juego ya está pausado (4.4).

## Verification

**Commands:**
- `cmake --build --preset web && python3 port/web/tests/run.py build/port/web input` -- expected: todos los casos pasan.
- `ctest --preset web` -- expected: verde; clase B saltada.
- `ctest --preset native-headless` -- expected: verde (cambios web-only salvo `port_pad_value`).
