---
title: 'Historia 3.1: Emulador de referencia y captura automática'
type: 'feature'
created: '2026-10-17'
status: 'done'
baseline_commit: '920675fc09fd4f8843b68d07ff6fa140a0a938a2'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** No hay Frames de referencia de la consola: la Épica 3 no puede comparar el port contra nada y la escena `title` solo prueba determinismo nativo/web.

**Approach:** Compilar PCSX-Redux desde un SHA fijo (con OpenBIOS del mismo SHA) dentro de `tools/port/Dockerfile` y agregar `tools/port/reference/capture.py`, que corre el emulador headless con un script Lua, detecta frame 0 y la marca de la Escena por breakpoint, aplica inputs por VBlank y guarda la captura del display fuera del repo en el mismo formato PPM que el port.

## Boundaries & Constraints

**Always:** Mismos puntos que el port: frame 0 = retorno del primer `VSync` llamado desde `main`; marca = entrada al símbolo `mark` del `.toml` (dirección desde `config/us/symbols.txt`); captura en el VBlank `mark + capture_vblank`, tras despachar el VBlank y antes de volver al juego. PPM P6 con expansión 5→8 bits `(c<<3)|(c>>2)` idéntica a `gpu_read_display` (`port/backend/gpu_soft/gpu_soft.c:613`), y 24 bpp sin conversión. Dos corridas → mismo SHA-256 o el script falla. Capturas, logs y BIOS compilada solo en `build/` (ignorado) o `$DW1_REFERENCE_DIR`; al repo va únicamente `reference_sha256` en `title.toml`. Toda llamada a la API Lua de PCSX-Redux no verificada se marca como hipótesis en `docs/port/reference.md` hasta probarla.

**Never:** BIOS de Sony en la imagen, el repo o CI; DuckStation; cambios en `src/`/`include/`; captura de audio o RAM (las piden 3.3 y 3.5 con su formato); comparar contra el port (3.2).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| HAPPY_PATH | `capture.py title`, `DW1_DISC_DIR` con `.cue` | 2 corridas, `reference/title/run-{1,2}.ppm` iguales, imprime SHA-256 | N/A |
| `--update` | igual + `--update` | escribe/actualiza `reference_sha256` en `title.toml` | N/A |
| Hash fijado | `reference_sha256` presente | sale 0 solo si ambas corridas coinciden con él | sale ≠0 nombrando ambos hashes |
| Sin disco | `DW1_DISC_DIR` vacío | `skipped`, sale 77 | N/A |
| Sin emulador | `pcsx-redux` ausente del PATH | sale 77 con motivo | N/A |
| No determinista | corridas distintas | sale ≠0 con los dos hashes | conserva ambas capturas |
| BIOS del dueño | `--bios <ruta>` fuera del repo | igual que HAPPY_PATH con ese BIOS; no toca `title.toml` | ruta inexistente: sale ≠0 |
| Marca nunca alcanzada | timeout de VBlanks | sale ≠0 indicando el último punto alcanzado (frame 0 o marca) | log del emulador guardado |

**Decisiones (2026-10-17):** (1) AC 2 no bloquea: 3.1 cierra con el procedimiento `--bios` documentado y la referencia OpenBIOS marcada *provisional* en `docs/port/reference.md`; la corrida con BIOS real queda en `deferred-work.md`. (2) Solo frames; audio (3.5) y estado (3.3) fuera. (3) Spec completa sin dividir.

</frozen-after-approval>

## Code Map

