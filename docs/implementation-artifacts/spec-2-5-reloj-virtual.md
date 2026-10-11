---
title: 'Historia 2.5: Reloj virtual y semántica de VSync'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: '042370b0ea6fafde3e7b49dc62f30a7e9e667efc'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Sin un dueño único del tiempo, las esperas del Juego (`VSync`, encuestas a flags de
callbacks) no terminan o terminan en un orden distinto en cada corrida.

**Approach:** `port/psyq/clock.c` lleva ciclos de CPU (33.868.800 Hz) y una cola de eventos
(VBlank sin deriva, root counters, eventos de dispositivo); toda entrada PsyQ avanza el reloj y las
esperas saltan al próximo evento. `VSync`, `VSyncCallback(s)`, `InterruptCallback` y
`ResetCallback` (libetc) y `SetRCnt`/`GetRCnt`/`ResetRCnt` (libapi) se traducen del asm de DW1
sobre ese reloj.

## Boundaries & Constraints

**Always:** comentario de fuente `asm:asm/us/main/psyq/<lib>.s` por función; callbacks solo en el
hilo que llama al reloj (`PORT_ASSERT` si otro hilo lo usa); determinista (sin reloj de pared).

**Never:** reloj de pared, pausa o rebase (Host, historias 2.6/4.x); eventos BIOS
(`OpenEvent`/`TestEvent`, usados por libmcrd/libspu: Historias 2.14/3.4/3.8); costos de CD/GPU/MDEC
(llegan con cada dispositivo).

**Decisiones (autónomas):**
- VBlank k ocurre en el ciclo `floor(k · 33868800 · 1001 / 60000)` (exacto, sin acumular error).
- 263 líneas por frame para el conteo de `VSync(1)` [hipótesis: NTSC; calibrar en 3.10].
- Costo fijo por entrada PsyQ: 64 ciclos [hipótesis; calibrar en 3.10].
- `VSync` usa un contador de líneas del reloj, no el root counter 1; se omite la espera de campo
  entrelazado del asm (DW1 usa 320×240 no entrelazado) hasta que exista el estado GP1 (2.12).
- La AC "`TestEvent` en loop" se cubre con `GetRCnt`/flags de callback: `TestEvent` es BIOS (fuera).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Sin deriva | 35.964 VBlanks | VBlank 35.964 en el ciclo `floor(35964·33868800·1001/60000)` | N/A |
| `VSync(0)` | llamada a mitad de frame | vuelve en el próximo VBlank | N/A |
| `VSync(n>1)` | último retorno en VBlank v | vuelve en el VBlank v+n (si se llama antes) | N/A |
| `VSync(1)` / `VSync(-1)` | — | líneas desde el último retorno / conteo de VBlanks, sin esperar | N/A |
| Encuesta | loop `while (!flag) VSync(-1);` con el flag puesto por `VSyncCallback` | termina | N/A |
| Root counter | `SetRCnt(RCntCNT2, t, RCntMdINTR)` + `InterruptCallback(6, f)` | `f` cada `8·t` ciclos | N/A |
| Determinismo | dos corridas iguales | trazas de eventos idénticas byte a byte | N/A |
| Otro hilo | reloj llamado desde un segundo hilo | — | `PORT_ASSERT` |

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libetc.s` -- `v_wait` (l.51), `VSync` (l.97), `ResetCallback`, `InterruptCallback`, `VSyncCallback(s)`, `trapIntrVSync` (Vcount++ y luego los 8 slots en orden).
- `asm/us/main/psyq/libapi.s` -- `SetRCnt` (l.322: modo → bits de hardware), `GetRCnt`, `ResetRCnt`.
- `external/psyq_headers/mw_lib41/include/libetc.h:68-74`, `libapi.h:29-33` -- prototipos.
- Usuarios: el Juego llama `VSync`, `ResetCallback`; `libsnd` usa `SetRCnt`, `ResetRCnt`, `InterruptCallback`, `VSyncCallback`.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/clock.{h,c}` -- reloj, cola, espera, traza, dueño de hilo.
- [x] `port/psyq/libetc.c` -- `VSync`, `VSyncCallback`, `VSyncCallbacks`, `InterruptCallback`, `ResetCallback`.
- [x] `port/psyq/libapi.c` -- `SetRCnt`, `GetRCnt`, `ResetRCnt` sobre el reloj.
- [x] `port/psyq/tests/test_clock.c` -- matriz completa.
- [x] `CMakeLists.txt` -- fuentes y test.

**Acceptance Criteria:**
- Given los dos presets, when `ctest`, then `clock` pasa junto con el resto.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde

## Implementation Notes

