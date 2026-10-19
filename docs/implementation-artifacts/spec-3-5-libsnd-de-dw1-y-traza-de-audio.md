---
title: 'Historia 3.5: libsnd de DW1 y traza de audio'
type: 'feature'
created: '2026-10-18'
status: 'in-progress'
baseline_commit: 'd7eeb9172f78896e54f7e71ac7f642e4d8f451f7'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/spu.md'
  - '{project-root}/docs/port/reference.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Las 21 entradas de `libsnd` que llama el Juego son stubs `stub-m2:` silenciosos, así que no hay música ni efectos, y no existe forma automática de comparar el audio del port con el Emulador de referencia (AD-15, FR-7).

**Approach:** Traducir `libsnd` del asm USA (`mw_lib41`) con su tick por root counter 2 (`SS_TICK240`) y las entradas de `libspu` que alcanza, apoyándose en la traducción CC0 de DW2-PC (Psy-Q 4.7) solo donde el asm de DW1 coincide. Agregar una traza de escrituras a registros SPU en ambos lados (Backend del port y breakpoints de escritura en PCSX-Redux), captura de PCM de la referencia, y `run.py title_audio --compare-audio`, que compara trazas y calcula el SNR del PCM alineado por correlación cruzada.

## Boundaries & Constraints

**Always:** Comentario `asm:` por función traducida (`asm/us/main/psyq/libsnd.s`, `libspu.s`); atribución CC0 a DW2-PC en todo código adaptado. Estado de `libsnd` en structs C tipados al estilo de `port/psyq/` (no direcciones PS1 fijas como `snd_native.h`). Tick por `SetRCnt`/`InterruptCallback` del reloj virtual existente. Timestamp de la traza = `(ciclo − ciclo del inicio del tramo) / 768` en ambos lados (ver decisión 2). Tests L1 sin disco (secuenciador con SEQ/VAB sintéticos, valores esperados calculados aparte). Traza, PCM e imágenes solo fuera del repo; en el repo, hashes y métricas. `scene_title`, `scene_title_compare` y `reference_newgame_state` siguen verdes. Sin cambios en `src/`/`include/`.

**Never:** XA y sectores Form 2 (3.6), videos (3.7), salida por SDL3 o resampleo del Host (3.9), calibración de costos de CPU (3.10). No usar `Speed` > 1 en PCSX-Redux (decima frames en el sink). No generar el PCM de referencia con el SPU del port.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tick | `SsSetTickMode(SS_TICK240)` + `SsStart` | RCnt2 con el target del asm; `SsSeqCalledTbyT` 240 veces por segundo de reloj virtual | N/A |
| SEQ sintético | note on/off, program change, tempo, loop, fin | escrituras SPU (KON/KOFF, pitch, vol, ADSR) en el tick esperado, calculado aparte | N/A |
| VAB | `SsVabOpenHeadSticky` + `SsVabTransBody` | cuerpo en SPU RAM en la dirección que devuelve `SsUtGetVBaddrInSB` | VAB inválido: retorno de error del asm |
| `SsUtKeyOnV`/`OffV`/`AutoVol`/`AllKeyOff` | voz, VAB, programa, nota | registros según el asm | voz fuera de rango: retorno del asm |
| Traza port vs referencia | misma Escena, decisiones 3/4 | escrituras de voz ordenadas idénticas: tramo 1 completo; tramo 2 desde primera nota nueva tras SsSeqPlay posterior a gameLoop hasta primer evento ajeno al secuenciador, ≥9 s por lado e intervalo común; mismo motivo de cierre | sale ≠ 0 e imprime la primera divergencia |
| PCM | dos WAV de 44,1 kHz que cubren su intervalo de traza (±1 frame) | SNR por ventana de 5 s tras alinear; métrica no bloqueante hasta 3.10, igual que regla B y desfases | imprime la peor ventana y su SNR |
| Referencia ausente | sin captura de referencia | código 77 (skip) como las demás clase B | N/A |

