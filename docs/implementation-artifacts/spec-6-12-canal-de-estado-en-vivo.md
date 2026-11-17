---
title: 'Historia 6.12: Canal de estado en vivo para el playtest'
type: 'feature'
created: '2026-11-17'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
baseline_commit: '2fee88c64d18446288fd4a6187000572172132c3'
context:
  - '{project-root}/docs/implementation-artifacts/epic-6-context.md'
  - '{project-root}/docs/port/playtest.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El soak da 0 fallos pero el bot es ciego: genera inputs al azar desde una semilla, sin leer el juego, y nunca sale de File City (overlays MOV, TRN y SHOP; nunca BTL). Para que un bot con objetivos (historia 6.16) y el diagnóstico al morir (6.13) existan, el binario tiene que exponer por VBlank el estado observable y aceptar la máscara de pad desde afuera, sin depender de `--capture-mark`.

**Approach:** Dos flags del host nativo. `--state-fd N` escribe un registro de texto por VBlank con el estado observable (modo observación, los inputs siguen viniendo de `--inputs`). `--state-fd N --input-fd M` agrega lockstep: el binario espera del bot una máscara y un `hold` (cantidad de VBlanks), y la aplica en `latch()` de `port/psyq/scene.c`. La misma secuencia de inputs da el mismo digest con o sin canal. Se documenta el significado de `GAME_STATE` y `MAIN_STATE` verificado en `src/`.

Decisiones del dueño (2026-11-17, plan aprobado en la sesión):
- Cambia la decisión técnica de la épica «el bot no modifica el motor: genera archivos de inputs»: el bot puede leer estado y responder en vivo por el canal. El juego no cambia; toda la instrumentación va en `port/` y `tools/`, nunca en `src/`.
- Protocolo de texto con encabezado versionado y respuesta `<máscara> <hold>`, descrito en Design Notes.
- La invocación del soak del homelab (`run.py --soak --build native-headless-release --build native-asan --prefix tools/port/scenes/newgame.toml --vblanks 540079 --jobs 14 --seed N...`) sigue funcionando sin cambios. Esta historia no cambia la CLI de `run.py`.

## Boundaries & Constraints

**Always:**
- El registro solo lee globales del juego. No llama funciones del juego, no consume `rand()`, no toca VRAM ni el reloj virtual.
- La máscara del canal entra por `port_pad_latch`, en el mismo punto y VBlank en que entra hoy la de `--inputs`. `--record-inputs` graba lo resuelto y su replay con `--inputs --inputs-from frame0` reproduce el mismo digest.
- El índice `k` del registro es el VBlank desde el frame 0 (`port_playtest_vblank()`), el mismo que usan `--record-inputs` e `--inputs-from frame0`.
- Ningún digest existente cambia. Sin los flags nuevos, el comportamiento es idéntico.
- `-Wall -Wextra -Werror` en el código del port. Tests L1 sin disco; tests clase B con `DW1_DISC_DIR` y salida 77 sin él.
- Nada con bytes del disco en el repo, en logs públicos ni en artifacts de CI.

**Never:**
- Cambiar `src/` o el comportamiento del juego para ayudar al bot.
- Implementar el bot con objetivos (6.16), el volcado al morir (6.13), `--seed` o `--digest-sparse` (6.14), ni tocar `run.py`.
- Soportar el canal en web (Emscripten). El canal es solo del host nativo.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Lockstep | `--state-fd 3 --input-fd 4`, el bot responde `4000 10` | Encabezado, un registro por VBlank desde `k=0`; `want=1` en `k=0`, después `want=0` durante 9 VBlanks y `want=1` en `k=10` | N/A |
| Observación | `--state-fd 3 --inputs F --inputs-from frame0` | Un registro por VBlank con `want=0`; mismo digest que sin `--state-fd` | N/A |
| Fin del bot | El bot cierra el fd de input | Parada limpia en ese VBlank: `port: input channel closed at VBlank k`, digest final escrito, salida 0 | N/A |
| Respuesta inválida | `zz 1`, `10000 1`, `4000 0`, `4000` o línea vacía | Sin aplicar | `scene: invalid input channel reply`, salida 2 |
| Lector cerrado | El fd de estado se cierra (EPIPE) | Igual que fin del bot | SIGPIPE ignorado; nunca muere por señal |
| Flags inválidos | `--input-fd` sin `--state-fd`, con `--inputs` o con `--checkpoints`; fd no numérico o negativo; fd que no está abierto | No arranca | Uso, salida 2 |

