---
title: 'Historia 3.3: Inputs por VBlank y comparación de estado'
type: 'feature'
created: '2026-10-18'
status: 'done'
baseline_commit: 'd3752e88b767d1be831ffb635a747e3ec581d649'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/scenes.md'
  - '{project-root}/docs/port/reference.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Los frames del título coinciden, pero nada reproduce inputs en el port ni compara variables del juego: una divergencia de lógica (incluido `rand`) pasa inadvertida si no cambia la imagen.

**Approach:** Inputs por VBlank aplicados en el latch del VBlank k en ambos lados; `tools/port/scenes/state_vars.txt` con variables por símbolo y tipo; el port y `capture.lua` vuelcan esas variables (punteros normalizados) y cada salida de `rand` en cada VBlank desde la marca hasta el punto de control; `run.py --state` compara ambas trazas y nombra la primera variable distinta y su VBlank. Una Escena nueva con inputs llega desde el título a código que llama a `rand`.

## Boundaries & Constraints

**Always:** Mismo punto de muestreo que la captura (tras despachar el VBlank k, antes de volver al juego). Normalización idéntica en ambos lados: puntero a la arena PS1 → `0x80000000 | offset`; puntero a un global → `símbolo+offset` (nativo con `dladdr`/tabla, referencia con `config/us/symbols.txt`); NULL → `0`. Trazas, volcados y capturas solo en `build/` o `$DW1_REFERENCE_DIR`; al repo van la secuencia de inputs, `state_vars.txt`, el `.toml` y hashes. `scene_title` y `scene_title_compare` siguen verdes. API Lua no verificada se marca hipótesis en `docs/port/reference.md` hasta probarla.

**Never:** Cambios en `src/`/`include/` salvo que sean imprescindibles (entonces detrás de `DW1_PORT` y en `docs/port/src-changes.md`); variables del BIOS en `state_vars.txt`; leer código de SoftGPU; exigir frames idénticos en la Escena nueva (solo estado y `rand`; su diff de frames se informa).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| HAPPY_PATH | `run.py <escena> --build native-headless --state`, disco | trazas port y referencia idénticas; imprime N VBlanks, M variables, R llamadas a `rand`; sale 0 | N/A |
| Divergencia | una variable difiere desde el VBlank k | sale 1: `first divergence at VBlank k: <símbolo>[i] port=<v> reference=<v>` | conserva ambas trazas |
| `rand` distinto | secuencias de `rand` difieren | sale 1 nombrando el índice de llamada, el VBlank y ambos valores | N/A |
| Símbolo inexistente | nombre ausente en `symbols.txt` o en el binario nativo | sale 1 antes de correr, nombrando el símbolo y el lado | N/A |
| Tipo inválido | tipo fuera de `u8 s8 u16 s16 u32 s32 ptr` o cuenta ≤ 0 | sale 1 antes de correr | N/A |
| Escena sin `rand` | la traza tiene 0 llamadas a `rand` y la Escena declara `expect_rand = true` | sale 1 | N/A |
| Sin disco / sin emulador | igual que 3.1/3.2 | 77 | N/A |

**Decisiones (2026-10-18, el dueño delegó en el orquestador; revisables):** (1) Base de tiempo de los inputs: la marca de la Escena (`inputs_from = "mark"`), no el frame 0, porque hasta la 3.7 el video de introducción es un stub y frame 0 → marca difiere en ~7.600 VBlanks; el título ya prueba que la marca está alineada. `inputs_from = "frame0"` se conserva como valor por defecto (title). (2) Semilla de `rand`: el port adopta la semilla inicial observada en la referencia OpenBIOS (provisional, igual que la referencia; la corrida con BIOS real del dueño ya está diferida) y reemplaza la hipótesis `0x24040001` si difiere. (3) Las trazas de estado se generan con el mismo `capture.py`/`run.py` (sin script nuevo). (4) Spec completa sin dividir.

**Decisión 5 (2026-10-18, orquestador con delegación del dueño; revisable, reemplaza la base de la decisión 1):** inputs y muestreo de estado alineados por **puntos de control** y no por VBlank absoluto. Un punto de control es la n-ésima entrada a un símbolo (`checkpoints = ["runMainMenu", "newGameScene", ...]` en el `.toml`, con `"símbolo#n"` para la n-ésima). Cada input es `{after = "<checkpoint>", vblank = k, buttons = [...]}`: se aplica en el latch del VBlank k contado desde que ese lado alcanzó el punto. El estado se compara en cada punto de control y en cada VBlank de una ventana `window` tras él (por defecto 0); `rand` se compara como secuencia ordenada de valores, informando el VBlank de cada lado sin exigir que coincida. La divergencia nombra el punto de control, la variable y el VBlank de cada lado. Motivo: las cargas de CD tienen costos sin calibrar (3.10) y desalinean el VBlank absoluto (`CURRENT_MENU` en el VBlank 165); la paridad de ritmo es de la 3.10, no de esta historia.