**Decisiones del dueño (2026-10-18):**
1. **Regla de la traza (opción B):** dentro de cada ventana de VBlank (contada desde el checkpoint que abre cada tramo, decisión 2), la secuencia de valores escrita en cada registro SPU es idéntica en ambos lados; no se exige el orden entre registros distintos ni la muestra exacta. El desfase en muestras de cada escritura se reporta como métrica (máximo y media) sin umbral hasta 3.10. AD-15 y el contexto de la épica se enmiendan en esta historia con esta regla. Motivo: el port no modela el tiempo de CPU; solo VBlank y RCnt2 caen en el ciclo exacto.
2. **`title_audio` en dos tramos (renegociada el 2026-10-19, opción A):** la ventana original (desde `runLandingScreen` hasta antes de `unloadNewGameScene`) tiene 0 s de música SEQ (sonda de dos corridas, Implementation Notes). Se define en dos tramos, cada uno con sus VBlanks, timestamps y PCM contados desde el checkpoint que lo abre: **tramo 1** desde `runLandingScreen` hasta la entrada a `unloadNewGameScene` (Start, menú, New Game, escena de Jijimon: efectos); **tramo 2** desde el checkpoint `gameLoop` durante N VBlanks, con N tal que contenga ≥ 10 s de música SEQ, sin inputs nuevos que salgan del mapa. Entre ambos queda excluido `playMovie(1)` (video/XA, de 3.6/3.7). Reutiliza los inputs de `newgame.toml` hasta `gameLoop`. Antes de implementar, una sonda de la referencia confirma la música en el tramo 2; si no hay ≥ 10 s de música SEQ, la implementación se detiene y lo reporta. Las ventanas de PCM de 5 s se evalúan por tramo.

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libsnd.s` (108 funciones, 10,6 k líneas) y `asm/us/main/data/psyq/libsnd.{data,rodata}.s` -- fuente de verdad. Accesos a voces por `_spu_RXX` (base `0x1F801C00`) se traducen a `spu_write16`/`spu_read16`.
- `asm/us/main/psyq/libspu.s` -- entradas que alcanza `libsnd` y que 3.4 dejó pendientes (`SpuInit`, `SpuStart`, `SpuQuit`, `SpuInitMalloc`, `SpuMalloc`, `SpuFree`, `SpuSetKey`, `SpuSetVoiceAttr`, `SpuSetCommonAttr`, `SpuWrite`, `SpuSetTransfer*`, `SpuIsTransferCompleted`, `SpuGetVoiceEnvelope`, `SpuSetNoise*`, `_spu_*`).
- `reference/dw2-pc/psyq/{libsnd.c,snd_seq.c,snd_vm.c,snd_hw.c,libspu.c}` -- traducción CC0 de Psy-Q 4.7 con direcciones fijas; reutilizar la lógica tras comparar cada función con el asm de DW1 (4.1). Diferencias: anotarlas en `docs/port/libsnd.md`.
- `port/psyq/libsnd.c` (stubs a reemplazar), `port/psyq/libspu.c` (6 entradas de 3.4), `port/psyq/psyq.h` (tipos `Ss*`, `SpuVoiceAttr`).
- `port/psyq/libetc.c:125` `InterruptCallback`, `port/psyq/libapi.c:155` `SetRCnt` -- ya modelan RCnt2 con IRQ; no reescribir.
- `port/backend/spu.{h,c}` -- agregar el gancho de traza en la escritura de registros (activado por flag/variable de entorno, sin costo si está apagado) y la escritura de PCM a WAV a pedido.
- `src/main/sound.c`, `sound_async.c` -- consumidores (no tocar); `initializeMusic` en `sound.c:516`.
- `tools/port/reference/capture.lua`/`capture.py` -- agregar breakpoint `Write` sobre `0x1F801C00..0x1F801DFF` (valor: registro `rt` de la instrucción en `pc`, timestamp desde `PCSX.getCPUCycles()`; `getRegisters().cycle` no existe en el SHA fijado) y captura de PCM con el backend SDL `disk` (`SDL_AUDIO_DISK_OUTPUT_FILE`, `SDL_AUDIO_DISK_TIMESCALE`; hipótesis a verificar: el sink es el reloj maestro y no pierde muestras). Fijar `Interp` gaussiana y anotar `KeyOnDelay=6` de Redux.
- PCM de referencia (decisión del orquestador, 2026-10-19): el backend SDL `disk` no sirve (rellena ceros en posiciones distintas en cada corrida; ver `docs/port/libsnd.md`). Tomar el PCM antes del sink: parche propio de PCSX-Redux en `PCSX::SPU::impl::flushOutput` (`src/spu/spu.cc:577` del SHA fijado) que, si está definida una variable de entorno, escribe los frames de `spuBuffer` (PCM16 estéreo 44,1 kHz) a un archivo antes de `dropVoiceFrames`/`feedStreamData`. Aplicarlo en la etapa `pcsx-redux` de `tools/port/Dockerfile` con un `.patch` versionado en `tools/port/reference/` que falle si no aplica (patrón del parche de semilla de OpenBIOS); mantener el sink `dummy`. Verificación: dos corridas dan WAV idénticos byte a byte por tramo. Requiere reconstruir la imagen local con `tools/port/docker_build.sh`; el job `images` de CI necesita push de la rama, que pide permiso al dueño (no pushear).
- `tools/port/scenes/run.py` + `title_audio.toml` -- flag `--compare-audio`, comparación de trazas y SNR; nuevo `reference_title_audio` en `CMakeLists.txt` (patrón de `reference_newgame_state`, línea 444, con `--reuse` y caché de `build/reference/`).
- `tools/port/psyq/gen_stubs.py`, `docs/port/stubs.md` -- quitar las 21 filas `3.5`.

## Tasks & Acceptance

**Execution:**
- [ ] `port/psyq/libsnd.c` (+ `libsnd_seq.c`/`libsnd_vm.c` si conviene) -- 21 entradas y rutinas internas traducidas del asm de DW1.
- [ ] `port/psyq/libspu.c` -- entradas de `libspu` que alcanza `libsnd`.
- [ ] `port/backend/spu.{h,c}` -- traza de escrituras y salida WAV opcional.
- [ ] `port/psyq/tests/test_libsnd.c` -- L1: tick, SEQ sintético, VAB, utilidades `SsUt*`.
- [ ] `tools/port/reference/capture.{lua,py}` -- traza SPU y PCM de la referencia; tests en `test_capture.py`.
- [ ] `tools/port/scenes/run.py`, `title_audio.toml`, `test_runner.py` -- `--compare-audio`, gate por contenido de decisiones 3/4; regla B, desfases y SNR no bloqueantes hasta 3.10, con casos sintéticos.
- [ ] `CMakeLists.txt`, `.gitlab-ci.yml` -- tests L1 en nativo/ASan/web; `reference_title_audio` y `scene_title_audio_compare` clase B.
- [ ] `docs/port/libsnd.md`, `docs/port/stubs.md`, `docs/port/reference.md`, `gen_stubs.py` -- estado, atribución, hipótesis y medición de SNR.
- [ ] `docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md` (AD-15, Audio) y `docs/implementation-artifacts/epic-3-context.md` -- enmendar el gate según las decisiones 3/4, con fecha y motivo.

**Acceptance Criteria:**
- Given las 21 funciones traducidas, when se buscan stubs `stub-m2:` de `libsnd`/`libspu`, then no queda ninguno y `gen_stubs.py` las cuenta como implementadas.
- Given la Escena `title_audio` con disco, when corre `run.py title_audio --build native-headless --compare-audio`, then las escrituras de voz ordenadas coinciden según decisiones 3/4 (tramo 1 completo; tramo 2 desde primera nota nueva tras SsSeqPlay posterior a gameLoop hasta primer evento ajeno al secuenciador, ≥9 s por lado, intervalo común), con el mismo motivo de cierre; regla B, desfases y SNR se reportan sin bloquear hasta 3.10.
- Given dos capturas de la referencia, when se comparan, then traza y PCM son idénticos (determinismo de la referencia).

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless'` -- expected: verde; igual con `native-asan` y `web`.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh` (nativo y ASan) -- expected: clase B verde sin omisiones, incluido `scene_title_audio_compare`.
- `make compare` en `dw-build` -- expected: idéntico.

## Implementation Notes — 2026-10-19

**Implementación detenida por la decisión 2 del dueño.** La ventana aprobada de
`title_audio` contiene **0 segundos de música SEQ**, por debajo de los 10 segundos
requeridos. No se modificó el bloque congelado ni se marcaron tareas como completas.

Se ejecutaron dos sondas del original USA en el PCSX-Redux fijado, con intérprete,
OpenBIOS parcheado, GPU software y SPU dummy con `UseNullSync=true`, `Speed=0`.
Se reutilizaron los inputs de `newgame.toml` anteriores a `gameLoop`, con origen
por checkpoint. Breakpoints Exec contaron las entradas a `SsSeqOpen`, `SsSeqPlay`,
`seqPlay`, `playMusic` y `SsUtKeyOnV` desde el arranque. Ambas sondas terminaron
en la entrada a `unloadNewGameScene`, antes de ejecutar esa función, sin capturar
imágenes ni PCM. Sus métricas coinciden línea por línea:

| Medición | Ambas corridas |
|---|---|
| Frame 0, VBlank desde arranque | 822 |
| `runLandingScreen`, VBlank desde arranque | 8642 |
| `unloadNewGameScene`, VBlank desde arranque | 10312 |
| Ventana | 1670 VBlanks = 27,861167 s |
| Llamadas a `SsSeqOpen`, `SsSeqPlay`, `seqPlay`, `playMusic` | 0, incluso contando desde el arranque |
| Llamadas a `SsUtKeyOnV` dentro de la ventana | 44 |
| `SEQ_ACCESS_NUM` / `CURRENT_SEQ_FONT` en los checkpoints | −1 / −1 |

La sonda y los logs quedan fuera del repositorio en `/tmp/dw1-3-5-gate/`
(`probe.lua`, `run-1.log`, `run-2.log`, `gate.txt`). La ausencia de aperturas y
reproducciones SEQ desde el arranque descarta una secuencia iniciada antes de la marca.
Esto verifica la condición de parada; no demuestra determinismo de PCM ni paridad SPU.
SHA-256 de las métricas normalizadas (`gate.txt`, iguales en ambas corridas):
`dd419015f126df63466a1dba31ca371633fdbb6c4b846902315a25e0cb88a4a2`.

También se comprobó una discrepancia en el Code Map: el emulador fijado rechaza
`PCSX.getRegisters().cycle`. La API disponible es `PCSX.getCPUCycles()`
(`src/core/pcsxffi.lua` y `pcsxlua.cc` del SHA fijado). La primera sonda falló por
ese acceso; las dos corridas válidas usan la API disponible.

Validación dentro de este contenedor: los builds `native-headless`, `native-asan`
y `web` terminan correctamente. Pasan las pruebas seleccionadas `spu_vectors`,
`spu`, `libspu`, `gen_stubs`, `reference_capture_unit`, `scene_runner` y `av_stubs`
donde existen en cada preset, junto al fixture `pack`: 8/8 nativo, 8/8 ASan y
5/5 web. Logs `build-*.log` y `tests-*.log` en el mismo directorio externo.
No se ejecutó `make compare` ni la suite completa de clase B.

Quedan pendientes la traducción de las bibliotecas, la traza SPU, la captura y
comparación de PCM, la integración CI y los criterios de aceptación de audio.
Para continuar hace falta renegociar la ventana de `title_audio` o su requisito
de duración SEQ con el dueño. No se hicieron commits.

### Pendiente según el orquestador (2026-10-19)

Orden: (1) traducir las 21 entradas de `libsnd`, sus rutinas internas y las entradas de `libspu` que alcanza, con `test_libsnd.c` (L1) y sin stubs `stub-m2:`; (2) el tap de PCM de PCSX-Redux del Code Map; (3) escenas reales con disco (`scene_title`, `scene_title_compare`, `reference_newgame_state`, `title_audio`). `cc_mips.dll` no está en la imagen `dw-port`: `make compare` lo corre el orquestador en `dw-build`.

## Spec Change Log

- 2026-10-19: la sonda de la referencia detuvo la implementación (0 s de música SEQ en la ventana de la decisión 2). El dueño eligió dos tramos (`runLandingScreen`→`unloadNewGameScene` y `gameLoop`+N) con origen por checkpoint; se ajustaron la decisión 1, la regla del timestamp y la API de ciclos de Lua (`PCSX.getCPUCycles()`). Evita una ventana continua que cruzaría `playMovie(1)`, cuyo largo difiere en el port. KEEP: las mediciones de la sonda en Implementation Notes.
- 2026-10-19 (decisión 3 del dueño, gate por contenido): la regla B por VBlank y el SNR ≥ 40 dB de AD-15 no pueden pasar antes de la 3.10. El port no modela costos de CPU ni de CD, su período de VBlank difiere del de Redux, y el desfase de 176 VBlanks del tramo 1 ya existía con libsnd en stubs (`docs/port/audio-parity.md`). Nueva aceptación de 3.5: (a) en el tramo 1, la secuencia ordenada de escrituras a registros de voz (`0x000`–`0x17f`) es idéntica a la de referencia (hoy 744/744); (b) el tramo 2 se re-ancla al arranque de la música SEQ (mismo símbolo en ambos lados) y la secuencia ordenada de escrituras de voz de la música es idéntica a la de referencia hasta el primer evento del juego ajeno al secuenciador, con al menos 10 s de música; (c) la regla B por VBlank, el desfase de muestras y el SNR se reportan sin bloquear hasta la 3.10. Se enmienda AD-15 con fecha y motivo. KEEP: la evidencia de `audio-parity.md`.
- 2026-10-19 (decisión 4 del dueño, enmienda la decisión 3 para el tramo 2): el primer grupo tras `SsSeqPlay` recalcula voces heredadas de la canción anterior (`Snd_SetPlayMode` → `_SsVmSetSeqVol`, sin filtro de voz activa), cuyo estado depende del timing de escena/CD de la 3.10; la fórmula del asm reproduce ambos lados (`docs/port/audio-parity.md`, test L1 `sequence_restart_volume`). El tramo 2 se ancla en el primer note-on nuevo (bundle KON con dirección de muestra) posterior al primer `SsSeqPlay` tras `gameLoop` y termina en el primer evento del juego ajeno al secuenciador; mínimo 9 s de música. La secuencia ordenada de escrituras de voz desde ese ancla es idéntica a la de referencia en la ventana común. El recálculo de voces heredadas y los 10 s se difieren a la 3.10. Se enmienda AD-15 con fecha y motivo.

## Review Triage Log
