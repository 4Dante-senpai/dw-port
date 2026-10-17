---
title: 'Historia 4.1: El Juego en wasm con su propio hilo y presentación'
type: 'feature'
created: '2026-10-16'
status: 'done'
baseline_commit: '10f07bbb2a5c8ed64e0e8447f7eba3846c9a1b2e'
route: 'dispatch'
review_loop_iteration: 1
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El preset `web` solo compila objetos; el Juego no linkea ni corre en el navegador, no hay presentación y las llamadas por punteros a función de firma distinta (que `call_indirect` de wasm aborta) no están inventariadas.

**Approach:** Linkear `dw1` con Emscripten (pthreads, longjmp/excepciones wasm, memoria fija), arrancar el Juego en un pthread con el stack estático de `crt.c` dentro de `[0, 16 MB)`, presentar el Renderer software por WebGL2 en un `OffscreenCanvas` transferido, y validarlo con Playwright 1.64.0 en Chromium y Firefox. Inventariar y resolver los casts de punteros a función.

## Boundaries & Constraints

**Always:** flags web de AD-7 (`-pthread -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`), `-sINITIAL_MEMORY` fijo, `-sALLOW_MEMORY_GROWTH=0`, sin LTO en objetos del juego, mismas opciones semánticas que nativo (`-fsigned-char`). Todo cambio en `src/` detrás de `#ifdef DW1_PORT`, documentado en `docs/port/src-changes.md`, con `make` + `make compare` verdes. Las decisiones (stack respetado o fallback; resolución de casts) se registran en `.memlog.md` de la arquitectura. Tests sin datos del disco (clase A).

**Never:** ASYNCIFY/JSPI; acceso a OPFS o al Pack (historia 4.2); input, audio o Shell (4.4+); umbrales de rendimiento (Épica 5); romper `native-headless`/`native-asan`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Arranque | página dev con COOP/COEP, `?frames=1` | consola: `reached frame 0` en Chromium y Firefox | test falla si aparece `abort`/`RuntimeError` |
| Stack | hilo del juego creado | `PORT_ASSERT` sobre la dirección de una local del hilo en `[0, 16 MB)` | si falla: `-sPROXY_TO_PTHREAD` + entrada en memlog |
| Sin aislamiento | página sin COOP/COEP | mensaje `cross-origin isolation required` y no arranca | sin crash del runtime |
| Presentación | patrón sintético en VRAM (`?pattern=1`) y frame 0 del Juego | `glReadPixels` del canvas == display convertido, 0 píxeles distintos | el test imprime cantidad y primer píxel distinto |
| Firma distinta | llamada indirecta inventariada | resuelta; el link no emite `function signature mismatch` | CTest falla con filas sin resolver |

</frozen-after-approval>

## Code Map