</frozen-after-approval>

## Code Map

- `tools/port/scenes/title.toml` -- formato actual: `mark`, `capture_vblank`, `inputs = []`, `reference_sha256`. Escena nueva `tools/port/scenes/<id>.toml` (p. ej. `newgame`): misma marca `runLandingScreen`, `inputs_from = "mark"`, `inputs = [{vblank = k, buttons = [...]}, ...]`, `capture_vblank` = punto de control, `expect_rand = true`.
- `tools/port/reference/capture.lua` -- `input(k)` aplica `held` con `pad.setOverride(bit)` en cada `GPU::Vsync` relativo a `origin` (frame 0); agregar base `mark`, volcado por VBlank de `state_vars` (lectura de RAM por dirección; API Lua exacta = hipótesis a verificar, p. ej. `PCSX.getMemPtr()`), y traza de `rand`: breakpoint en `rand` (`0x8009127C`, `config/us/symbols.txt:71`) + breakpoint en su `ra` para leer `v0` (mismo patrón que frame 0).
- `tools/port/reference/capture.py` -- genera la tabla `DW1_CAPTURE`; agregar `inputs_from`, direcciones/tipos de `state_vars.txt` y ruta de la traza de estado; modo que no exija `reference_sha256` de frames para la Escena nueva.
- `port/psyq/libetc.c:27-41` -- `PadInit`/`PadRead` devuelven `~pad_buf` sin input. Agregar programa de inputs del Host aplicado en el despacho del VBlank (mismo observer que la captura en `port/psyq/crt.c:48-100`). Verificar contra la referencia el mapeo bit de override de PCSX-Redux ↔ formato de `PadRead` (el juego prueba Start con `POLLED_INPUT & 0x800`, `src/main/main.c:3667`).
- `port/psyq/libc.c:5-29` -- `rand` LCG `0x41C64E6D`/`0x3039` (igual a OpenBIOS `psxrand`, verificado en su fuente MIT); `BIOS_RAND_SEED` hipótesis. Agregar traza de cada salida con el VBlank actual. El juego nunca llama a `srand` (`grep` en `src/`, `asm/us`).
- `port/psyq/crt.c` -- flags del Host (`--capture-mark`, `--capture-vblank`, `--clock-trace`); agregar `--inputs <archivo>`, `--state-vars <archivo>`, `--state-trace <archivo>`, y relativo a marca o frame 0.
- `port/psyq/ps1mem.h:39-70` -- `ps1_in_window`/`ps1_link` para traducir punteros de la arena a `0x80000000 | offset`.
- `CMakeLists.txt` -- el preset nativo puede necesitar `-rdynamic` para `dlsym`/`dladdr`, o una tabla generada desde `state_vars.txt`; elegir lo más simple que funcione en `native-headless` y `native-asan`.
- `tools/port/scenes/run.py`, `test_runner.py` -- extender con `--state` y los casos de la matriz (trazas sintéticas).
- Candidatos de `state_vars.txt` (verificados en `config/us/symbols.txt`): `FIRST_SCREEN_FRAME_COUNTER`, `FIRST_SCREEN_PRESSED_START`, `MAIN_STATE`, `POLLED_INPUT`, `POLLED_INPUT_PREVIOUS`, `CHANGED_INPUT`; sumar ≥ 6 más del código que recorre la Escena (menú, fade, objetos, estado de partida), con al menos un `ptr`.
- Escena con `rand`: `rand` se llama en `src/main/particle.c`, `model.c`, `math.c` (`randomLimit`), `efe.c`. Camino candidato: título → Start → menú principal → New Game → primer mapa. Elegir el primer punto alcanzable con inputs donde `rand` tenga ≥ 16 llamadas; si la Memory card vacía de la referencia (en `work-N/`) frente a "tarjeta ausente" del port desvía el menú, documentarlo y elegir un camino que no dependa de ella.

## Tasks & Acceptance

**Execution:**
- [ ] `port/psyq/scene.c`, `tools/port/reference/capture.lua`, `capture.py`, `tools/port/scenes/run.py` -- puntos de control (decisión 5): inputs `after`, muestreo en cada punto + ventana, `rand` como secuencia; tests sin datos de cada caso.
- [ ] Desborde de `BOYS.MMD` sobre `TAMER_MODEL_BUFFER` -- reproducir la contigüidad del original (`asm/us/main/data/` y `config/us/symbols.txt`) bajo `DW1_PORT` o en la capa de memoria del port, sin alterar el build PS1; documentar en `docs/port/src-changes.md`.
- [ ] SIGSEGV en `clearMapDigimon` -- causa raíz verificada contra el asm y la referencia (qué lee/escribe el original en esa situación); arreglo mínimo bajo `DW1_PORT`.
- [ ] `tools/port/scenes/newgame.toml` -- extender los inputs hasta el primer tramo donde la referencia llame a `rand` ≥ 16 veces (p. ej. primer mapa jugable); registrar en `docs/port/scenes.md` el camino y la cuenta.
- [ ] `port/psyq/libetc.c`, `port/psyq/crt.c` -- programa de inputs por VBlank (base marca o frame 0) y volcado de `state_vars` + `rand` por VBlank con normalización de punteros.
- [ ] `port/psyq/libc.c` -- traza de `rand`; semilla según la decisión 2, con evidencia en el comentario.
- [ ] `tools/port/reference/capture.lua`, `capture.py` -- base `mark`, volcado de estado y traza de `rand` en la referencia.
- [ ] `tools/port/scenes/state_vars.txt` -- formato `<símbolo> <tipo>[<cuenta>]`, comentarios con `#`.
- [ ] `tools/port/scenes/<id>.toml` -- Escena nueva con inputs y `expect_rand = true`.
- [ ] `tools/port/scenes/run.py`, `test_runner.py`, `tools/port/reference/test_capture.py` -- `--state` y casos de la matriz sin datos; test de los overrides de inputs (cierra el diferido de 3.1).
- [ ] `CMakeLists.txt`, `.gitlab-ci.yml` -- test clase B `scene_<id>_state` en `native-disc`.
- [ ] `docs/port/scenes.md`, `docs/port/reference.md` -- formato de inputs y `state_vars`, APIs Lua verificadas, semilla observada, diff de frames informativo de la Escena nueva.

