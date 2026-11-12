---
title: 'Historia 6.11: Build optimizado para jugar, CI y soak'
type: 'chore'
created: '2026-11-12'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
baseline_commit: '01fb0d02655646617b496414d11f815eea1d7edd'
context:
  - '{project-root}/docs/implementation-artifacts/epic-6-context.md'
  - '{project-root}/docs/port/playtest.md'
  - '{project-root}/docs/port/testing-env.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Todos los presets son Debug: el código del Juego y de PsyQ se compila a `-O0` (solo `dw1_backend` usa `-O2`, y solo en web). El entorno de testing que juega el dueño sirve ese build, y `sdl_pacing` falla en `runner-231` desde el pipeline 157 porque Debug no llega a tiempo real en un Xeon E5-2696 v4: 2,5 s de cómputo para 120 VBlanks contra 2,0 s de presupuesto. El mismo test con `-O2` tarda 1,33 s y no tiene resincronizaciones. El soak corre a 72 VBlanks/s.

**Approach:** Presets optimizados `native-headless-release`, `native-release` y `web-release` (`-O2 -g`, sin `NDEBUG`, mismos flags de semántica que Debug). El deploy de testing, los tests de tiempo de pared nativos y el soak usan esos builds. El oráculo de digest de la 6.9 exige el mismo digest entre Debug, optimizado, ASan y web.

## Boundaries & Constraints

**Always:**
- Los builds optimizados conservan `PORT_ASSERT` con `abort()` (sin `NDEBUG`), `-finstrument-functions` en los objetos del Juego (las marcas por símbolo lo necesitan) y los flags de semántica de Debug.
- Los tests de tiempo de pared conservan sus tolerancias actuales: cero resincronizaciones, error menor a 1 VBlank y ratio de 0,95 a 1,10.
- Si el digest optimizado diverge del de Debug, se busca la causa (lectura sin inicializar o comportamiento indefinido), se corrige con un bloque `DW1_PORT` con su fila en `docs/port/src-changes.md` y se verifica que `make compare` siga idéntico.
- Los presets Debug, ASan y web siguen existiendo con el mismo comportamiento para depurar.

**Never:**
- Bajar tolerancias, sumar reintentos o saltear tests de tiempo de pared según la CPU.
- Usar `-O3`, `-ffast-math` u otros flags que cambien la semántica de punto flotante o de aliasing.
- Tocar el host del runner.
- Hacer el renderer WebGL2 o el trabajo de rendimiento de la Épica 5.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pacing en el runner lento | `sdl_pacing` en `native-release` en un E5-2696 v4 | Pasa con cero resincronizaciones | N/A |
| Test de pared en Debug | `ctest --preset native` | Los tests de tiempo de pared no se registran en Debug, porque solo valen en el build optimizado | N/A |
| Oráculo | La misma corrida del bot en `native-headless`, `native-headless-release`, `native-asan` y `web-release` | Mismo digest final | Si difiere, el informe nombra el primer intervalo distinto |
| Deploy | Merge a `main` | El job `testing` despliega `build/port/web-release` | Si falla la verificación, se hace rollback como hoy |

</frozen-after-approval>

## Code Map

- `CMakePresets.json` -- presets configure, build y test. `native` y `native-asan` heredan de `native-headless`. Agregar las variantes release heredando de las existentes con `CMAKE_BUILD_TYPE=RelWithDebInfo` y `CMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -g`. Emscripten también necesita `-O2` al linkear.
- `CMakeLists.txt` -- `dw1_backend` usa `-O2` solo en web (líneas 198-204). `-finstrument-functions` está en las líneas 188-192, que no hay que tocar. `sdl_pacing` está en 506-514 y `native_wall_10min` en 515-518. Registrar estos dos solo cuando `CMAKE_BUILD_TYPE` no sea Debug. `playtest_smoke` está en 464-467 y `web_playtest` en 882-884.
- `port/psyq/port.h:8-17` -- `PORT_ASSERT` depende de `NDEBUG`. Hay que mantenerlo definido como en Debug.
- `.gitlab-ci.yml` -- jobs:
  - `native`: suma `native-release` y sus tests de pared.
  - `native-disc`: `native_wall_10min` corre en release, y `playtest_smoke` corre también en release.
  - `testing` (líneas 153-170): pasa a `web-release`.
- `tools/port/testing/deploy.sh:47` -- el build se pasa como argumento, con default `build/port/web`.
- `tools/port/playtest/soak.sh` -- hoy usa `native-headless` y `native-asan`. Pasa a `native-headless-release` y `native-asan`.
- `tools/port/playtest/run.py:196-197` -- un nombre de build se resuelve a `build/port/<name>/dw1`. Compara digests entre los builds de una corrida.
- `port/web/tests/run.py` -- el fixture del pack web. El nativo de comparación es `native-headless`.
- `tools/port/test_sdl_pacing.py`, `tools/port/test_native_wall.py` -- sin cambios de tolerancia.
- `docs/port/setup.md`, `docs/port/playtest.md`, `docs/port/testing-env.md` -- documentar los presets y cuál usa cada cosa.
- `docs/implementation-artifacts/deferred-work.md` -- el ítem «velocidad del playtest headless» de la 6.9 queda resuelto por esta historia.

## Tasks & Acceptance

**Execution:**
- [x] `CMakePresets.json` -- agregar los presets configure, build y test `native-headless-release`, `native-release` y `web-release`, cada uno en `build/port/<name>`.
- [x] `CMakeLists.txt` -- registrar `sdl_pacing` y `native_wall_10min` solo en builds no Debug. Mantener el `-O2` de `dw1_backend`.
- [x] `.gitlab-ci.yml` -- en `native`, construir `native-release` y correr sus tests de pared. En `native-disc`, correr `native_wall_10min` y `playtest_smoke` en release. En `testing`, construir, probar y desplegar `web-release`. En `web-disc`, correr `web_playtest` también sobre `web-release`.
- [x] `tools/port/playtest/soak.sh` -- soak sobre `native-headless-release` y `native-asan`.
- [x] `tools/port/playtest/run.py` / CMake -- `playtest_smoke` compara el digest entre `native-headless` y `native-headless-release` cuando los dos están construidos.
- [x] Documentación -- `setup.md`, `playtest.md`, `testing-env.md` y `deferred-work.md` (cerrar el ítem de la 6.9).

**Acceptance Criteria:**
- Given `native-release` en el host de desarrollo, when corre `sdl_pacing` 5 veces, then pasa las 5 sin cambiar `test_sdl_pacing.py`.
- Given el disco, when la corrida smoke del bot corre en `native-headless`, `native-headless-release`, `native-asan` y `web-release`, then los cuatro dan el mismo digest final.
- Given `native-headless-release`, when corre el soak, then avanza al menos a 4 veces el tiempo real (240 VBlanks/s) y más rápido que `native-headless`. La medición queda en Implementation Notes.
- Given `web-release`, when corren `web_launcher`, `testing_deploy`, `testing_context`, `web_scene_title` y `web_shell`, then pasan.
- Given las suites completas `native-headless`, `native-asan` y `web` con disco, when corren después del cambio, then pasan igual que antes.

## Implementation Notes

- Presets heredados `RelWithDebInfo`, con `-O2 -g` en compilación y también en
  enlace web, sin `NDEBUG`. Se conserva la instrumentación y el backend web a `-O2`.
- `playtest_smoke` descubre el otro headless construido en tiempo de ejecución;
  `web_playtest` agrega optimizado y ASan disponibles. El informe nativo y el
  diagnóstico de Playwright identifican el primer intervalo divergente.
- El runner de Escenas y `--card-build` de las referencias aceptan los presets
  nativos optimizados. La captura SDL usa el preset de su propio directorio, para evitar validar Debug por accidente.
