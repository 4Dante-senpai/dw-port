---
title: 'Historia 2.14: stubs con comportamiento definido para audio, video y Memory card'
type: 'feature'
created: '2026-10-16'
baseline_commit: '29f4645d9bb2ea2350ce337018e6129d17c39336'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** quedan 52 stubs generados (`port_stub_hit` + `return 0`): `libsnd` (21), `libspu` (6),
`libpress` (6), `libmcrd` (11) y el streaming de `libcd` (`CdRead2` + 7 `St*`). Con ellos
`MOV_startRead` nunca sale (`CdRead2` devuelve 0) y la Memory card no tiene un comportamiento
elegido, así que el arranque no puede llegar al título.

**Approach:** implementar las 52 como versiones mínimas con comportamiento documentado (silencio,
video negro que termina, tarjeta ausente), listadas en `docs/port/stubs.md` con la historia de la
Épica 3 que las reemplaza, y un test L1 del video y de la tarjeta.

## Boundaries & Constraints

**Always:** una función por entrada en `port/psyq/libsnd.c`, `libspu.c`, `libpress.c`, `libmcrd.c`
y `libcd_stream.c` (o `libcd.c`), con firmas de `external/psyq_headers/mw_lib41/include/*.h` y un
comentario `stub-m2: <comportamiento>` en cada una; `clock_psyq_entry()` en cada entrada;
prototipos verificados en `psyq_prototypes.c`; `-Wall -Wextra -Werror` limpio en
`native-headless`, `native-asan` y `web`; sin cambios en `src/`/`include/`.

**Never:** modelo de SPU, secuenciador SEQ, MDEC real, decodificación de STR ni `.mcd` (historias
3.4, 3.5, 3.7, 3.8); `port_yield` (2.15); funciones de esas librerías que el Juego no llama
(`libds` no tiene ninguna: se anota en `stubs.md`).

**Decisiones (autónomas):**
- Audio: cada función devuelve el valor del camino de éxito sin sonido: `SsVabOpenHeadSticky` y
  `SsVabTransBody` devuelven el `vabid` recibido, `SsVabTransCompleted` 1, `SpuGetKeyStatus`
  `SPU_OFF`, `SsSeqOpen` 0; el resto 0 o `void`. Cada valor se confirma contra su uso en
  `src/main/sound*.c`.
- Video: stream sintético de un frame negro de 320×240 marcado como último
  (`StHEADER.frameCount = 0xFFFFFFFF`): `CdRead2` devuelve 1, `StGetNext` siempre devuelve 0 con el
  mismo encabezado y un bitstream estático, `DecDCTout` llena el buffer con ceros y llama al
  callback de `DecDCToutCallback` en el hilo del juego de forma iterativa (si el callback vuelve a
  llamar `DecDCTout`, la llamada queda pendiente y la corre el loop exterior, sin recursión). Así
  `MOV_playMovie(0)` hace una sola vuelta de su loop, muestra un frame negro y devuelve 1.
- Memory card ausente: las asíncronas (`MemCardExist`, `MemCardAccept`, `MemCardReadFile`,
  `MemCardWriteFile`) registran el comando y devuelven 1; `MemCardSync` (modo 0 o 1) devuelve -1 sin
  comando pendiente y, si hay uno, 1 con ese `McFunc*` y `McErrCardNotExist`, sin esperar; las
  síncronas (`MemCardCreateFile`, `MemCardDeleteFile`, `MemCardFormat`, `MemCardGetDirentry`)
  devuelven `McErrCardNotExist`. Modelo de `reference/dw2-pc/psyq/libmcrd.c`, verificado contra el
  uso en `src/main/main_menu.c`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Video | `MOV_playMovie(0)` real (TU de `src/mov/mov.c`) sobre un Pack sintético con `MOVIE/OP1.STR` | devuelve 1, `MOVIE_COMPLETED == 1`, un número fijo de VBlanks (constante del test), slices negros en VRAM | N/A |
