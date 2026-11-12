---
title: 'Historia 6.11: Build optimizado para jugar, CI y soak'
type: 'chore'
created: '2026-11-12'
status: 'ready-for-dev'
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
- [ ] `CMakePresets.json` -- agregar los presets configure, build y test `native-headless-release`, `native-release` y `web-release`, cada uno en `build/port/<name>`.
- [ ] `CMakeLists.txt` -- registrar `sdl_pacing` y `native_wall_10min` solo en builds no Debug. Mantener el `-O2` de `dw1_backend`.
- [ ] `.gitlab-ci.yml` -- en `native`, construir `native-release` y correr sus tests de pared. En `native-disc`, correr `native_wall_10min` y `playtest_smoke` en release. En `testing`, construir, probar y desplegar `web-release`. En `web-disc`, correr `web_playtest` también sobre `web-release`.
- [ ] `tools/port/playtest/soak.sh` -- soak sobre `native-headless-release` y `native-asan`.
- [ ] `tools/port/playtest/run.py` / CMake -- `playtest_smoke` compara el digest entre `native-headless` y `native-headless-release` cuando los dos están construidos.
- [ ] Documentación -- `setup.md`, `playtest.md`, `testing-env.md` y `deferred-work.md` (cerrar el ítem de la 6.9).

**Acceptance Criteria:**
- Given `native-release` en el host de desarrollo, when corre `sdl_pacing` 5 veces, then pasa las 5 sin cambiar `test_sdl_pacing.py`.
- Given el disco, when la corrida smoke del bot corre en `native-headless`, `native-headless-release`, `native-asan` y `web-release`, then los cuatro dan el mismo digest final.
- Given `native-headless-release`, when corre el soak, then avanza al menos 2,5 veces más VBlanks por segundo que `native-headless`. La medición queda en Implementation Notes.
- Given `web-release`, when corren `web_launcher`, `testing_deploy`, `testing_context`, `web_scene_title` y `web_shell`, then pasan.
- Given las suites completas `native-headless`, `native-asan` y `web` con disco, when corren después del cambio, then pasan igual que antes.

## Implementation Notes

## Spec Change Log

## Review Triage Log

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'cmake --preset native-release && cmake --build --preset native-release && for i in 1 2 3 4 5; do ctest --preset native-release -R "^sdl_pacing$" || exit 1; done'` -- expected: 5 pasadas.
- `DW1_DISC_DIR=/dw/game_rom/extracted` + `ctest --preset native-headless-release -R "pack_disc|playtest_smoke|scene_"` en `remote_run.sh` -- expected: todo pasa y el digest coincide con Debug.
- `DW_IMAGE=dw-port:remote tools/port/remote_run.sh` con `ctest --preset web-release -R "web_launcher|web_shell|testing_deploy|testing_context|web_scene_title|web_playtest"` y el disco -- expected: pasa.
- `ctest --preset native-headless`, `--preset native-asan` y `--preset web` completos en `remote_run.sh` con disco -- expected: sin regresiones.
