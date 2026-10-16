---
title: 'Historia 2.12: Renderer software y libgpu'
type: 'feature'
created: '2026-10-15'
status: 'done'
baseline_commit: '3519fe1fda9b0e064216f779b9da998aaa65860f'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** las 28 funciones de `libgpu` que llama el Juego son stubs generados y no existe un
Backend de GPU: nada de lo que el Juego dibuja o sube llega a una VRAM.

**Approach:** adaptar el rasterizador de `dw2-pc/backend/psxgpu.c` (sin HD ni PGXP) como Renderer
software detrás de `port/backend/gpu.h`, e implementar en `port/psyq/libgpu.c` las 28 funciones
(más `DrawOTag` y `ClearOTagR`) traducidas del asm de DW1, con `DrawSync` terminando por costo en
el reloj virtual.

## Boundaries & Constraints

**Always:** cabecera `/* Adapted from DW2-PC (CC0): backend/psxgpu.c @ 816ed0d */` en el Renderer;
una función por entrada en `libgpu.c` con comentario `asm:asm/us/main/psyq/libgpu.s` (o `dw2-pc:`
si se toma de `reference/dw2-pc/psyq/libgpu.c` y coincide con el asm), y `clock_psyq_entry()` en
cada entrada; la Capa PsyQ toca el Backend solo por `gpu.h` (nunca la VRAM ni el estado interno);
enlaces de OT solo con `PS1_LINK`/`PS1_LINK_PTR`; `-Wall -Wextra -Werror` limpio en
`native-headless`, `native-asan` y `web`; sin cambios en `src/`/`include/`.

**Never:** `psxgpu_hd`, PGXP, `psxgpu_hw`, presentación o ventana (Historias 2.15/3.9/5.x);
`libgs` (2.13); funciones de `libgpu` que el Juego no llama, salvo `DrawOTag` y `ClearOTagR`
(las usará `libgs`); `DrawSyncCallback` y la cola interna del asm (`_addque`, `_exeque`);
GP0 C0 (VRAM→CPU en paquete) más allá de ignorarlo con log.

**Decisiones (autónomas):**
- Alcance: las 28 de `docs/research/psyq-usage.md` § libgpu (`AddPrim` … `SetPolyG4`) + `DrawOTag`
  y `ClearOTagR`. Internas que necesiten (`get_tim_addr`, `checkRECT`…) como `static`.
- Ejecución inmediata: cada transferencia y cada `DrawOTag` se ejecutan en la llamada; el Backend
  devuelve su trabajo (palabras transferidas y píxeles escritos) y `libgpu` marca la GPU ocupada
  hasta `clock_now() + costo`. `DrawSync(0)` espera con `clock_wait_until` a ese ciclo;
  `DrawSync(1)` devuelve lo que devuelve el asm mientras está ocupada y 0 al terminar. Constantes
  de costo marcadas `[hypothesis]` en `clock.h`, se calibran en la Historia 3.10.
- `[hypothesis]` que el Juego no modifica el buffer origen entre `LoadImage` y su `DrawSync`
  (en PS1 la DMA lee después); queda anotado en `libgpu.c`.
- Esperados de los tests: chequeos de píxel exactos derivados de psx-spx para cada rasgo
  (dithering, cada modo de semitransparencia, ventana de textura, CLUT 4/8 bit, máscara) más un
  SHA-256 de VRAM por caso fijado en la primera corrida verde (regresión).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Primitivas | F3/F4, G3/G4, FT3/FT4, GT3/GT4, sprite, tile, líneas F2/F3/F4, fill | SHA-256 de VRAM fijo por caso + píxeles clave exactos | N/A |