| Callback reentrante | `DecDCTout` llamado desde el callback | slices en orden, sin recursión, `flipPending` al final del frame | N/A |
| Tarjeta: polling | `MemCardSync(1)` sin comando; `MemCardExist(0)` + `MemCardSync(1)` | -1; luego 1 con `cmd = McFuncExist`, `result = McErrCardNotExist` | N/A |
| Tarjeta: espera | `MemCardSync(0)` tras `MemCardReadFile` | vuelve sin avanzar más que el costo de una entrada PsyQ | N/A |
| Audio | secuencia de `loadVAB`/`seqPlay` de `sound.c` | `vabid` devuelto igual al pedido, `SsVabTransCompleted(1) == 1` | N/A |

</frozen-after-approval>

## Code Map

- `src/mov/mov.c` -- consumidor del video: `MOV_startRead` (loop sobre `CdRead2`), `MOV_readFrame` (`StGetNext`, `header->frameCount/width/height`), `MOV_onSliceDecoded` (callback, slices de 24×h desde `MOV_SLICE_BUFFER_*` de 2880 longs), `MOV_waitForFlip` (spin sin PsyQ: el callback debe haber puesto `flipPending` antes). No tocar.
- `src/main/sound.c:400-490,570-665`, `src/main/sound_async.c:140-200` -- valores de retorno que usa el Juego.
- `src/main/main_menu.c:3287-3320` (`updateMemoryCardState`) y `3400-4320` -- uso de `MemCardSync`/`result`.
- `reference/dw2-pc/psyq/libmcrd.c` (CC0) -- modelo asíncrono de comandos; adaptar solo la parte de tarjeta ausente, con atribución.
- `external/psyq_headers/mw_lib41/include/{libsnd,libspu,libpress,libmcrd,libcd}.h` -- firmas, `StHEADER`, `McFunc*`, `McErr*`, `SPU_OFF`.
- `port/psyq/libcd.c` -- estilo, `CdControl`/`CdSearchFile`/`CdControlB` ya implementados; `port/psyq/libgs_data.c` define `StCdIntrFlag`.
- `port/psyq/tests/test_libcd.c` -- patrón de Pack sintético (fixture `pack_t1.pack`); si no se puede agregar `MOVIE/OP1.STR` sin cambiar sus hashes, el test arma el suyo.
- `CMakeLists.txt:136-160,260-275` -- fuentes de `dw1_psyq` y patrón de tests; el objeto del overlay `mov` sale de los TUs envoltorio (2.3).
- `tools/port/psyq/gen_stubs.py` -- contador `stubs=` (hoy 52).

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/libsnd.c`, `libspu.c`, `libpress.c`, `libmcrd.c`, `libcd_stream.c` -- las 52 entradas mínimas con `stub-m2:`.
- [x] `port/psyq/psyq.h` y `port/psyq/tests/psyq_prototypes.c` -- prototipos contra `mw_lib41`.
- [x] `port/psyq/tests/test_av_stubs.c` + `CMakeLists.txt` -- la matriz I/O; fuentes nuevas en `dw1_psyq`.
- [x] `docs/port/stubs.md` -- tabla función, comportamiento y historia que la reemplaza (3.4 `libspu`, 3.5 `libsnd`, 3.7 `libpress` + `St*`/`CdRead2`, 3.8 `libmcrd`); `libds` sin uso.
- [x] `docs/STATUS.md` (métrica de stubs) y `docs/implementation-artifacts/evidence/story-2-14.log`.

**Acceptance Criteria:**
- Given `native-headless` y `native-asan` sin `DW1_DISC_DIR`, when `ctest`, then todo pasa (`av_stubs`, `frame0` incluidos) y gen_stubs reporta `stubs=0`.
- Given las 52 funciones, when se busca `stub-m2:`, then cada una lo tiene y cada una figura en `docs/port/stubs.md`.
- Given el preset `web`, when se construye, then compila sin errores.
- Given L0a, when `preproc_check.sh`, then OK.

## Implementation Notes

- Cinco archivos nuevos en `port/psyq/` (`libsnd.c` 21, `libspu.c` 6, `libpress.c` 6, `libmcrd.c` 11,
  `libcd_stream.c` 8), cada entrada con `stub-m2:` y `clock_psyq_entry()`; prototipos en `psyq.h`
  verificados en `psyq_prototypes.c`. gen_stubs: `stubs=0` (antes 52).
- `DecDCTout` drena callbacks reentrantes con un flag `out_draining`: una sola salida pendiente.
- Test `av_stubs`: enlaza los objetos reales del overlay `mov` y el fixture `pack_t1.pack` (ya trae
  `MOVIE/OP1.STR`); `MOV_playMovie(0)` devuelve 1 con 20 slices negros y 8 VBlanks fijos;
  `LoadImage` interceptado con `--wrap`.
- `frame0` ya no ve trazas de stubs: `port_frame_done` escribe `frame N` en la traza y el test la
  busca.
- Revisión: 2 huecos de test (reset con estado activo, `DIRENTRY` real) corregidos reanudando la
  sesión Codex del implementador; 1 diferido (`DRAWENV` sin inicializar en `MOV_playMovie`).

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `MOV_playMovie` pasa `draw` sin inicializar a `PutDrawEnv` (blind, verification gap) | medium | Real (`src/mov/mov.c:263`), preexistente en el C del Juego y ahora alcanzable; el arreglo es un bloque `DW1_PORT` en `src/`, que esta historia excluye. Puede afectar la captura determinista de 2.15. **defer**. |
| 2 | Dos `DecDCTout` desde un callback: la segunda pisa a la primera (blind, edge) | low | El MDEC tiene una sola salida en curso y `MOV_onSliceDecoded` pide una por callback. Arreglo = cola nueva. Rechazado. |
| 3 | `DecDCTReset` sin test con estado activo (blind, verification gap) | medium | Pre-verificado (gap). **patch**: reset con callback instalado y desde un callback con salida encolada. |
| 4 | Sin segunda reproducción sin preparar globals (blind) | low | `MOV_startStream` resetea el decoder y el final quita el callback; el Juego no repite sin pasar por ahí. Rechazado. |
| 5 | `cards()` no corre el consumidor de `main_menu.c` (blind) | low | Los valores se eligieron leyendo `main_menu.c`; el recorrido real llega con 2.15/3.8. Rechazado. |
| 6 | `audio()` no corre `sound.c`/`sound_async.c` (blind) | low | Igual que 5; los retornos se fijaron contra esos call sites. Rechazado. |
| 7 | Sin `port_stub_hit` se pierde el diagnóstico de las mínimas (blind) | low | Las 52 están listadas en `docs/port/stubs.md` con comportamiento fijo; no hay camino desconocido que diagnosticar. Arreglo = logging nuevo. Rechazado. |
| 8 | `MemCardGetDirentry` solo probado con `dir = NULL` (verification gap) | medium | Pre-verificado (gap): `getCardUsedBlockCount` pasa `MEMCARD_DIRENTRIES`. **patch**: array con centinelas. |
| 9 | `StGetNext` con `addr`/`head` nulos (edge) | false | El único llamador (`MOV_readFrame`) pasa direcciones de locales. |
| 10 | `DecDCTvlc2` con `buf` nulo (edge) | false | `MOV_decodeFrame` pasa `runLevelBuffer[i]`, buffers estáticos. |
| 11 | `DecDCTout` con `buf` nulo (edge) | false | Los llamadores pasan `sliceBuffer[i]`, estáticos. |
| 12 | `size` que desborda `size_t` (edge) | false | El Juego pasa `(24*240)/2 = 2880`. |
| 13 | Callback que encola salidas sin fin (edge) | low | `MOV_onSliceDecoded` deja de encolar al terminar el frame (20 slices). Arreglo = límite nuevo. Rechazado. |
| 14 | Comando de tarjeta nuevo con otro pendiente pisa al anterior (edge) | low | El Juego hace `MemCardSync` antes de cada comando y `updateMemoryCardState` solo llama `MemCardExist` con -1; arreglo = guarda nueva. Rechazado. |

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && env -u DW1_DISC_DIR ctest --preset $p; done'` -- expected: verde
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web'` -- expected: exit 0
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK
