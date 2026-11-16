---
title: 'Historia 6.10: Partida real en testing y triage de caídas'
type: 'bugfix'
created: '2026-11-15'
status: 'in-progress'
route: 'dispatch'
review_loop_iteration: 0
baseline_commit: 'cc9f2176361d135871281e3d03535836b9d0dbf3'
context:
  - '{project-root}/docs/implementation-artifacts/epic-6-context.md'
  - '{project-root}/docs/port/playtest.md'
  - '{project-root}/docs/port/testing-env.md'
  - '{project-root}/docs/port/scenes.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El port solo se probó en Escenas cortas y en una corrida de bot. Esa corrida se cuelga en el VBlank 34.200 (overlay `SHOP`, display apagado). El CI de `main` está rojo desde el 2026-11-12 (pipelines 161-164): los tres jobs con disco pasan su timeout en `runner-231`, así que el entorno de testing no recibe el build actual. Además, `web_scene_title_audio` diverge del PCM nativo y la corrección del scratchpad (`BTL_drawTMD`) no se confirmó en un combate real.

**Approach:** Primero dejar verde el pipeline de `main` para que testing se despliegue. Después corregir en su causa los dos hallazgos conocidos, correr el soak de la 6.9 en `docker-server` hasta que no queden fallos nuevos y triar cada fallo con su reproducción y su test de regresión. Por último, confirmar el combate con una Escena y que el dueño juegue una sesión grabada en testing.

## Boundaries & Constraints

**Always:**
- Cada fallo queda registrado en `docs/port/triage.md` con: síntoma, VBlank, overlay, comando de reproducción, causa, commit de la corrección y test de regresión (una Escena o un archivo de inputs sintético con su digest).
- Cada corrección va a la causa raíz. Un cambio en `src/` va detrás de `DW1_PORT`, se anota en `docs/port/src-changes.md` y `make compare` sigue dando el binario idéntico.
- Timeouts del CI: se suben con un margen de ×1,5 sobre la duración medida en `runner-231` y no se quitan ni se acortan tests (preferencia del dueño, 2026-10-22).
- El soak corre con `tools/port/playtest/soak.sh` en `docker-server` y deja informes en `build/port/playtest/soak/` (privado).
- Las sesiones grabadas por el dueño, las cards y las capturas quedan fuera del repo. El repo guarda hashes, semillas e inputs sintéticos.

**Never:**
- No bajar tolerancias, no marcar tests como `allow_failure` y no reintentar hasta que pase.
- No cambiar el watchdog de 30 s virtuales para tapar un cuelgue, salvo que se demuestre que el juego original pasa más tiempo sin frames en esa misma situación (con PCSX-Redux como diagnóstico).
- No usar el runner del CI para el soak ni agregar un job de soak.
- No dividir `port/web/dev/index.html` (acción de retrospectiva aparte).

**Decisiones del dueño (2026-11-15):**
- Sesión del dueño: la implementación llega hasta CI verde, testing desplegado, soak limpio y Escena de combate. Ahí se detiene y el dueño juega alrededor de una hora en testing y deja el archivo de inputs descargado en almacenamiento privado. La historia cierra después de triar esa sesión.
- Escena de combate: inputs sintéticos escritos a mano desde `newgame.toml` hasta el primer combate, versionados como las demás Escenas, para que corra en CI sin depender de la sesión del dueño.

**Decisiones del dueño (2026-11-16, tras la primera implementación):**
- Lo hecho (correcciones 1 a 4 de `docs/port/triage.md`, timeouts) se mergea a `main` y se pushea antes de cerrar la historia, para que el pipeline corra y testing se despliegue con las correcciones antes de la sesión del dueño.
- Escena de combate: los inputs sintéticos se escriben a mano siguiendo la ruta de la sesión del dueño hasta el primer combate (el bot no llega a `BTL` y la navegación a ciegas falló); la grabación del dueño queda privada.
- Timeouts del CI: quedan subidos (estimación ×1,5); con la primera corrida se miden las duraciones reales y se ajustan. Si el runner los recorta, se le avisa al dueño para subir su `maximum_timeout`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Soak limpio | 4 semillas × 540.079 VBlanks, `native-headless-release` + `native-asan` | `report.json` sin fallos; digests iguales entre builds; overlays visitados listados | N/A |
| Fallo nuevo en soak | Caída, assert, sanitizer o cuelgue | Fila en `triage.md`, corrección y test de regresión | Si la causa no se encuentra, la fila queda como abierta con su reproducción y la historia no cierra |
| Divergencia entre builds | Digest distinto entre release y ASan o web | Se trata como fallo: primer intervalo divergente y causa | Igual que fallo nuevo |
| Sesión del dueño | Archivo descargado de la Shell de testing | Se reproduce en `native-headless` con el mismo digest final que la Shell | Si difiere, es un fallo de determinismo y se tría |