| Semitransparencia | 4 modos `abr` sobre fondo conocido; texel con bit 15 en 0 y en 1 | fórmula de psx-spx por canal | N/A |
| Dithering | G4 con E1 bit 9 encendido/apagado | matriz 4×4 de psx-spx aplicada/no | N/A |
| Ventana de textura | E2 con máscara y offset | texel repetido según psx-spx | N/A |
| Transferencias | `LoadImage`/`StoreImage` ida y vuelta, `MoveImage` solapado, `ClearImage`, rect que envuelve en 1024/512 | datos idénticos; envoltura de coordenadas | N/A |
| OT | `ClearOTagR(ot, n)` + `AddPrim` de 3 prims + `DrawOTag` | orden de dibujo inverso al índice, como PS1 | lista que no termina: corte tras 0x100000 paquetes con mensaje |
| `DrawSync` | `LoadImage` grande y `DrawSync(1)` repetido, luego `DrawSync(0)` | `DrawSync(1)` ≠ 0 hasta el costo; `DrawSync(0)` avanza el reloj exactamente al ciclo de fin | N/A |
| Helpers | `GetTPage`, `GetClut`, `SetPoly*`/`SetLine*`, `SetSemiTrans`, `SetDrawLoad`, `SetDrawOffset`, `OpenTIM`/`ReadTIM` con TIM sintético 4/8/16 bit con y sin CLUT | bytes y retornos idénticos al asm | N/A |

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libgpu.s` -- fuente de verdad (71 funciones; en el checkout, no versionado).
- `reference/dw2-pc/backend/psxgpu.{c,h}` -- Renderer a adaptar; quitar todo `PsxHd_*`/`Pgxp_*`; el tipo de estado viene de `psxgpu_hd_int.h`, traer solo lo necesario. Agregar contador de píxeles escritos.
- `reference/dw2-pc/psyq/libgpu.c` -- `DrawOTag`, `ClearOTagR`, `PutDrawEnv`, `PutDispEnv`, `SetDefDispEnv`, `AddPrim`: reutilizables si coinciden con el asm.
- `port/backend/gte.h`/`gte.c` + `port/backend/tests/test_gte.c` -- estilo de API (`gte_*`) y de tests; `gpu.h` usa prefijo `gpu_`.
- `port/psyq/libgte.c`, `port/psyq/libgs.c` -- estilo de entrada PsyQ.
- `port/psyq/ps1mem.h` -- `PS1_LINK`, `PS1_LINK_PTR`, `PS1_LINK_END`.
- `port/psyq/clock.{h,c}` -- `clock_now`, `clock_wait_until`, constantes `[hypothesis]`.
- `port/psyq/psyq.h` + `port/psyq/tests/psyq_prototypes.c` -- prototipos contra `external/psyq_headers/mw_lib41/include/libgpu.h`.
- `port/host/pack/sha256.c` (en `dw1_pack_lib`, ya enlazada por `dw1_backend`) -- SHA-256 para los tests.
- `CMakeLists.txt:132-142` -- `dw1_backend`/`dw1_psyq`; `:198-203` patrón de `test_gte`.
- `tools/port/psyq/gen_stubs.py` -- el contador `stubs=` baja en las funciones implementadas.

## Tasks & Acceptance

**Execution:**
- [x] `port/backend/gpu.h` + `port/backend/gpu_soft/gpu_soft.c` -- interfaz (reset, GP0, fill, load/store/move, display, lectura de VRAM, trabajo hecho) y Renderer adaptado.
- [x] `port/psyq/libgpu.c` -- 30 entradas y costo de `DrawSync`; constantes en `port/psyq/clock.h`.
- [x] `port/psyq/psyq.h`, `port/psyq/tests/psyq_prototypes.c` -- prototipos.
- [x] `port/backend/tests/test_gpu.c` -- matriz I/O del Renderer (primitivas, semitrans, dither, ventana, transferencias).
- [x] `port/psyq/tests/test_libgpu.c` -- OT, `DrawSync` por costo, helpers y TIM sintético.
- [x] `CMakeLists.txt` -- fuentes nuevas y tests `gpu`, `libgpu`.
- [x] `docs/STATUS.md` (métrica de stubs) y `docs/implementation-artifacts/evidence/story-2-12.log`.

**Acceptance Criteria:**
- Given `native-headless` y `native-asan` sin `DW1_DISC_DIR`, when `ctest`, then todo pasa (`gpu`, `libgpu`, `frame0` incluidos) y gen_stubs reporta 28 stubs menos que antes, sin ninguno de `libgpu`.
- Given el preset `web`, when se construye, then compila sin errores.
- Given `grep -rn 'PsxHd\|Pgxp\|psxgpu_hd' port/`, then no hay resultados.
- Given `libgpu.c`, when se buscan accesos al Backend, then solo incluye `backend/gpu.h`.
- Given L0a, when `preproc_check.sh`, then OK.

## Implementation Notes

- `port/backend/gpu.h` (prefijo `gpu_`, cada operación devuelve `GpuWork` = palabras y píxeles
  escritos) y `port/backend/gpu_soft/gpu_soft.c`, adaptado de DW2-PC sin HD/PGXP, con VRAM privada.
- `port/psyq/libgpu.c`: 30 entradas; el costo (`CLOCK_GPU_WORD_CYCLES` = 2,
  `CLOCK_GPU_PIXEL_CYCLES` = 1, `[hypothesis]` en `clock.h`) extiende `busy_until` y agenda un
  evento de fin; `DrawSync(1)` devuelve 1 mientras está ocupada.
- `port/psyq/gpu_types.h`: tipos de `libgpu.h` para el port, con `sizeof` y `offsetof` literales
  que `psyq_prototypes.c` también verifica contra `mw_lib41`.
- Tests: `gpu` (píxeles de psx-spx + 30 SHA-256 de VRAM) y `libgpu` (helpers, TIM, OT con
  `DR_LOAD`, `LoadClut`, costo de `DrawSync`). gen_stubs: `stubs=112` (antes 140).
- Revisión: 5 parches aplicados reanudando la sesión Codex del implementador
  (`CODEX_RESUME` en `tools/port/codex-run.sh`); 1 diferido (orden de filas de GP0 80).

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Las líneas monocromas no aplican dithering (blind) | false | psx-spx y DuckStation (`IsDitheringEnabled` = sombreado o textura modulada): las primitivas planas nunca se ditherizan; `gpu_soft.c` lo exige igual. |
| 2 | `draw_disp` (E1 bit 10) se guarda y no se usa (blind) | low | Solo afecta modos entrelazados de 480 líneas; ningún test ni escena actual lo alcanza. Rechazado. |
| 3 | Un comando GP0 partido entre nodos de la OT se descarta (blind, edge) | low | Los paquetes de PsyQ son autocontenidos (`SetDrawLoad` corta a 13 palabras, el resto de `Set*` tiene longitud fija); no hay caso en el Juego que lo produzca. Agrega un buffer pendiente. Rechazado. |
| 4 | `MoveImage` con solape elige el orden de filas por `dy > sy`, distinto en la envoltura 511→0 y posiblemente distinto del hardware (blind, edge) | maybe-false | Hay que contrastar el orden real de copia de filas del GP0 80 con el Emulador de referencia. **defer** (medium sin verificar). |
| 5 | `gpu_types.h` verifica `sizeof` pero no offsets contra `mw_lib41` (blind) | medium | Un campo reordenado con el mismo tamaño pasaría y el Juego (compilado con `libgpu.h` de `mw_lib41`) leería otro campo. **patch**: `offsetof` con literales en `gpu_types.h` y los mismos en `psyq_prototypes.c`. |
| 6 | GT3/GT4 sin interpolación conjunta de color y UV (blind, verification gap) | medium | Pre-verificado (gap): los hashes GT usan color y textura constantes. **patch**: casos GT con colores por vértice y UV variables, píxeles calculados aparte. |
| 7 | Sin tests de `flip_x`/`flip_y` ni UV que cruzan 255 (blind) | low | Los flips de rectángulo los activa E1 bits 12/13, que `PutDrawEnv` de `mw_lib41` nunca escribe. Rechazado. |
| 8 | El comentario "Completion survives intervening VBlanks" no se demuestra (blind) | low | La carga cuesta 524 288 ciclos, menos de un frame (~565 000). **patch**: corregir el comentario. |
| 9 | `DrawOTag` no valida enlaces corruptos (blind, edge) | false | `PS1_LINK_PTR` hace `PORT_ASSERT(link < PS1_LINK_END)` y un enlace fuera de la ventana falla en voz alta; la PS1 tampoco valida la DMA. |
| 10 | Falta test de la desactivación de capas en `codex_bmad_layers.py` (blind) | low | Verificado a mano con `render_skill.py`: solo queda `codex-merged`. Rechazado. |
| 11 | `LoadClut` sin verificación de VRAM ni retorno (verification gap) | medium | Pre-verificado. **patch**: cargar 256 colores distintos y comprobar VRAM y el id devuelto. |
| 12 | `DR_LOAD` no se prueba a través de la OT (verification gap) | medium | Pre-verificado. **patch**: `SetDrawLoad` + `AddPrim` + `DrawOTag` y comprobar la CLUT. |
| 13 | `ReadTIM` acepta bloques menores de 12 bytes o desalineados (edge) | false | `get_tim_addr` del asm solo compara el magic `0x10`; no valida tamaños. La implementación coincide con el asm. |
| 14 | `SetDrawLoad` con ancho o alto 0 (edge) | false | El asm calcula `(w*h+1)/2` sin guardas; tamaño 0 en A0 es el máximo también en hardware. El Juego no pasa rects vacíos. |

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && env -u DW1_DISC_DIR ctest --preset $p; done'` -- expected: verde
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web'` -- expected: exit 0
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK
