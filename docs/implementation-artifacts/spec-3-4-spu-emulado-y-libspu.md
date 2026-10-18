---
title: 'Historia 3.4: SPU emulado y libspu'
type: 'feature'
created: '2026-10-18'
status: 'done'
baseline_commit: '1ed9e84208ebd465ec73a328af85219e6cfa6c1f'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/stubs.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El port no tiene SPU: las 6 entradas de `libspu` que llama el Juego son stubs `stub-m2:` y no hay generación de audio, así que `libsnd` (3.5) y XA (3.6) no tienen dónde sonar.

**Approach:** Adaptar `reference/dw2-pc/backend/psxspu.{h,c}` (CC0) a `port/backend/spu.{h,c}` (24 voces ADPCM, ADSR, ruido, reverb, KON/KOFF/ENDX, transferencias manuales y DMA, entrada de CD por callback) sincronizado con el reloj virtual: antes de cada escritura o lectura de registro el SPU genera las muestras pendientes hasta `clock_now()`, a una muestra cada 768 ciclos. Traducir del asm de DW1 las entradas de `libspu` que llama el Juego, con sus rutinas internas, sobre los registros del Backend.

## Boundaries & Constraints

**Always:** Atribución CC0 a DW2-PC en el encabezado; comentario `asm:` por función traducida (`asm/us/main/psyq/libspu.s`). Muestras totales tras N VBlanks = `clock_vblank_cycle(N) / 768` (división entera sobre ciclos exactos, sin acumulador en coma flotante), equivalente a `floor(N × 44100 × 1001 / 60000)`. Tests L1 sin datos del disco, con SHA-256 fijos de muestras generadas por bloques sintéticos y valores esperados calculados aparte (psx-spx), no copiados del renderer. La salida de audio en `native-headless` se descarta (o se escribe a un archivo solo si se pide por flag); sin dependencia de SDL. `scene_title` y `scene_title_compare` siguen verdes. Sin cambios en `src/`/`include/`.

**Never:** `libsnd` (3.5), decodificador XA y sectores Form 2 (3.6; la entrada de CD queda como callback sin fuente), salida por SDL3 o resampleo del Host (3.9), comparación de audio contra la referencia (3.5).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| ADPCM | bloques sintéticos con los 5 filtros, shift 0..12, flags loop start/end/repeat | muestras con SHA-256 fijo; ENDX y repetición según flags | N/A |
| ADSR | ataque lineal/exponencial, decay, sustain, release | nivel por muestra con SHA-256 fijo y valores puntuales calculados aparte | N/A |
| Reverb | work area sintética y parámetros de un preset | salida con SHA-256 fijo; escritura solo en ESA..0x7FFFE | N/A |
| Sincronía | escritura de registro en el ciclo c | antes de aplicarla, muestras generadas = c / 768 | N/A |
| Ritmo | 35.964 VBlanks | `clock_vblank_cycle(35964)/768` muestras exactas; sin deriva al partir el intervalo en llamadas arbitrarias | N/A |
| Transferencia | `SpuWrite` / DMA de N bytes a una dirección | SPU RAM con esos bytes; `SpuIsTransferCompleted` según el reloj virtual | dirección fuera de 512 KB aborta en debug |
| `SpuGetKeyStatus` | voz con KON, en release y terminada | `SPU_ON`, `SPU_ON_ENV_OFF`/`SPU_OFF_ENV_ON`, `SPU_OFF` según el asm | N/A |

**Decisiones (2026-10-18, el dueño delegó en el orquestador; revisables):** (1) Se traducen las 6 entradas que llama el Juego (`SpuClearReverbWorkArea`, `SpuGetKeyStatus`, `SpuSetReverb`, `SpuSetReverbDepth`, `SpuSetReverbModeParam`, `SpuSetReverbVoice`) y las rutinas internas que alcanzan; el resto de `libspu` (p. ej. `SpuInit`, `SpuSetVoiceAttr`) se traduce en 3.5 cuando `libsnd` las llame. (2) Spec completa sin dividir.

</frozen-after-approval>

## Code Map

- `reference/dw2-pc/backend/psxspu.{h,c}`, `psxspu_gauss.inc` -- base a adaptar (`PsxSpu_Reset/Write16/Read16/DmaWrite/DmaRead/Render`; ADPCM, ADSR, ruido, reverb a 22,05 kHz, entrada CD desde `xadec`). Reemplazar la dependencia de `xadec` por un callback de entrada de CD (`NULL` = silencio).
- `port/backend/gpu_soft/`, `port/backend/gte.{c,h}`, `port/backend/cdrom.{c,h}` -- patrón de módulo del Backend y de su registro en `CMakeLists.txt`.
- `port/psyq/clock.{h,c}` -- `CLOCK_HZ`, `clock_now()`, `clock_vblank_cycle(k)` (`clock.c:50`), `clock_psyq_entry()`. El SPU usa ciclos, no VBlanks.
- `port/psyq/libspu.c` -- 6 stubs `stub-m2:` a reemplazar; `port/psyq/psyq.h` define `SpuReverbAttr`, `SPU_OFF`, etc.
- `asm/us/main/psyq/libspu.s` -- 45 funciones; acceso a registros por `_spu_RXX` (base `0x1F801C00`) y a SPU RAM por `_spu_t`/DMA; traducir a llamadas del Backend. Datos de presets de reverb en `asm/us/main/data/psyq/libspu.data.s` / `libspu.rodata.s` si los usa `SpuSetReverbModeParam`.
- `tools/port/psyq/gen_stubs.py` y `docs/port/stubs.md` -- contar las entradas como implementadas y quitar sus filas `3.4`.
- `port/backend/tests/` -- agregar `test_spu.c` (L1) y registrarlo en CTest como los demás tests del Backend.