**Acceptance Criteria:**
- Given la Escena nueva y el disco, when se corre `run.py <id> --build native-headless --state`, then sale 0 con trazas de estado y `rand` idénticas a la referencia y al menos 16 llamadas a `rand`.
- Given la misma Escena en `native-asan`, when se corre con `--state`, then también coincide.
- Given `title`, when se corren `scene_title` y `scene_title_compare`, then siguen verdes.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I -B tools/port/scenes/test_runner.py` -- expected: OK.
- `... python3 -I -B tools/port/reference/test_capture.py` -- expected: OK.
- `ctest --preset native-headless` y `ctest --preset native-asan` en la imagen -- expected: verdes.
- `DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native-headless -R "scene_|reference_title"` -- expected: verde, sin omisiones.
- `DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/scenes/run.py <id> --build native-asan --state` -- expected: sale 0.

## Implementation Notes

- Implementados inputs y muestras por checkpoint/ventana, 15 variables tipadas y 16 llamadas naturales a `rand` en `newgame`; preservadas las correcciones mínimas bajo `DW1_PORT` documentadas en `src-changes.md`.
- Triage: aplicadas #1, #2, #3, #5, #7, #8, #9, #11 y #12. Las repeticiones de referencia exigen igualdad de bytes, incluidos VBlanks; la comparación port/referencia conserva la decisión 5. La huella incluye `run.py` y `openbios` o SHA-256 del BIOS externo; se sella tras todas las validaciones. Punteros fuera de RAM, marcas ausentes y checkpoints nativos sin instrumentación fallan explícitamente.
- Hipótesis abiertas: OpenBIOS/semilla provisionales respecto al BIOS del dueño y ritmo de CD pendiente de 3.10; se mantienen los diferidos/rechazados del triage.
- Verificación de revisión: builds `native-headless`, `native-asan` y `web` correctos; los tres presets CTest sin fallos (41/39/23 pruebas ejecutadas; 8/8/3 omitidas sin disco). `test_runner.py` y las 26 pruebas de `test_capture.py` pasan, incluidos Lua real sin disco, caché válida/ausente/obsoleta y preflight real de `rand`/`VSync`. Codex agotó su cuota durante la batería con disco; la completó el orquestador.

- Verificación del orquestador (2026-10-18), tras los parches: builds nativo, ASan y web; CTest nativo 49/49, ASan 47/47 y web 26/26; con disco 13/13 sin omisiones (incluidas `reference_newgame_state`, con repeticiones idénticas byte a byte, y `scene_newgame_state`); `run.py newgame --build native-asan --state` coincide en 9 checkpoints, 15 variables y 16 llamadas a `rand`; título ASan con 0 píxeles distintos; `make compare` idéntico. Evidencia: `docs/implementation-artifacts/evidence/story-3-3.log`.

## Spec Change Log

- 2026-10-18, iteración de implementación 0 (sin revisión): la Escena `newgame` divergió en `CURRENT_MENU` en el VBlank 165 por el ritmo de las cargas, el port falló más adelante (ASan: `BOYS.MMD` desborda `TAMER_MODEL_BUFFER`, que en PS1 es contiguo a `MAIN_D_801A8B98`; SIGSEGV en `clearMapDigimon` por una entrada puesta en NULL) y la referencia hizo 0 llamadas a `rand` en 3000 VBlanks. Enmienda: decisión 5 (puntos de control), tareas de los dos fallos y extensión de la Escena hasta ≥ 16 llamadas a `rand`. KEEP: inputs por VBlank en ambos lados, `state_vars.txt` y su validación, normalización de punteros, traza de `rand`, `run.py --state` y sus tests, `NULL` guard de `script_menu.c` y `NAMING_CTRL_BOXES` (documentados en `src-changes.md`), `port/psyq/include/libgpu.h`. La rama se rebasó sobre `main` con la 3.4 (`d3752e8`): el diff de revisión se toma contra ese commit.

## Review Triage Log

Iteración 0 (`codex-merged`: blind-hunter, verification-gap, edge-case-hunter; diff contra `d3752e8`):

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Las dos repeticiones de la referencia se comparan con el comparador tolerante de port contra referencia (blind, verification-gap) | medium | Real (`capture.py:193`): oculta VBlanks no deterministas del propio emulador. **patch** (igualdad exacta entre repeticiones) |
| 2 | La huella de la referencia no incluye el BIOS (blind, edge) | low | Real: una captura con `--bios` hereda el sello de OpenBIOS. **patch** |
| 3 | `run.py` no está en la huella aunque el capturador importa sus helpers (blind, edge) | low | Real. **patch** |
| 4 | Sin control de la extensión del símbolo PS1 en `state_vars.txt` (blind, edge) | low | `config/us/symbols.txt` no tiene tamaños; las 15 variables actuales son escalares o arrays revisados. Diferido |
| 5 | `capture.lua` enmascara como RAM cualquier puntero no nulo (blind, edge) | low | Real: scratchpad o registros de hardware se confundirían con RAM. **patch** (fallar o emitir el valor crudo) |
| 6 | Sin canonicalización de punteros al área fusionada `TAMER_MODEL_BUFFER`/`MAIN_D_801A8B98` (blind, edge) | low | Ninguna variable actual apunta a esas áreas. Diferido |
| 7 | `state.sha256` se escribe antes de validar el SHA fijado de la imagen (blind, edge) | low | Real. **patch** (sellar al final) |
| 8 | `port_scene_prepare` no falla si la marca nativa no existe (blind, edge) | low | Real: agota el límite sin diagnóstico. **patch** |
| 9 | El macro `addPrim` del puente no tiene test (blind, verification-gap) | medium | Real: `test_libgpu.c` usa `AddPrim`. **patch** |
| 10 | Faltan casos inválidos del parser C de `--state-vars`/`--inputs` (blind) | low | La validación principal vive en `run.py`, cubierta por `test_runner.py`. Rechazado |
| 11 | `main() --state` no se prueba con referencias válidas, ausentes u obsoletas (verification-gap) | medium | Real. **patch** |
| 12 | Un checkpoint en una función no instrumentada (`rand`, `VSync`) pasa el preflight y nunca dispara (edge) | low | Real. **patch** (rechazar en preflight) |

Nota: el pipeline 100 de `main` (`d3752e8`, 3.4) falló en `av_stubs` bajo ASan (9 VBlanks en lugar de 8). El `DRAWENV` sin inicializar de `MOV_playMovie` depende de la basura de pila. Esta historia lo inicializa bajo `DW1_PORT` (`src-changes.md`), lo que elimina esa dependencia.
