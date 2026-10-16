---
title: 'Historia 2.15: pantalla de título en nativo headless'
type: 'feature'
created: '2026-10-16'
baseline_commit: '62e7594dc7a4558cf84575e4f5a811690525ba47'
status: 'review'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** con el Pack real el Juego ya recorre `playMovie(0)` → `runLandingScreen` en bucle
(`MOV_REL.BIN` se recarga cada ~440 M ciclos), pero no hay Escena, runner, captura ni evidencia
de que llega al título; M2 no puede cerrarse (SM-1, parte nativa).

**Approach:** marca por símbolo en la entrada de `runLandingScreen` (bloque `DW1_PORT`), captura
del área de display en el VBlank k posterior a la primera marca, Escena `title.toml` y runner
`tools/port/scenes/run.py` que corre dos veces y compara hashes de captura y de traza del reloj.

## Boundaries & Constraints

**Always:** el bloque `DW1_PORT` en `src/main/main.c` es una sola llamada
`port_scene_mark("runLandingScreen")` (declaración en ámbito de bloque, sin headers nuevos en
`src/`), con fila en `docs/port/src-changes.md`; L0a y `make compare` siguen verdes. Captura y
trazas se escriben fuera de lo versionado (directorio del build, ignorado). El Host decide la
captura y la salida; `run.py` solo orquesta y verifica. `-Wall -Wextra -Werror` en los tres
presets. Los errores de ASan/UBSan del camino al título se corrigen en su origen (Capa PsyQ o
bloque `DW1_PORT` mínimo documentado).

**Never:** comparación contra el Emulador de referencia ni SHA esperado en el `.toml` (Épica 3);
input del host; Escenas además de `title`; ventana SDL; `port_yield` en `MOV_waitForFlip` (su
spin tiene timeout y el stub de 2.14 pone `flipPending` antes: no bloquea); validar el flag de
mapeo de todos los TMD del disco (queda diferido).

**Decisiones (autónomas):**
- Marca: `port_scene_mark(name)` escribe `mark <name>` en la traza y, si coincide con la marca
  pedida y es la primera, guarda `clock_vblanks()`.
- Captura: tras despachar los eventos del VBlank `mark + k` (hook del VBlank del reloj), se lee
  `gpu_read_display` y se escribe un PPM P6 (RGB888 expandido del RGB555, sin pérdida) y se
  termina con estado 0. Flags del Host: `--capture-mark NAME --capture-vblank K --capture FILE`.
- Traza del reloj: `clock_set_trace` escribe `<ciclo> <kind>` por evento (sin punteros) en un
  archivo pedido con `--clock-trace FILE`; `run.py` compara su SHA-256 entre corridas.
- `title.toml`: `id = "title"`, `overlay = "mov"`, `inputs = []`, `mark = "runLandingScreen"`,
  `capture_vblank = 60` (después del `fadeFromBlack(0x28)`).
- `run.py <id> --build <preset>`: binario `build/port/<preset>/dw1`, Pack `--pack` o
  `build/port/<preset>/us.pack` (si falta y hay `DW1_DISC_DIR`, lo construye con `dw1-pack`);
  corre dos veces, falla si: estado ≠ 0, falta `mark runLandingScreen`, aparece
  `AddressSanitizer|runtime error`, capturas o trazas difieren, o ≤ 16 colores. Imprime ruta,
  SHA-256 y colores. Sin `DW1_DISC_DIR` ni Pack: sale 77.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Título | Pack real, `native-headless` | `mark runLandingScreen` en la traza; PPM del display con > 16 colores; dos SHA-256 iguales de captura y de traza | N/A |
| ASan | Pack real, `native-asan` | igual, sin reportes ASan/UBSan | falla con el reporte |
| Marca nunca llega | Pack sintético (L1) | watchdog o `--frames` termina con estado ≠ 0 | `run.py` falla, sin captura |
| Display apagado | captura con `gpu_read_display` = 0 | Host sale con error, no escribe PPM | mensaje en stderr |
| Marca repetida | segunda vuelta del bucle película/título | solo cuenta la primera | N/A |