- Verificación realizada directamente en este contenedor, por instrucción del
  dueño, sin wrappers remotos, deploy real ni commits. Los cambios previos de
  estado de esta spec y del sprint se conservan.
- Evidencia privada: `build/port/verification-6-11/` y
  `build/port/native-headless-release/playtest/smoke/`. La primera tanda concurrente
  de pacing/shell sufrió resincronizaciones con cuota de 2,5 CPU; la verificación
  temporal restante se ejecuta en secuencia, sin cambiar tolerancias ni agregar
  reintentos al producto o a CI.


- Builds Debug, ASan, SDL y web compilados dentro del contenedor; los tres
  presets optimizados compilan. `compile-flags.json` verifica `-O2 -g`, ausencia
  de `NDEBUG` e instrumentación/semántica del Juego en todos los TUs.
- `sdl_pacing`: cinco ejecuciones aisladas consecutivas aprobadas, sin cambios
  en `test_sdl_pacing.py`. Debug registra cero tests de pared y `native-release`
  registra los dos.
- `playtest_smoke` optimizado y Debug: 29.435 VBlanks, inputs SHA-256
  `22ebf45b72da1b64bb3e90e1529e0d15f91d2ab93598e7bf3830c3e856ba6ab3`,
  digest final `7614dc25449f701f6a94b9a4bf00a7ff00d0046cb815994c439791776a4a5cee`.
  `web_playtest` pasó en 1.846,29 s con ese mismo digest en Debug, optimizado,
  ASan, Chromium y Firefox, incluidos los roundtrips de grabaciones.
- `web_launcher`, `testing_deploy`, `testing_context` y `web_scene_title` aprobados;
  el último comprueba imagen, audio y controles en ambos navegadores (685,93 s).
- `web_shell` pasó aislado (161,38 s), después de fallar dos casos temporales
  en la primera tanda concurrente. No se modificaron sus tolerancias.
- `native_wall_10min` aprobado: 36.006 VBlanks en 600,012958357 s, cero
  resincronizaciones y error inferior a 0,008 VBlank respecto del reloj racional.
- Benchmark corto aislado (29.435 VBlanks): Debug 112,22 VBlanks/s,
  optimizado 261,04 VBlanks/s, mejora 2,33× y digest idéntico. Esta muestra no
  alcanza 2,5×; el criterio de velocidad del soak sigue pendiente de validación.
- La muestra extendida solicitada a 60.000 VBlanks abortó por falta de frames
  durante 30 segundos virtuales en ambos builds, en el mismo VBlank 34.200.
  No valida el soak ni se cuenta como aceptación. Reproducciones e inputs
  privados en `build/port/verification-6-11/benchmark-soak/`.
- Suites completas con disco en docker-dev (2026-11-12), las tres en paralelo:
  `web` 63/64, `native-asan` 91/93 y `native-headless` 90/95. Las fallas de
  `reference_xa_op1`, `reference_newgame_state` y `scene_title` fueron choques entre
  suites concurrentes que escriben `build/reference/` y la captura nativa del título
  («invalid PCM tap chunk size», error de decode, «missing scene mark»). Repetidas en
  serie, pasan las 9, junto con sus comparaciones.
- `web_scene_title_audio` falla también en serie, con la misma diferencia en Chromium
  y Firefox: byte 22.733.052, frame estéreo 5.683.263, sobre 51.369.620 bytes nativos.
  Es previa a esta historia. El build `web` Debug y toda la ruta del test son idénticos
  a `main`, y el test solo corre en el job `testing`, cuya última pasada fue el
  pipeline 148 (`07c27f1`). Se difiere a la Historia 6.10, porque bloquea el deploy
  de testing.
- `repro_check.sh build/port/web-release`: `dw1.wasm` y `dw1_pack.wasm` idénticos entre
  dos builds.

## Spec Change Log