- `VSync` traducido de `asm/us/main/psyq/libetc.s`: `v_wait(pre_vcount - 1 + n)` (o `pre_vcount` si n = 0) y luego `v_wait(Vcount + 1)`; guarda `pre_vcount` y el ciclo de las líneas. `trapIntrVSync`: `Vcount++` y después los 8 slots; `VSyncCallback(f)` = slot 4.
- `SetRCnt`: mismo cálculo de bits de modo que el asm (`0x48`/`0x49`/`0x248`, `|0x100`, `|0x10`). El período con reset al llegar al target se asume de `target` ticks [hipótesis, se verifica en 3.5 con SS_TICK240].
- Un evento de dispositivo que vence en el mismo ciclo que un VBlank corre después del VBlank; dos eventos en el mismo ciclo, en orden de programación.
- `clock_wait_until` sin eventos posibles sigue saltando de VBlank en VBlank (el VBlank siempre está pendiente), así que no hay deadlock silencioso: la espera de un flag que nadie pone no termina, como en la consola.
- `psyq.h` repite los prototipos de mw_lib41 que el port implementa.
- Revisión: interrupciones enmascaradas durante un handler (un avance anidado solo suma tiempo; una espera anidada es fatal), tiempo monótono, ids con generación, cola llena fatal en todo build, `ResetCallback` solo inicializa la primera vez (`startIntr`), contador 1 en HBlank por `clock_lines`, chequeo de prototipos contra mw_lib41.
- Evidencia: `native-headless` 13/13, `native-asan` 11/11 (`docs/implementation-artifacts/evidence/story-2-5.log`). `native-asan` corre 2 tests menos: `overlay_sections` y `overlay_objects` no aplican bajo sanitizers (Historia 2.3).

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `ResetCallback` borra los callbacks en cada llamada; el original solo la primera vez | high | Verificado: `startIntr` sale con 0 si `intrEnv` ya está iniciado; DW1 lo llama desde `main`, `initializeMusic`, `PadInit`, `CdInit`, libpress. **patch**: solo la primera llamada limpia e instala el trap, devolviendo distinto de 0; test con dos llamadas. |
| 2 | Un callback que llama a PsyQ hace retroceder el tiempo (`now = target`) | high | Reproducido por un revisor. **patch**: `run_until` nunca retrocede; test `handler_calls_psyq`. |
| 3 | Callbacks anidados: eventos despachados dentro de un handler (recursión, crash con período < costo de entrada) | high | Reproducido (segfault). **patch**: interrupciones enmascaradas durante un handler; test con período de 8 ciclos. |
| 4 | Espera bloqueante desde un handler | medium | **patch**: fatal (`clock_wait_until` en handler); test `handler_wait_aborts`. |
| 5 | Ids de evento reutilizados: cancelar un id viejo mata otro evento; `psyq_libapi_reset` deja eventos vivos | medium | Verificado. **patch**: ids con generación; el reset cancela; test `cancel_ids`. |
| 6 | Sin test de orden en el mismo ciclo (VBlank vs evento, `seq`) | medium | Verificado. **patch**: `same_cycle_order`. |
| 7 | Cola llena o uso desde otro hilo solo se registran en release | medium | **patch**: `CLOCK_FATAL` aborta en todo build; `check_thread` en todas las funciones que modifican el estado. |
| 8 | 59,94 Hz vs ~59,83 Hz de NTSC no entrelazado sin marcar como hipótesis | medium | Verificado contra psx-spx (dato recordado). **patch**: ratio en `CLOCK_VBLANK_NUM/DEN` marcado [hipótesis], pregunta en la memlog de la arquitectura, se mide en 3.10. |
| 9 | Contador 1 en HBlank con 2148 ciclos truncados deriva contra el VBlank | low | **patch**: valor por `clock_lines` (misma razón que el VBlank); IRQ en HBlank no soportada (assert). |
| 10 | Modo gate/sync y dot clock del contador 0 aceptados en silencio | low | **patch**: `PORT_ASSERT` (no usados por DW1). |
| 11 | `target == 0` sin test; período 0x10000 asumido | low | **patch**: marcado [hipótesis] y test `target_zero` (incluye `ResetRCnt` con el contador armado). |
| 12 | Overflow de `k * CLOCK_HZ * 1001` y de las líneas | low | **patch**: cálculo partido por bloques de 60000 VBlanks. |
| 13 | Determinismo comparado por hash dentro de un proceso | low | **patch**: traza completa byte a byte, también contra un proceso hijo. |
| 14 | `psyq.h` dice repetir mw_lib41 sin verificarlo | low | **patch**: `psyq_prototypes` compila `psyq.h` junto a `kernel.h`/`libapi.h`/`libetc.h` (un prototipo distinto es error) y compara las constantes. |
| 15 | `dw1_psyq` usa pthreads sin linkearlas | low | **patch**: `Threads::Threads` PUBLIC. |
| 16 | `InterruptCallback(0, f)` aceptado y nunca llamado | low | **patch**: `PORT_ASSERT(irq != 0)`. |
| 17 | `VSync` antes de `ResetCallback` se cuelga en silencio | low | **patch**: `PORT_ASSERT(intr_started)`. |
| 18 | `VSyncCallback` devuelve `int` y trunca punteros de 64 bits | false | El port es de 32 bits en ambos builds (AD-3, `_Static_assert` en `ps1mem.h`). |
| 19 | El test de dueño de hilo falla con `NDEBUG` | false | El check de hilo ahora aborta en todo build (`CLOCK_FATAL`). |
| 20 | `.gitignore` con `core` ignora cualquier `core` del árbol | low | **patch**: `/core` y `/core.*` en la raíz; el hook pre-commit rechaza un `core` en cualquier ruta. |
| 21 | Evidencia sin comando ni commit; STATUS.md | low | **patch**: el log incluye commit base, tests por preset y la salida de `test_clock`; STATUS al cerrar. |