</frozen-after-approval>

## Code Map

- `src/main/main.c:3587` -- `runLandingScreen`: bloque `DW1_PORT` al inicio. Llamado en `main.c:3430`.
- `port/psyq/crt.c` -- `port_frame_done`, `port_overlay_done`, watchdog: agregar `port_scene_mark` y la captura; `port_run_game` recibe la config.
- `port/host/headless/main.c` -- parseo de flags nuevos (estilo actual de `usage`).
- `port/psyq/clock.c:66-75,125-140` -- `clock_set_vblank_handler`, `clock_set_trace` (`KIND_VBLANK`).
- `port/backend/gpu.h` -- `gpu_read_display` (hasta 640×480, 0 si display apagado).
- `port/psyq/port.h`, `stub.c` -- `port_trace` (formato `<ciclo> <texto>`).
- `tools/port/pack/test_dw1_overlay.sh` -- patrón clase B (skip 77, chequeo sanitizer).
- `CMakeLists.txt:291-296,353-366` -- `frame0` y tests clase B: agregar `scene_title` (clase B, `FIXTURES_REQUIRED disc_pack`) y un L1 sin disco de los flags (Pack sintético, marca ausente).
- `tools/port/metrics/isr_spins.tsv` -- 0 filas `pendiente`; no tocar.
- `docs/implementation-artifacts/deferred-work.md` -- `DRAWENV` sin inicializar de `MOV_playMovie`: si las dos corridas difieren, se corrige con `DW1_PORT`; si no, se anota que no afecta.

## Tasks & Acceptance

**Execution:**
- [x] `src/main/main.c` + `docs/port/src-changes.md` -- marca `DW1_PORT`.
- [x] `port/psyq/crt.c`, `crt.h`, `port/host/headless/main.c` -- marca, captura PPM, traza del reloj, flags.
- [x] `tools/port/scenes/title.toml`, `tools/port/scenes/run.py` -- Escena y runner (Python 3.12, `tomllib`).
- [x] `CMakeLists.txt` + test L1 de flags/captura sin disco y `scene_title` clase B.
- [x] `docs/STATUS.md` ("Escenas que funcionan (nativo): 1 (título, sin referencia)", M2 completo), `docs/implementation-artifacts/evidence/story-2-15.log` (salida de `run.py` en ambos presets, sin imágenes).

**Acceptance Criteria:**
- Given el Pack real, when `tools/port/scenes/run.py title --build native-headless`, then sale 0, la traza tiene `mark runLandingScreen`, el PPM queda en el build con > 16 colores y las dos corridas coinciden en SHA-256 de captura y de traza del reloj.
- Given lo mismo con `--build native-asan`, then sale 0 sin reportes ASan/UBSan.
- Given `native-headless` y `native-asan` sin `DW1_DISC_DIR`, when `ctest`, then todo verde (clase B en skip).
- Given L0a, `web` y `make compare`, then verdes.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && env -u DW1_DISC_DIR ctest --preset $p && DW1_DISC_DIR=/dw/game_rom/extracted python3 tools/port/scenes/run.py title --build $p; done'` -- expected: verde
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web'` -- expected: exit 0
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK
- `make compare` en la imagen `dw-build` (`docs/port/setup.md`, paso 7) -- expected: `Compiled binaries match`

## Implementation Notes

- Implementado el hook de observación posterior a los callbacks de VBlank y a los eventos
  pendientes de ese ciclo, sin reemplazar el handler de `libetc`. La observación persiste
  si un callback avanza el reloj más allá del objetivo de la llamada actual.
- El Host captura RGB888 mediante `gpu_read_display`, escribe P6 y sale; los límites de
  frames/overlay salen con error cuando hay una captura pendiente. El runner ejecuta dos
  procesos, conserva logs en el build y verifica marca, sanitizers, colores y ambos hashes.
- L1: CLI real sin disco, Juego sintético sobre el Pack L1 con marca ausente, primera marca,
  eventos diferidos por un callback, pixel RGB de P6, display apagado y errores del runner.