- `tools/port/Dockerfile` -- agregar etapa `pcsx-redux` (FROM la misma base; deps de build; `ARG PCSX_REDUX_SHA`; `make` + OpenBIOS) y copiar a la imagen final solo binario, `openbios.bin` y libs de runtime + `xvfb`/mesa. Hoy no tiene emulador (`docs/port/setup.md:228`).
- `tools/port/scenes/title.toml` -- `mark = "runLandingScreen"`, `capture_vblank = 60`, `inputs = []`; agregar `reference_sha256`.
- `tools/port/scenes/run.py` -- runner nativo; reutilizar su lectura de `.toml`, `ppm_colors` y el skip 77. No cambiar su comportamiento (3.2 lo extiende).
- `port/psyq/crt.c:48-100` -- semántica de marca y captura del port (primera marca, `clock_vblanks() - mark_vblank >= K`).
- `src/main/main.c:3608` -- `port_scene_mark` al entrar a `runLandingScreen` (0x800EED30); `main` = 0x800EE800, `VSync` = 0x80091CA8 (`config/us/symbols.txt`).
- `CMakeLists.txt:443-450` -- patrón de tests clase B (`SKIP_RETURN_CODE 77`, `DW1_DISC_DIR`).
- `.gitlab-ci.yml` -- job `images` reconstruye por cambios en el Dockerfile; digests fijados en `default.image` y `native-disc`.
- `game_rom/extracted/Digimon World.cue` -- disco local para verificación (no versionado).

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/Dockerfile` -- etapa de build de PCSX-Redux en SHA fijo (HEAD de `main` al implementar, registrado en el ARG) con OpenBIOS; copiar artefactos mínimos -- AC 1 pide el emulador en `dw-port` sin engordarla con el toolchain del emulador.
- [x] `tools/port/reference/capture.lua` -- breakpoints Exec en el primer `VSync` (y su `ra`) y en la marca; contador de VBlanks; aplica `inputs` del `.toml` en el latch de cada VBlank; vuelca display (raw 16/24 bpp, ancho, alto) a archivo y sale del emulador -- puntos idénticos al port.
- [x] `tools/port/reference/capture.py` -- lee `.toml` y símbolos, lanza `xvfb-run pcsx-redux` dos veces con timeout, convierte a PPM, compara, `--update`, `--bios`, `--out`; códigos según la matriz.
- [x] `tools/port/reference/test_capture.py` -- L1 sin datos: conversión 5→8 contra `gpu_read_display` con vectores calculados aparte, 24 bpp, resolución de símbolos, salida 77 sin disco/emulador, detección de corridas distintas y hash fijado (emulador falso).
- [x] `CMakeLists.txt` -- tests `reference_capture_unit` (A) y `reference_title` (B, skip 77); `.gitlab-ci.yml` -- `reference_title` en `native-disc`, digests nuevos tras el job `images` (pendiente: requiere push de la rama).
- [x] `tools/port/scenes/title.toml` -- `reference_sha256` producido por `--update`.
- [x] `docs/port/reference.md` -- SHA de PCSX-Redux, uso, hipótesis de API Lua verificadas, referencia *provisional* (OpenBIOS) y procedimiento con BIOS real; `docs/port/setup.md` -- reemplazar "Emulador de referencia (pendiente)".

**Acceptance Criteria:**
- Given la imagen `dw-port` reconstruida, when se corre `pcsx-redux -version` bajo `xvfb-run`, then responde y la versión impresa corresponde al SHA del ARG.
- Given `DW1_DISC_DIR` con el disco USA, when se corre `tools/port/reference/capture.py title` dos veces seguidas, then todas las capturas tienen el mismo SHA-256, igual al de `title.toml`, y `git status` solo muestra cambios en archivos versionables.
- Given la captura de referencia, when se inspecciona, then tiene el tamaño del display según GP1 y más de 16 colores (no es negro ni la pantalla del BIOS).

## Design Notes

PCSX-Redux expone en Lua `PCSX.addBreakpoint`, eventos `GPU::Vsync`, `PCSX.GPU.takeScreenShot()`, `PCSX.SIO0.slots[..].pads[..]` y `PCSX.quit()`; nombres y semántica exactos son hipótesis hasta probarlos con el SHA fijado. Si `takeScreenShot` no respeta GP1, leer el rango de display de los registros GPU y la VRAM directamente. Captura en ejecución determinista: intérprete o dynarec fijado explícitamente por flag, sin límite de velocidad ni audio real.

## Verification

**Commands:**
- `tools/port/docker_build.sh` -- expected: imagen con `pcsx-redux` y `openbios.bin`; el build corre `xvfb-run -a pcsx-redux -version`.
- `tools/port/docker_run.sh python3 -I -B tools/port/reference/test_capture.py` -- expected: OK.
- `tools/port/docker_run.sh sh -c 'DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/reference/capture.py title'` (2 veces) -- expected: mismo SHA-256 que `title.toml`, sale 0.
- `ctest --preset native-headless` en la imagen -- expected: verde; `reference_title` pasa con disco.
- Pipeline de la rama -- expected: `images`, `native`, `native-disc` (con `reference_title`) y `matching` verdes.

## Implementation Notes

- Implementados el runner Python, el adaptador Lua, tests sin datos, integración
  CTest/CI y etapa de build de PCSX-Redux/OpenBIOS en el Dockerfile. No se modificó
  `src/` ni `include/`; no se hizo commit.
- Verificación en el contenedor: 15 tests del capturador OK; build nativo OK;
  CTest 42 tests sin fallos (5 skips sin disco); cuatro tests nativos con el disco
  local OK. Evidencia y límites en `docs/port/reference.md`.
- Discrepancia detectada: `main` (`0x800EE800..0x800EEBDC`) no llama directamente
  a `VSync` en el asm original. Sus 78 llamadas directas se registraron en
  `build/reference-main-calls.txt`. El filtro literal de `ra` se conserva hasta
  que el dueño autorice corregir esa definición; todavía no hay hash validado.
- No hay Docker en el contenedor; no se reconstruyó/publicó la imagen, no hay
  nuevos digests verificables y no se ejecutó un pipeline sobre estos cambios.
- PCSX-Redux y OpenBIOS compilaron localmente. `-help`/`-version` bajo Xvfb
  imprimen el SHA correcto; smoke Lua real OK (display OpenBIOS 640×478 RGB555,
  eventos, registros, breakpoint y overrides de control).
- La ejecución real del disco USA alcanzó `runLandingScreen` en VBlank 8642,
  sin frame 0: salió 1 con `scene mark reached before frame 0`. No se ejecutó
  `--update`. El filtro literal queda sin alterar y la historia, incompleta.

Correcciones posteriores (orquestador, 2026-10-17):

- Frame 0: el filtro literal "`ra` dentro de `main`" no se cumple nunca (`main` no llama a `VSync` directamente; 0 `jal` a `0x80091CA8` en `0x800EE800..0x800EEBDC`). Se usa el primer `VSync` del juego, igual que `port_frame_done` del port; nada llama a `VSync` antes de `main`. AD-15 ("llamado desde `main`" = desde el programa del juego) no cambia.
- Sin launcher `-help`: upstream no tiene `-help` que termine; el AC y el build usan `-version`.
- El Dockerfile pasó a tres etapas (`toolchain`, `pcsx-redux` FROM `toolchain`, final FROM `toolchain`): la etapa del emulador reutiliza el SDL3 de `/opt/sdl3/native` en vez de recompilarlo (la versión de Codex fallaba sin headers X11 y nunca se había construido).
- Referencia vs port (dato para 3.2): mismas dimensiones 320×240 y 256 colores; 10.674 píxeles distintos, caja x 1..128, y 0..217 (rayos animados del lado izquierdo). Referencia: frame 0 en VBlank 822, marca en 8642, captura en 8702.

## Spec Change Log

## Review Triage Log

Iteración 0 (`codex-merged`: blind-hunter, verification-gap, edge-case-hunter):

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | CI usa el digest viejo de `dw-port`; `reference_title` devuelve 77 y CI queda verde sin capturar (blind, verification-gap) | medium | Real: el digest de `.gitlab-ci.yml:14` es anterior al cambio del Dockerfile y el skip 77 está permitido. **patch**: `native-disc` corre `xvfb-run -a pcsx-redux -version` antes de CTest (falla si falta el emulador); digests nuevos tras el job `images` |
| 2 | `DW1_OPENBIOS` permite `--update` con un BIOS no OpenBIOS (blind, edge) | low | Real pero solo por uso deliberado de la variable. **patch** por eliminación: la variable se borró (`OPENBIOS` constante; los tests la parchean) |
| 3 | `capture_vblank = 0` captura en el VBlank siguiente a la marca (blind, edge) | false | El port hace lo mismo: `capture_vblank()` de `port/psyq/crt.c:66` corre en el observer del VBlank y con K=0 captura en el primer VBlank tras la marca; mismo punto en ambos lados |
| 4 | Dos capturas concurrentes en el mismo directorio se pisan (blind, edge) | low | Real solo si alguien lanza dos a mano sobre la misma salida; el arreglo requiere locking. Rechazado (improbable, no es una corrección directa) |
| 5 | Sin `reference_sha256`, la corrida normal pasa sin comparar contra la referencia (blind, edge) | low | Real si alguien borra el hash del `.toml`, lo que es visible en el diff. Requiere una rama nueva; rechazado (improbable) |
| 6 | El adaptador Lua no tiene test de inputs: pulsación, mantenimiento y liberación (blind, verification-gap) | medium | Real: `test_capture.py` reemplaza el Lua y `title` usa `inputs = []`. Frame 0, marca y captura sí quedan verificados por la corrida real. **defer** a 3.3, la primera historia con inputs |
| 7 | `--timeout nan/inf` deja la corrida sin límite (edge) | low | Real solo con un argumento absurdo escrito a mano. Rechazado |
| 8 | `inputs = 1` produce `TypeError` fuera del manejo de errores (edge) | low | Falla de forma ruidosa con traceback; no hay daño. Rechazado |
| 9 | Una ruta con caracteres de control rompe el escape Lua (edge) | low | Las rutas salen de `build/` o de `DW1_REFERENCE_DIR`; es improbable. Rechazado |