- `CMakePresets.json:27,40` -- preset `web` existente (solo objetos); extender a link de `dw1` + test preset.
- `CMakeLists.txt:102-137,191,226,250,261-280` -- flags globales, objetos de juego/backend/PsyQ, tests con `-m32`/ELF sin guarda Emscripten, ejecutable `dw1` solo nativo.
- `port/psyq/crt.c:26,90-97,124-165` -- stack estático 4 MiB, `exit` al llegar a N frames, `game_thread`, `pthread_attr_setstack`+`pthread_create`+`pthread_join` (el join no sirve en web: `main` debe volver).
- `port/psyq/port.h:9` -- `PORT_ASSERT`. `port/psyq/ps1mem.h:26,58,63` -- base wasm 0, `ps1_in_window`, `PS1_LINK`.
- `port/psyq/libetc.c:80,95` -- `VSync`/`port_frame_done`: punto donde presentar cada frame.
- `port/backend/gpu.h:14,17`, `port/backend/gpu_soft/gpu_soft.c:613` -- `gpu_vram`, `gpu_read_display` (`0x00RRGGBB`, requiere conversión a RGBA).
- `port/host/headless/main.c:20,105` -- CLI y entrada nativa; modelo para `port/host/web/main.c`.
- `include/dw/world_object.h:8`, `src/main/world_object.c:89,101`, `src/main/main.c:3606,4083` -- `TickFunction void(*)(int32_t)` llamando a `renderPressStartToContinue void(void)`.
- `tools/port/psyq/gen_stubs.py:63`, `CMakeLists.txt:225,268-270` -- stubs `long symbol()` sin prototipo; test `psyq_prototypes`.
- `tools/port/env_check/web_models/` -- spike previo pthread/longjmp (reusar flags).
- `tools/port/Dockerfile` -- imagen `dw-port` (emsdk 6.0.10, node de emsdk); sin Playwright.
- `docs/implementation-artifacts/deferred-work.md:14` -- inventario de casts pendiente (cerrar).

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/Dockerfile` -- (orquestador, antes del handoff) instalar `@playwright/test@1.64.0` global y `playwright install --with-deps chromium firefox` en `/opt/ms-playwright`; reconstruir `dw-port:latest`.
- [x] `tools/port/fnptr/inventory.py` + `tools/port/metrics/fnptr_casts.tsv` -- inventario automático: casts/argumentos de callback (`-Wcast-function-type-strict`/`-Wincompatible-function-pointer-types`), llamadas directas con aridad distinta (`too few/many arguments`, `-Wdeprecated-non-prototype`) y `function signature mismatch` de wasm-ld, con columna de resolución; CTest `fnptr_inventory` falla con filas sin resolver o drift.
- [x] `CMakeLists.txt` (link web) -- llamadas **indirectas**: resolverlas todas con `-sEMULATE_FUNCTION_POINTER_CASTS`, sin tocar `src/`; registrar la decisión (y su costo) en memlog.
- [x] `src/`, `include/` (bloques `DW1_PORT` mínimos) -- solo las llamadas **directas** que wasm-ld rechaza; cada cambio conserva todos los argumentos que la definición consume (nunca descartar un parámetro usado). Si el argumento original no está determinado (p. ej. `setActiveAnim(0xc)`, `a1` residual), trap explícito + entrada en `deferred-work.md`.
- [x] `tools/port/psyq/gen_stubs.py` -- emitir stubs con la firma del prototipo de los headers PsyQ.
- [x] `port/host/plat.h`, `port/host/web/main.c`, `port/host/web/present_gl.c` -- `plat_present(frame)` único (no-op en headless); `main` web arranca el hilo y vuelve; contexto WebGL2 en el hilo del juego sobre el canvas transferido; textura 1× desde el display convertido a RGBA; medición del tiempo por `plat_present`.
- [x] `port/psyq/crt.c` -- en `__EMSCRIPTEN__`: sin `pthread_join`, `PORT_ASSERT` del stack efectivo en `[0, 16 MB)`, fin de corrida sin matar el runtime.
- [x] `CMakeLists.txt`, `CMakePresets.json` -- link web de `dw1` (`.html`/`.js`/`.wasm`), guardas Emscripten en tests nativos, CTest `web_boot` que corre Playwright.
- [x] `port/web/dev/` (página, `serve.py` con COOP/COEP) y `port/web/tests/` (spec Playwright + config) -- arranque, aislamiento, comparación de píxeles y registro de tiempos en `build/port/web/present-timing.json`. El patrón se verifica además contra colores esperados calculados en el test desde su definición (no solo contra el búfer que se sube a la textura); una corrida de varios frames con patrón que cambia por frame verifica cada presentación; el readback y `Module.presentations` solo existen en modo validación (argumento explícito), no en corridas normales.
- [x] `docs/port/src-changes.md`, `.memlog.md`, `docs/port/setup.md`, `deferred-work.md` -- documentar.

**Acceptance Criteria:**
- Given el preset `web`, when se configura y compila, then `dw1.wasm` linkea sin `function signature mismatch` y con memoria fija.
- Given `ctest --preset web`, when corre `web_boot`, then pasan Chromium y Firefox (arranque, aislamiento, píxeles) y queda el JSON de tiempos por navegador.
- Given `native-headless` y `native-asan`, when corren sus CTest, then siguen verdes; `native-asan` mantiene `-fno-sanitize=function` (las llamadas indirectas se resuelven en wasm por emulación, no en el código).

## Design Notes

El `OffscreenCanvas` se transfiere con `emscripten_pthread_attr_settransferredcanvases` (o `-sOFFSCREENCANVAS_SUPPORT`) y el contexto se crea en el hilo del juego con `preserveDrawingBuffer` en modo dev, así `glReadPixels` lee lo presentado (filas invertidas). El patrón `?pattern=1` llena el área de display con un degradado RGB15 determinista para que la comparación no sea trivial (el frame 0 sin Pack puede ser negro). Parámetros de URL llegan como `argv` (`--frames`, `--pattern`).

## Verification

**Commands:**
- `tools/port/docker_run.sh cmake --preset web && cmake --build --preset web && ctest --preset web` -- expected: `web_boot` y `fnptr_inventory` pasan.
- `ctest --preset native-headless && ctest --preset native-asan` -- expected: verdes.
- `make && make compare` en `dw-build` (si se tocó `src/`) -- expected: binario idéntico.

## Implementation Notes

Iteración 0 (revertida): `build/port/4-1-iter0.patch` (no versionado) contiene la implementación previa. Ver KEEP en el Spec Change Log.

## Spec Change Log

- **Iteración 1 (review 0).** Hallazgo: los ~101 adaptadores `port_cb_*` escritos a mano en 52 archivos de `src/` (≈1.700 líneas) descartan argumentos que las definiciones K&R consumen (`TRN_tickMpTraining(instanceId)`, `TRN_tick/renderSlotMachine(arg)`, `TRN2_*`; clang solo avisa `too few arguments`), y el despacho de menús por id (`port_menu_tick[value]`) asume `value == id` sin verificarlo. Enmienda: llamadas indirectas resueltas con `-sEMULATE_FUNCTION_POINTER_CASTS` (sin tocar `src/`); `src/` solo para las llamadas directas que rechaza wasm-ld, conservando argumentos; inventario también de `too few/many arguments`; `native-asan` sigue con `-fno-sanitize=function`; tests de presentación con colores esperados independientes, varios frames y readback solo en modo validación. Estado malo evitado: divergencia masiva con upstream (regla 3), churn de `ps1_literals.tsv`/`isr_spins.tsv` por corrimiento de líneas, y callbacks con argumentos indeterminados. **KEEP** (reutilizar de `build/port/4-1-iter0.patch` salvo que la enmienda lo contradiga): `port/host/plat.h`, `port/host/web/main.c`, `port/host/web/present_gl.c`, `port/psyq/crt.c`, `port/psyq/include/memory.h`, `port/web/**`, cambios web de `CMakeLists.txt`/`CMakePresets.json`, `tools/port/psyq/gen_stubs.py` + test, `tools/port/fnptr/inventory.py` como base, `docs/port/setup.md`; el trap explícito de `setActiveAnim(0xc)` (no inventar el argumento); la estructura de evidencia en `docs/implementation-artifacts/4-1/evidence/`.

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|---|
| 1 | blind/edge/vgap | `trn_mp.c:194` adaptador llama `TRN_tickMpTraining()` sin `instanceId` | high | La definición K&R usa `instanceId` en `removeObject`; clang compila con warning `too few arguments` (verificado en la imagen). bad_spec (iteración 1). |
| 2 | edge | `trn_slots.c`/`trn2_slots.c` adaptadores sin `arg` | high | Prototipo `(int32_t arg)` en `trn_slots.c:32-33`; mismo defecto que #1. Agrupado con #1. |
| 3 | vgap | despacho de menús `port_menu_tick[value]` sin prueba | medium | Asume `value == id` e `id < 6` sin verificar; desaparece con la emulación de casts. Agrupado con #1. |
| 4 | blind/edge | `setActiveAnim(0xc)` hace trap si existe el NPC | medium | Asm `scriptStartAnimation` (`0x80105540`): `a1` = residuo de `scriptNPCStartAnimation`/`startAnimation`; no determinado sin referencia. Defer (épica 3). |
| 5 | blind/vgap | patrón RGB15 comparado contra el mismo búfer que sube | medium | `present_gl.c:59-76` compara contra el RGBA que genera; un swap R/B pasaría. Incorporado a la enmienda. |
| 6 | blind/vgap | tests de un solo frame | medium | `boot.spec.cjs` pide `frames=1` y exige 1 presentación. Incorporado a la enmienda. |
| 7 | blind/edge | readback y `Module.presentations` en cada frame, sin límite | medium | `present_gl.c:67-80` sin condición de modo. Incorporado a la enmienda. |
| 8 | blind | `-fsanitize=function` activado con título de 1 color en native-asan | medium | Evidencia de la iteración 0 (`title-native-asan.log`); causa no aislada. La enmienda vuelve a `-fno-sanitize=function`; defer de la causa (maybe-false). |
| 9 | blind | sin WebGL2: aserción genérica sin mensaje en el DOM | low | Rechazado: mensajes de capacidades son la historia 4.4 (Never del intent). |
| 10 | blind | `test_gen_stubs.py` exige `emcc` en `PATH` | low | Rechazado: los tests corren en la imagen `dw-port`, que lo tiene. |
| 11 | blind/edge | `strtol` sin `ERANGE` en `--frames` | low | Rechazado: argumento de desarrollo; agregar guardas no se justifica. |
| 12 | edge | display deshabilitado cambia el canvas a 320×240 | low | Rechazado: sin evidencia de que el Juego apague el display con otra resolución en esta historia. |
| 13 | edge | pérdida de contexto WebGL no manejada | low | Rechazado: fuera del alcance de la presentación mínima; Épica 5. |
| 14 | edge | `index.html` sin `onerror` del script | low | Rechazado: página de desarrollo; el test falla igual por timeout. |
| 15 | blind | `setActiveAnim` trap (iteración 1) | medium | carried: igual a #4; defer ya registrado. |
| 16 | blind/edge | `strtol` sin `ERANGE` | low | carried: igual a #11, rechazado. |
| 17 | blind/edge | pérdida de contexto WebGL2 | low | carried: igual a #13, rechazado. |
| 18 | edge | display deshabilitado cambia dimensiones | low | carried: igual a #12, rechazado. |
| 19 | edge | `index.html` sin `onerror` | low | carried: igual a #14, rechazado. |
| 20 | blind | sin regulación por tiempo real entre presentaciones | low | Rechazado: el ritmo de 59,94 Hz es de la historia 4.10 / Épica 5 (Never del intent: umbrales de rendimiento). |
| 21 | blind | `inventory.py --update` asigna resoluciones automáticamente | low | Rechazado: los desajustes directos nuevos rompen el link (`--fatal-warnings`) y los indirectos quedan cubiertos por la emulación; no hay alta silenciosa con daño. |
| 22 | blind/vgap | corrida normal sin comprobación de píxeles | medium | `boot.spec.cjs:54-60` solo mira estado y ausencia de muestras. patch. |
| 23 | blind/vgap | `test_setjmp` no corre en wasm | medium | `CMakeLists.txt:206` lo deja solo nativo; el intérprete usa `longjmp` (`script_interp.c:624,641`). patch. |
| 24 | edge | oráculo acepta `pixels` vacío o corto | low | `some` sobre slice vacío es `false`; corrección directa de una línea. patch. |
| 25 | blind | sin pruebas de RGB24 ni cambio de resolución | low | Rechazado: el Juego no ejerce esas rutas en frame 0; el oráculo del patrón cubre la conversión. |
| 26 | blind | frame del Juego sin oráculo independiente | low | Rechazado: la conversión está cubierta por el patrón con oráculo independiente; el frame comparte la misma ruta. |
| 27 | blind/vgap | `tickCameraMoveTo`/`handlePraiseScold` sin prueba dirigida | low | Cambios verificados contra el asm (`j checkCameraMovement` en cola; `a0 = partnerState` intacto hasta `jal handlePraiseScold` en 0x800DDE34) y documentados en `src-changes.md`; no hay infraestructura de tests de lógica de juego. Rechazado. |
| 28 | edge | desborde con signo en `3 * frame` del patrón | low | Rechazado: requiere >715 M frames. |
| 29 | edge | memoria de validación sin límite de frames | low | Rechazado: modo solo de desarrollo, corridas de pocos frames. |