</frozen-after-approval>

## Code Map

- `.gitlab-ci.yml` -- `native-disc` (timeout 4 h), `native-asan-disc` (5 h), `web-disc` (1 h 30 min) y `testing` (2 h). El pipeline 164 cortó `native-disc` en `scene_title_audio_compare` (exit -15 = SIGTERM del timeout) y `web-disc` en `web_playtest`: el `spawnSync` de `port/web/tests/playtest.spec.cjs:25` (timeout de 900 s) mató la reproducción nativa en el VBlank 12.300.
- `port/psyq/crt.c:207-220` -- watchdog `no frame in 30 virtual seconds`, sale con 3.
- `build/port/verification-6-11/benchmark-soak/trial-0000/build-{0,1}/` (privado) -- reproducción del cuelgue en el VBlank 34.200: `reproduce.sh`, `inputs.txt`, `digest.txt`, `host.trace`. Overlays 7 (`MOV_REL`) y 11 (`SHOP_REL`); avisos `display disabled` y `frozen display`; mismo digest en Debug y `-O2`. Diagnóstico previo (2026-11-15, verificado salvo lo marcado): el último retorno de `VSync` es el VBlank 31.289 (última entrada del bot antes: `31280 0008`). El juego queda dentro de una sola llamada a `DrawOTag` desde `gameLoop` (`src/main/main.c:3999`, `GsDrawOt`), que recorre paquetes distintos (0x08, 0x09, 0x0c, `e1`) en unos 130 KB y no retorna; el reloj virtual avanza dentro del rasterizador (`port/backend/gpu_soft/gpu_soft.c`) hasta que salta el watchdog, antes del aviso de truncado de `port/psyq/libgpu.c:148-156` (tope de 0x100000 paquetes). Un frame normal recorre unos 3.482 paquetes. **Hipótesis:** la OT de la escena de la tienda queda cíclica o desmesurada; falta ver si el enlace roto nace en el juego o en `GsSortClear`/`GsClearOt`/`AddPrim`/`GsSort*` del port (o en el tamaño de `GS_ORDERING_TABLE`). Reproducción: `tools/port/docker_run.sh sh /dw/build/port/verification-6-11/benchmark-soak/trial-0000/build-1/reproduce.sh` (~85 s en `-O2`; `DW1_VSYNC_PROBE=1` imprime los retornos de VSync). No hay gdb en la imagen; los shims `LD_PRELOAD` de muestreo están en `build/port/triage-34200/` (privado).
- `docs/implementation-artifacts/deferred-work.md` (secciones «Historia 6.11: `web_scene_title_audio`…» y «…cuelgue en el VBlank 34.200») -- hallazgos de entrada; al cerrarlos, marcar `status: closed` con el commit.
- `web_scene_title_audio` -- el PCM del worklet difiere del nativo desde el byte 22.733.052 (frame estéreo 5.683.263) en Chromium y Firefox; `scene_title_audio_compare` nativo contra Redux pasa, así que la divergencia está del lado web (`port/host/web/`, worklet de audio, `port/web/tests/`).
- `tools/port/playtest/{run.py,bot.py,soak.sh}` -- runner, bot y soak de la 6.9; reutilizarlos sin cambiar su contrato. `run.py` deja `reproduce.sh` por fallo.
- `tools/port/scenes/*.toml`, `tools/port/scenes/run.py`, `CMakeLists.txt` (tests clase B `scene_*`) -- patrón para la Escena nueva `battle_first` (o similar) y para los tests de regresión del triage.
- `port/psyq/include/libetc.h` (`getScratchAddr` → `Ps1_Scratchpad`), test `scratchpad` -- corrección E4 a confirmar en combate, en `native-headless` y web.
- `docs/STATUS.md` -- al cerrar, listar los overlays alcanzados sin fallos y el estado del CI.

## Tasks & Acceptance