- Verificación dentro del contenedor actual (sin Docker anidado): builds nativos y web,
  CTest sin disco y `scene_title` con disco; L0a y `make compare`. El driver GCC MIPS se
  extrajo a `/tmp` para generar dependencias/enlazar el build matching; la compilación del
  Juego siguió usando CodeWarrior R4. No se modificaron Makefiles ni se crearon commits.
- Captura de título: 320×240, 256 colores; SHA-256 idéntico dentro de cada preset y entre
  presets. Las trazas de reloj coinciden entre las dos corridas de cada preset; no se exige
  igualdad entre builds. `DRAWENV` sin inicializar no afecta la captura observada y queda
  registrado en `deferred-work.md` para video real. Sin comparación contra emulador.
- Evidencia textual: `docs/implementation-artifacts/evidence/story-2-15.log`. Capturas y
  trazas permanecen en `build/port/<preset>/scenes/title/`, ignorado por Git.

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `run.py` no comprueba dimensiones 320×240 (blind, edge) | low | Real, pero el AC solo pide > 16 colores; una regresión de resolución cambia el SHA entre versiones y la paridad llega en 3.1. Arreglo = campo nuevo en el `.toml`. Rechazado. |
| 2 | Sin comprobación del contenido del título (blind) | false | Sin referencia no hay contenido esperado: es la Épica 3 (3.1, SHA en `title.toml`). La captura se inspeccionó a mano: logo, "DIGIMON WORLD", "©BANDAI 1997-2000". |
| 3 | `scene_title` reconstruye `build/port/<nombre>` desde el nombre del build dir (blind, edge) | low | Los presets fijan `binaryDir` en `build/port/<preset>`; otro directorio es improbable. Arreglo = parámetro nuevo. Rechazado. |
| 4 | `--pack` explícito inexistente da 77 o crea un Pack en esa ruta (blind, edge) | low | Real (`run.py:34-44`) y fácil de pisar con un typo; arreglo directo de una línea. **patch**. |
| 5 | Captura y traza en el mismo archivo se corrompen (blind, edge) | low | Solo con flags elegidos a mano iguales; `run.py` usa nombres distintos. Arreglo = guarda nueva. Rechazado. |
| 6 | Timeout de `run.py` pierde el log y deja uno viejo (blind, edge) | low | `TimeoutExpired` llega como error con mensaje; el log viejo no se usa para nada. Arreglo = manejo nuevo. Rechazado. |
| 7 | Construcción del Pack sin timeout (edge) | low | `dw1-pack` lee un archivo local; bloqueo improbable. Rechazado. |
| 8 | Dos runners simultáneos pisan sus archivos (edge) | low | CTest corre `scene_title` una vez por preset y cada preset tiene su directorio. Rechazado. |
| 9 | Un callback que cruza otro VBlank aplaza la captura (edge, claim) | maybe-false | Se resolvería probando si algún callback de VSync del Juego avanza el reloj más de un VBlank; aun así la captura usa `>=` y sigue siendo determinista: como mucho `low`. Rechazado. |
| 10 | Intervalo relativo solo probado con marca en VBlank 0 (verification gap) | medium | Pre-verificado (gap). **patch**: marca inicial en VBlank 5, captura en 7. |
| 11 | `--until-overlay` con captura pendiente sin test (verification gap) | medium | Pre-verificado (gap). **patch**: modo del probe con `port_overlay_done`. |
| 12 | Fallos de escritura/cierre de captura y traza sin test (verification gap) | medium | Pre-verificado (gap). **patch**: directorio inexistente y `--clock-trace /dev/full`. |

Verificación tras los parches: CTest 38/38 (`native-headless`) y 36/36 (`native-asan`) sin disco; `run.py title` OK en ambos presets (captura `14d5bb08…`, 256 colores); web OK; L0a `differ=0`, `isr_spins pending=0`. Observación diferida: la traza del reloj difiere entre presets (`deferred-work.md`).