</frozen-after-approval>

## Code Map

- `port/psyq/scene.c:359-386` -- `latch()`. Hoy corre en `port_scene_origin` (frame 0) y en cada `port_scene_tick`. Rama nueva para el canal antes de `if (!input) return;`. Puede llamarse dos veces en el mismo VBlank: el intercambio va una sola vez por `k`. `read_event()` (:321-357) es el modelo de validación estricta de máscara hex.
- `port/psyq/scene.c:40-52` -- `port_scene_resolved_input`: el grabador, sin cambios.
- `port/psyq/libetc.c:56-68` -- `port_pad_latch`: único punto de inyección, sin cambios.
- `port/psyq/crt.c:91-95` y `:167-172` -- con `--inputs` no se lee el input del host. Agregar la misma exclusión cuando hay `--input-fd`.
- `port/psyq/crt.c:96-104` -- la parada por `stop_requested` escribe el digest final y sale 0. El fin del canal usa `port_request_stop()` (:50) y su mensaje propio.
- `port/psyq/crt.c:47` -- `frames` es estático. Agregar un accesor `long port_frame_count(void)` en `crt.h`.
- `port/psyq/playtest.c:14`, `:57-61` -- `overlay` es estático y se actualiza siempre. Agregar `unsigned port_playtest_overlay_id(void)` en `playtest.h`.
- `port/psyq/crt.h` -- `PortCapture`: agregar `int state_fd, input_fd` con -1 como «sin canal». Inicializar a -1 en `native_cli.c` y en cualquier otro constructor de `PortCapture` (tests incluidos).
- `port/host/native_cli.c:25-43`, `:44-140` -- uso y parseo. Mismo estilo que `--vblanks` para validar números.
- `port/psyq/state.c` (nuevo) -- arma el encabezado y el registro. Incluye los headers del juego (`<dw/entity.h>`, `<dw/params.h>`, `<dw/script.h>`, `<dw/btl.h>`, `<dw/clock.h>`) con el mismo esquema que `test_av_stubs` (`CMakeLists.txt:629-635`: include `include`, `-idirafter external/psyq_headers/mw_lib41/include`, más las definiciones que pidan los headers). Cada global con `#pragma weak`, para que `test_playtest_runtime` y `test_watchdog` linkeen sin el juego. Un global ausente (NULL) da 0 en su campo.
- Globales del registro: `CURRENT_SCREEN_ID` u16, `GAME_STATE` s8, `MAIN_STATE` s8, `IS_SCRIPT_PAUSED` s32, `CURRENT_MENU` s32, `BTL_COMMAND_MENU_ACTIVE` s8, `TAMER_ENTITY.entity.posData->location`, `PARTNER_ENTITY.digimonEntity.entity.posData->location` (posData NULL da 0 0 0), `PARTNER_ENTITY.digimonEntity.stats.current.currentHP/currentMP`, `PARTNER_PARA.condition` u32, `MONEY` s32, `YEAR` u8, `DAY`/`HOUR`/`MINUTE` s16, `PLAYTIME_HOURS/MINUTES/FRAMES` u16.
- `CMakeLists.txt:439-450` -- `dw1_runtime`: sumar `state.c`. `:454-463` -- tests L1 `playtest_digest` y `playtest_unit`. `:464-482` -- tests clase B de playtest, solo con host headless. Registrar ahí `playtest_channel` (`SKIP_RETURN_CODE 77`, `FIXTURES_REQUIRED disc_pack`, timeout 3600). El job `native-disc` ya corre `^playtest_` en release y `native-asan-disc` corre la suite completa, así que no hace falta tocar `.gitlab-ci.yml`.
- `port/psyq/tests/test_playtest_runtime.c` -- juego sintético sobre `dw1_runtime`, usado por `test_playtest.py:264-277`. Extenderlo con modos de canal (fds por argumento) y que defina algunos globales del registro con valores que cambian por VBlank.
- `tools/port/playtest/test_playtest.py` -- tests L1. Usar `os.pipe()` + `pass_fds`.
- `tools/port/playtest/channel.py` (nuevo) -- cliente mínimo: lanza `dw1` con los dos pipes, parsea encabezado y registros por nombre de campo, y responde con un callback. Lo reutilizan el test clase B y la historia 6.16.
- `tools/port/playtest/bot.py:36-74` -- `resolve_prefix` resuelve `newgame.toml` a eventos frame 0. El test clase B lo usa tal cual.
- `docs/port/playtest.md` -- secciones nuevas «Estado observable» (tabla de `GAME_STATE`/`MAIN_STATE`) y «Canal de estado».

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/crt.h`, `port/psyq/crt.c`, `port/psyq/playtest.h`, `port/psyq/playtest.c` -- campos `state_fd`/`input_fd`, accesores de frames y overlay, exclusión del input del host con canal.
- [x] `port/psyq/state.c`, `CMakeLists.txt` -- encabezado y registro con referencias weak; sumarlo a `dw1_runtime` con los includes de los headers del juego.
- [x] `port/psyq/scene.c` -- canal en `latch()`: un intercambio por `k`, `hold`, EOF y EPIPE como parada limpia, validación estricta de la respuesta, `SIGPIPE` ignorado cuando hay canal.
- [x] `port/host/native_cli.c` -- `--state-fd`, `--input-fd`, uso y validaciones de la matriz (incluye `fcntl(fd, F_GETFD)` para fds cerrados).
- [x] `port/psyq/tests/test_playtest_runtime.c`, `tools/port/playtest/test_playtest.py` -- L1: todas las filas de la matriz sobre el runtime sintético, y flags inválidos sobre `dw1` real sin disco.
- [x] `tools/port/playtest/channel.py` -- cliente reutilizable.
- [x] `tools/port/playtest/test_channel.py`, `CMakeLists.txt` -- clase B `playtest_channel`.
- [x] `docs/port/playtest.md` -- estado observable, `GAME_STATE`/`MAIN_STATE` y protocolo del canal.

**Acceptance Criteria:**
- Given el runtime sintético, when una corrida en lockstep graba con `--record-inputs` y después se reproduce esa grabación con `--inputs` sin canal y con `--state-fd` solo, then los tres digests y las dos trazas de reloj son idénticos, y los registros de la corrida en lockstep y de la de observación coinciden salvo `want`.
- Given el disco, when `playtest_channel` corre el prefijo `newgame` más 3000 VBlanks de máscaras con semilla por el canal en `native-headless-release` y en `native-asan`, then el replay sin canal da el mismo digest final, `screen` toma al menos un valor distinto de 0 y la posición del tamer no es 0 0 0 en algún registro después del prefijo.
- Given cualquier preset, when corren `playtest_unit`, `playtest_digest`, `playtest_smoke` y los `playtest_<regresión>`, then pasan con los mismos digests que en `main`.
- Given `docs/port/playtest.md`, when se lee, then documenta cada valor de `GAME_STATE` y `MAIN_STATE` con su cita a `src/`, el formato del registro y la respuesta, y marca 2 y 3 de `GAME_STATE` como hipótesis hasta verlas en una captura.

## Implementation Notes

- `state.c` vive en `dw1_runtime`, que ahora compila con `include`, `-idirafter mw_lib41/include` y `VERSION_US`. Cada global es `#pragma weak`; un global ausente da 0.
- `--state-fd` y `--input-fd` rechazan además el mismo fd para los dos y la repetición del flag (no estaba en la matriz).
- El canal no hace latch antes del frame 0, igual que `--inputs-from frame0` (corrección del orquestador después del informe del implementador).
- Desvío conocido: si el canal se cierra en `k=0` (en `port_vsync_return`, fuera del observador de VBlank), la parada efectiva ocurre en `k=1`. En cualquier otro `k` es en ese mismo VBlank.
- Implementador: clase B `pack_disc` + `playtest_channel` en `native-headless-release` (396 s) y `native-asan` (891 s); `playtest_smoke` y las tres regresiones con los digests de `main`.
- Verificación del orquestador sobre el árbol con parches (remoto): builds `native-headless`, `native-asan`, `native`, `web`, `native-headless-release` verdes; L1 sin disco verde salvo 5 `scene_*_compare` que fallan con `incomplete audio reference manifest` (manifest parcial que quedó en el build remoto de corridas viejas; esta historia no toca `tools/port/scenes`); clase B release `^(pack_disc|playtest_)` 8/8 (`playtest_channel` 498 s, `playtest_smoke` 657 s, regresiones con sus digests) y ASan `pack_disc|playtest_channel` 2/2 (1053 s).

## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|---|
| 1 | blind-hunter, edge-case | `--state-fd`/`--input-fd` como último argumento desreferencia NULL | false | El parseo está dentro de `if (i + 1 < argc)` en `native_cli.c`; sin valor cae en `usage()`. | rechazado |
| 2 | blind-hunter, edge-case | Se aceptan los fds 0, 1 y 2 | low | Real, pero el fd lo elige el bot que abre los pipes; nadie lo usa así. | rechazado |
| 3 | blind-hunter, edge-case | `channel.run` no tiene timeout: un binario colgado bloquea `readline` | low | En `playtest_channel` lo corta el `TIMEOUT 3600` de ctest. La detección de cuelgue con canal es del runner de la 6.16 y queda en su spec. | rechazado |
| 4 | blind-hunter, edge-case | `channel.py` pierde stderr y código si el binario muere antes del encabezado; `BrokenPipeError` al responder | low | Real: `parse_header('')` lanza `ValueError` sin diagnóstico. | patch |
| 5 | blind-hunter, edge-case | Respuesta de más de 255 bytes desincroniza; última línea sin `\n` aceptada | low | Una respuesta válida tiene menos de 13 bytes; una sobrelarga termina en `exit 2` en la línea siguiente. | rechazado |
| 6 | blind-hunter | `GET(&sym)` podría disparar `-Waddress`; `extern` redundante; `posData` colgante | false | Compila con Clang 18 y `-Werror` en todos los presets. `MAIN_STATE` y `CURRENT_MENU` no están en `include/`, así que el `extern` es necesario. El juego desreferencia el mismo `posData` en cada frame. | rechazado |
| 7 | blind-hunter | Comentario en español en código («Registro globals») | low | Real, contra la regla de idiomas. | patch |
| 8 | blind-hunter, verification-gap, edge-case | include, `-idirafter` y `VERSION_US` para todo `dw1_runtime` cambian las entradas de crt/playtest/scene (nativo y web) | low | Real: solo `state.c` los necesita. | patch |
| 9 | blind-hunter, edge-case | La doc no dice que un bot que no responde frena el juego | low | Real; una oración. | patch |
| 10 | blind-hunter, edge-case | `test_channel.py`: `KeyError` o `IndexError` sin diagnóstico | low | Solo cambia el mensaje de un test que igual falla. | rechazado |
| 11 | verification-gap | 22 de 27 campos del registro sin verificar su cableado | medium | Las aserciones L1 actuales pasan aunque se intercambien `hour`/`minute` o `hp`/`mp`. | patch |
| 12 | verification-gap | El test de lector cerrado no distingue parada limpia de llegar al límite | low | Solo verifica rc 0 con `vblank_limit=10`. | patch |
| 13 | verification-gap | Los flags válidos no pasan por el host en L1 | low | Solo cubiertos por la clase B. | patch |
| 14 | edge-case | Si falla el `fflush` del encabezado se para sin mensaje | low | Real; `channel_close(0)` lo unifica. | patch |
| 15 | edge-case | Con el canal muerto, la máscara vieja se aplica un VBlank más | low | La parada se toma en el mismo `capture_vblank`; solo en `k=0` hay un VBlank más, ya anotado. | rechazado |
| 16 | edge-case | Con `--vblanks`, el último registro `want=1` espera una respuesta | false | El bot siempre responde a `want=1`; el binario no se frena si el bot sigue el protocolo. | rechazado |
| 17 | edge-case (claim) | «Parada en ese VBlank» no se cumple | false | `port_request_stop` se consulta en el mismo `capture_vblank` después de `port_scene_tick`; el único desvío es `k=0`, anotado. | rechazado |
| 18 | edge-case (claim) | Web y los otros TUs cambian sus entradas de compilación | low | Igual que el 8. | patch (con el 8) |

## Design Notes

Protocolo (texto ASCII, `\n`, un registro por VBlank desde el frame 0):

```
# dw1-state 1 k overlay screen gs ms paused menu btl_menu tx ty tz px py pz hp mp cond money year day hour minute play_h play_m play_f frame want
0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0x00000000 0 0 0 0 0 0 0 0 1 1
```

- Los campos van en decimal con signo según el tipo del global, salvo `cond` (hex `0x%08x`). `frame` es `port_frame_count()` y `overlay`, `port_playtest_overlay_id()`.
- El bot parsea por nombre del encabezado. Los campos nuevos se agregan al final y el número de versión sube solo si cambia un significado.
- Respuesta (solo cuando `want=1`): `<máscara hex 1-4 dígitos, ≤ffff> <hold decimal 1..1000000>`. La máscara usa el orden wire de `--inputs`. Se aplica en `k` y se mantiene durante `hold` VBlanks; el próximo `want=1` llega en `k+hold`.
- Escribir un registro por VBlank con `fflush`: la contrapresión del pipe solo frena la pared, nunca cambia el estado.

`GAME_STATE` (`include/dw/btl.h:112`), verificado en `src/`:
- 0 overworld (`tamer.c:369`, `:860`; `std_setup.c:473`).
- 3 carga de datos de combate (`btl.c:409`).
- 2 combate no interactivo (`btl.c:521`; `battle_ui.c:341`; `battle_main.c:773`).
- 1 combate interactivo (`tamer.c:849`; `battle_setup.c:332`; `command_menu.c:201-235`).
- 4 pelea de torneo (`std_main.c:4040`).
- 5 torneo fuera de la pelea (`std_setup.c:263`, `:418`, `:593`; `std_main.c:3957`, `:4536`; `std_hud.c:1381`).
- `NPCEntityTick` (`overworld.c:1143-1157`) lo confirma: 0 campo, 1–3 combate, 4–5 torneo.

`MAIN_STATE` (`main.c:3222`):
- 0 partida nueva (`main.c:3467`).
- 1 continuar un save de la máquina de guardado (`main.c:3483`, `main_menu.c:5277`, `partner.c:362`).
- 2 continuar después del final (`main.c:3502`, `main_menu.c:5322`).
- 3 fin del juego, sale de `gameLoop` (`main.c:3550`; lo asigna el script en `script_interp.c:554`).
- Al cargar un save se restaura desde el archivo (`main_menu.c:5059`).

## Verification

**Commands:**
- `tools/port/docker_run.sh sh -c 'cmake --preset native-headless && cmake --build --preset native-headless && ctest --preset native-headless -R "^(playtest_digest|playtest_unit|frame0|watchdog)"'` -- expected: pasan sin disco.
- `tools/port/remote_run.sh sh -c 'for p in native-headless-release native-asan; do cmake --preset $p && cmake --build --preset $p && DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset $p --output-on-failure -R "^(pack_disc|playtest_)" || exit 1; done'` -- expected: `playtest_channel`, `playtest_smoke` y las regresiones pasan con los digests de `main`.
- `ctest --preset native-headless` y `--preset native-asan` completos (L1) en `remote_run.sh` -- expected: sin regresiones.