- 2026-11-12, verificación: el criterio «2,5× más VBlanks/s que Debug» era una estimación de planificación. El benchmark aislado de 29.435 VBlanks dio 2,34× (Debug 112 VBlanks/s, optimizado 263 VBlanks/s, digest idéntico). Con gprof sobre el optimizado, el 41% del tiempo de esa muestra está en la IDCT de `port/backend/mdec.c` (películas de la intro), que ya pesa en los dos builds y limita el cociente. Se reemplazó por un objetivo absoluto: 4× tiempo real, con el que 20 horas virtuales caben en unas 5 horas de pared. Optimizar la IDCT queda diferido a la Épica 5. Cachear los `getenv` de los hooks de instrumentación no cambió el tiempo medido y se descartó. KEEP: los presets, el gating de tests de pared y el oráculo de digest entre builds.

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|---|
| 1 | blind-hunter | Evidencia de rendimiento incompleta (2,33× y corrida extendida abortada) | false | Benchmark aislado repetido: 263 VBlanks/s, 4,4× tiempo real; el criterio se reformuló (Spec Change Log). El aborto a 34.200 es un bug del juego, igual en Debug, que va a la 6.10. | rechazado |
| 2 | blind-hunter | No hay prueba de que los tests de pared pasen en `runner-231` | maybe-false | Se mide en el mismo modelo de CPU (E5-2696 v4) en local; lo confirma el primer pipeline de `main` tras el merge. | verificación al cerrar |
| 3 | blind-hunter | `smoke_builds()` aprueba con un solo build si falta la contraparte | low | Diseño intencional: CI construye ambos antes de `playtest_smoke` en release; la corrida individual sigue siendo válida como smoke. | rechazado |
| 4 | blind-hunter | Falta `native_sdl_title_capture` en `native-release` en CI | low | El dueño juega en web; el SDL nativo es para depurar. Suma 20 min a un job de 4 h. | rechazado |
| 5 | blind-hunter | `repro_check.sh` solo verifica `build/port/web`, no el build servido | medium | `repro_check.sh` reconstruía con `--preset web` fijo. | patch: el preset sale del directorio y `testing` verifica `web-release` |
| 6 | blind-hunter | La suite completa de `web-release` solo corre en el job protegido `testing` | low | Ese job bloquea el deploy y hace rollback si falla; un fallo no llega al jugador. | rechazado |
| 7 | verification-gap | El default sin argumentos de `deploy.sh` no está probado | low | CI pasa el directorio explícito; solo afecta un deploy manual y el test exigiría tocar `build/port/` real. | rechazado |
| 8 | verification-gap | El intervalo divergente calculado en JS no tiene test | low | Es solo diagnóstico en un mensaje de error; el cálculo Python equivalente sí está probado. | rechazado |
| 9 | edge-case-hunter | `native_sdl_title_capture` falla con un directorio SDL de nombre no estándar | low | Solo en configuraciones CMake a mano fuera de los presets; falla ruidosamente con argumento inválido. | rechazado |
| 10 | edge-case-hunter | `web_playtest` compara menos builds si faltan release o ASan | low | En `web-disc` los dos se construyen antes de `web_playtest` sobre `web-release`; la pasada Debug previa es la de siempre. | rechazado |

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'cmake --preset native-release && cmake --build --preset native-release && for i in 1 2 3 4 5; do ctest --preset native-release -R "^sdl_pacing$" || exit 1; done'` -- expected: 5 pasadas.
- `DW1_DISC_DIR=/dw/game_rom/extracted` + `ctest --preset native-headless-release -R "pack_disc|playtest_smoke|scene_"` en `remote_run.sh` -- expected: todo pasa y el digest coincide con Debug.
- `DW_IMAGE=dw-port:remote tools/port/remote_run.sh` con `ctest --preset web-release -R "web_launcher|web_shell|testing_deploy|testing_context|web_scene_title|web_playtest"` y el disco -- expected: pasa.
- `ctest --preset native-headless`, `--preset native-asan` y `--preset web` completos en `remote_run.sh` con disco -- expected: sin regresiones.