## Tasks & Acceptance

**Execution:**
- [x] `port/backend/spu.h`, `port/backend/spu.c` -- adaptación CC0 con sincronía por ciclos y callback de CD.
- [x] `port/psyq/libspu.c` -- 6 entradas y rutinas internas traducidas del asm sobre `spu.h`; arranque del SPU en el reset del port.
- [x] `port/backend/tests/test_spu.c` -- casos de la matriz con SHA-256 fijos y valores puntuales calculados aparte.
- [x] `port/psyq/tests/` -- test de las 6 entradas (`SpuGetKeyStatus` en sus cuatro estados, reverb: registros escritos según el asm).
- [x] `CMakeLists.txt` -- módulo y tests en nativo, ASan y web.
- [x] `docs/port/stubs.md`, `gen_stubs.py`, `docs/port/spu.md` (corto) -- estado, atribución, decisiones e hipótesis.

**Acceptance Criteria:**
- Given los tests L1, when corren en `native-headless`, `native-asan` y `web`, then ADPCM, ADSR y reverb dan los SHA-256 fijos y el SPU genera las muestras pendientes antes de cada escritura de registro.
- Given 35.964 VBlanks del reloj virtual, when se cuentan las muestras, then son exactamente `clock_vblank_cycle(35964)/768` (= 26.459.973).
- Given el título con disco, when corren `scene_title` y `scene_title_compare`, then siguen verdes con el SPU activo.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless'` -- expected: verde.
- Igual con `native-asan` y `web` -- expected: verdes.
- `DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native-headless -R "scene_title|reference_title"` -- expected: verde, sin omisiones.
- `make compare` en la imagen `dw-build` -- expected: binarios idénticos (sin cambios en `src/`).

## Implementation Notes

- Implementado por Codex (sin commits): `port/backend/spu.{h,c}` adaptado de DW2-PC (CC0) con sincronía por ciclos (`clock_now()/768`), callback de CD y salida descartable; las 6 entradas de `libspu` traducidas del asm con presets de reverb de `libspu.data.s`; vectores independientes en `tools/port/audio/spu_vectors.py`.
- 26.459.973 muestras en 35.964 VBlanks, verificado por test.
- Tras la revisión: DMA encadenada, cursor de reverb sin signo, guarda `NDEBUG`, vectores de PMON, pitch fraccionario con cruce de bloque y EON.
- Verificación del orquestador (2026-10-18): builds nativo, ASan y web; CTest nativo 46/46, ASan 44/44, web 26/26; con disco 6/6 sin omisiones (`pack_disc`, `dw1_overlay`, `gte_tables_disc`, `scene_title`, `reference_title`, `scene_title_compare`). Evidencia: `docs/implementation-artifacts/evidence/story-3-4.log`.
- Hipótesis abiertas: costo temporal de la DMA del SPU (3.10); sweep con fase negativa sin implementar (heredado de DW2-PC); paridad de audio contra la referencia (3.5).

## Spec Change Log

## Review Triage Log

Iteración 0 (`codex-merged`: blind-hunter, verification-gap, edge-case-hunter):

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Una DMA nueva reemplaza `transfer_due` aunque la anterior siga pendiente (blind, edge) | low | Real en `spu.c:377-395`; hoy `clear_work_area` espera cada bloque, pero `libsnd` (3.5) puede encadenar. Corrección directa (`max`). **patch** |
| 2 | Leer el FIFO `0x1A8` devuelve el último valor escrito (blind, edge) | low | Ninguna entrada traducida ni `src/` lee el FIFO; `libspu` usa DMA. Rechazado (improbable) |
| 3 | Sin detección ni emisión de IRQ del SPU (blind) | low | El Juego no llama `SpuSetIRQ*` (`docs/research/psyq-usage.md`). Rechazado |
| 4 | Sweep con fase negativa sin implementar (blind) | low | Limitación heredada de DW2-PC y documentada en `docs/port/spu.md`; la medición de audio es de la 3.5. Rechazado |
| 5 | PMON sin test (blind, verification-gap) | medium | Real: ningún test escribe PMON; quitar el bloque `spu.c:564-568` no rompe nada. **patch** |
| 6 | Interpolación gaussiana con `ip != 0` y cruces de bloque sin test (blind, verification-gap) | medium | Real: el vector ADPCM usa pitch `0x1000`. **patch** |
| 7 | Efecto de EON sobre voces sin test (verification-gap) | medium | Real: ROOM se alimenta por CD; quitar el envío de voces no rompe nada. **patch** |
| 8 | `rev_pos` como `int` desborda tras 2³¹ muestras (~13,5 h) (blind, edge) | low | Real (UB con signo); corrección directa a `unsigned`. **patch** |
| 9 | El test de `SIGABRT` falla con `NDEBUG` (blind, edge) | low | Real si se compila en Release; guarda de una línea. **patch** |
| 10 | `spu_vectors.py` replica las mismas operaciones que el backend (blind) | maybe-false | Lo resuelve la comparación de audio contra PCSX-Redux, AC de la 3.5. Rechazado aquí |