**Execution:**
- [ ] `.gitlab-ci.yml` -- medir la duración de cada job con disco en `runner-231` (o estimarla desde los logs de los pipelines 161-164) y subir los timeouts a ×1,5; subir el timeout de reproducción de `playtest.spec.cjs` con el mismo criterio -- `main` verde y testing desplegado con el build actual.
- [x] Cuelgue del VBlank 34.200 -- encontrar qué espera el juego y corregir la causa; agregar como test de regresión clase B los inputs del bot recortados (sintéticos) con su digest esperado.
- [x] `web_scene_title_audio` -- encontrar la causa de la divergencia del PCM web y corregirla; el test pasa en Chromium y Firefox.
- [ ] `tools/port/scenes/<combate>.toml`, `CMakeLists.txt` -- Escena de combate en `native-headless`, `native-asan` y web con el criterio de jugabilidad (sin caída, dos corridas iguales, mismo estado en nativo y web, captura revisada).
- [x] Soak -- correr `soak.sh` en `docker-server`; triar cada fallo según la matriz y repetir hasta un soak completo sin fallos.
- [x] `docs/port/triage.md` -- registro de fallos (formato de «Always»).
- [ ] Sesión del dueño -- con lo anterior verde, detenerse y pedirle al dueño que juegue en testing; reproducir su archivo en `native-headless` (mismo digest final que la Shell), triar sus fallos y sumar a la tabla de `triage.md`.
- [ ] `docs/port/playtest.md`, `docs/implementation-artifacts/deferred-work.md`, `docs/STATUS.md` -- resultados del soak, hallazgos cerrados y overlays alcanzados sin fallos.

**Acceptance Criteria:**
- Given un push a `main`, when corre el pipeline, then todos los jobs pasan sin reintentos y `testing` despliega ese commit.
- Given la reproducción del VBlank 34.200, when corre en `native-headless-release`, then pasa ese punto sin cuelgue y su test de regresión pasa.
- Given la Escena de combate, when corre en `native-headless`, `native-asan` y web, then termina sin caída ni acceso fuera de rango y da el mismo estado en los tres.
- Given un soak completo en `docker-server`, when termina, then su informe no tiene fallos y cada fallo de soaks anteriores tiene su fila cerrada en `triage.md`.

## Implementation Notes

- Implementación en subagente Sonnet (2026-11-15/16, ~5 h 40 min). Correcciones y causas en `docs/port/triage.md` (filas 1-4). La hipótesis del Code Map (OT cíclica) era falsa: era la pausa de `handlePause`.
- `make compare`: binario idéntico con los cambios de `src/` (corrido por el orquestador en `dw-build`). Objeto original de `tickHealingParticles` sin chequeo de cero (sin `break`/`teq`). Bytes de la entrada 62 de `EVO_CHART_BOXES` iguales a la primera entrada de `MAIN_D_8012472C`.
- Soak `20261116T002418Z` (antes de las correcciones): 7 de 8 corridas fallaron. Soak `20261116T020359Z` (después): 8 corridas, 0 fallos, 20 h virtuales, digests iguales; overlays 7, 10 y 11. Corrió en el contenedor de desarrollo porque `docker-server` resolvió al mismo host.
- Tests de regresión verificados contra el código sin corregir: `pause_34200` falla con salida 3 en el VBlank 34.200 y `healing_div0` con señal 8 en el 45.000; `evo_chart_oob` falla solo bajo ASan.
- Clase A `native-headless` (61 tests, `remote_run.sh`): verde. Web en serie: `web_shell`, `web_playtest`, `web_scene_title_audio`, `web_cards_real` (Chromium y Firefox) verdes.
- Pendiente para cerrar: pipeline verde y deploy de testing (se verifica tras el push), sesión del dueño, Escena de combate según su ruta y `docs/STATUS.md`.

## Spec Change Log

## Review Triage Log

Pasada 1 (Sonnet `sonnet-merged`, 2026-11-16). Hallazgos repetidos entre capas en una sola fila.

| # | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|
| 1 | `playtest_pause_34200` no falla sin la corrección (VG, ECH) | medium | Sin la corrección, el watchdog salta en el VBlank 34.200 (corrida original, determinista) y el test corta en 33.500: pasaría igual | patch |
| 2 | `index.html` solo activa la reproducción absoluta con `replay=1`; `cards_real.spec.cjs:18` usa `replay=${scene}` (BH, VG, ECH) | medium | Los `replay.txt` de Escena se escriben con VBlank absoluto (`--resolved-inputs`); `card_newgame`/`card_continue` reciben el origen del frame 0, el mismo desfase de la fila 2 de `triage.md`. Solo `playtest.spec.cjs` (`replay=playtest`) usa grabaciones desde el frame 0 | patch |
| 3 | `triage.md` da el borde en (-147, -68) y el arreglo tiene -146/-67; «No hay filas abiertas» sugiere cobertura amplia (BH, ECH) | low | 0xff6e = -146 y 0xffbd = -67; el soak solo visitó 7, 10 y 11 | patch |
| 4 | El job `native-disc` lista las regresiones por nombre (BH, ECH) | low | Una regresión nueva del `foreach` quedaría fuera del job release; corrección directa con `^playtest_` | patch |
| 5 | El watchdog no detecta un cuelgue que siga leyendo el pad (BH, VG, ECH) | low | Compromiso aceptado: una pausa real puede durar horas y un tope la mataría; el informe del playtest sigue marcando `frozen display`. El arreglo agrega estado | reject |
| 6 | Carrera en `pad_polled` en web (BH, ECH) | false | `clock_schedule` exige el hilo dueño del reloj (`check_thread`, `port/psyq/clock.c:125`): el watchdog corre en el hilo del Juego, el mismo que llama a `PadRead` | reject |
| 7 | Bytes de la entrada 62 de `EVO_CHART_BOXES` sin verificar (BH, ECH) | false | Coinciden con la primera entrada de `MAIN_D_8012472C` (`game_menu.c:933`), dato del build matching (`make compare` idéntico) | reject |
| 8 | El hit-test de `game_menu.c:1994` ve la entrada 62 (BH) | false | En la PS1 ese bucle lee los mismos 8 bytes; el port reproduce lo mismo | reject |
| 9 | `particle.c`: expresión duplicada y -1 solo para dividendo positivo (BH, ECH) | false | El dividendo es la constante 512; el objeto original no tiene chequeo de cero (sin `break`/`teq` en `tickHealingParticles`) | reject |
| 10 | Timeouts del CI por encima del máximo del runner o del proyecto (BH, ECH) | maybe-false | Se resuelve con la primera corrida del pipeline; el corte del 164 en 4 h coincide con el timeout del job, sin tope menor visible | defer (decisión del dueño, pregunta 3) |
| 11 | VBlanks de las regresiones en `CMakeLists.txt` y digest en otro archivo (BH, ECH) | low | Error de mantenimiento poco probable; el arreglo agrega formato | reject |
| 12 | `check_replay.py` sin chequeo de argumentos ni del informe (BH, ECH) | low | Solo lo llama CMake con 5 argumentos; un `KeyError` en CI ya señala el fallo | reject |
| 13 | `--replay-absolute` sin `--replay` se ignora en silencio (BH, ECH) | low | Solo lo pasa la Shell junto a `--replay` | reject |
| 14 | Columna «Commit» en `(sin commit)` (BH, ECH) | false | La completa el orquestador al commitear | reject |
| 15 | Sin `_Static_assert` del tamaño del arreglo; solo ASan detecta el OOB (VG, ECH) | low | El job `native-asan-disc` corre la suite completa en cada merge | reject |
| 16 | `fnptr_casts.tsv` regenerado sin chequeo (VG) | false | `fnptr_inventory` lo compara contra el código y pasa | reject |
| 17 | Falta la Escena de combate y la sesión del dueño (BH, ECH) | high | Tareas de la spec sin hacer: el bot no llega a `BTL` | intent_gap (preguntas al dueño) |
| 18 | Evidencia de `title_audio.spec.cjs` solo corre si el test falla (VG) | low | Es diagnóstico para cuando falla; sin daño | reject |

## Verification

**Commands:**
- `tools/port/docker_run.sh ctest --preset native-headless` y `tools/port/docker_run.sh ctest --preset native-asan` -- expected: verde.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/remote_run.sh ctest --preset native-headless -R 'scene_|playtest'` -- expected: verde, sin skips.
- `DW_IMAGE=dw-port:remote DW1_DISC_DIR=/dw/game_rom/extracted tools/port/docker_run.sh python3 -I -B port/web/tests/run.py title_audio` (serial) -- expected: PCM igual al nativo en Chromium y Firefox.
- `tools/port/playtest/soak.sh` -- expected: `report.json` sin fallos.
- Pipeline de `main` en GitLab -- expected: verde y deploy de testing con el commit.
- `tools/port/ci/preproc_check.sh` y `make compare` -- expected: binario PS1 idéntico.
