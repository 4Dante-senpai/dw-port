---
title: 'Historia 3.6: XA y sectores Form 2'
type: 'feature'
created: '2026-10-20'
status: 'done'
baseline_commit: '92a222b61562dab2f5b7b887c0d529ada8b7bc78'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/spu.md'
  - '{project-root}/docs/port/reference.md'
  - '{project-root}/docs/port/stubs.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Los videos del disco no tienen sonido en el port: `CdRead2` no lee nada, `StGetNext` termina el video de inmediato y no existe decodificador XA. En DW1 el XA solo aparece intercalado en los cuatro `MOVIE/*.STR` (verificado sobre el disco del dueño: 1 de cada 8 sectores, file 1 / canal 1, coding `0x01` = ADPCM 4 bits estéreo 37,8 kHz; ningún otro archivo es Form 2 y el juego no llama `CdlSetfilter`).

**Approach:** Implementar `port/backend/xa.{h,c}` (decodificador XA-ADPCM con interpolación zigzag de psx-spx, adaptado del `backend/xadec.c` CC0 de DW2-PC) y el streaming real de CD en modo RT (`CdRead2`, anillo `StSetRing`/`StGetNext`/`StFreeRing`), que separa los sectores de audio hacia el decodificador y la entrada de CD del SPU y entrega los de video al anillo. El video sigue negro (`libpress` sigue en stub hasta 3.7), pero avanza y termina con la duración real del STR. Una Escena `xa_op1.toml` compara contra PCSX-Redux.

## Boundaries & Constraints

**Always:** Atribución CC0 a DW2-PC en el código adaptado; comentario `asm:` en cada función de `libcd` traducida del asm USA. Nunca copiar código de PCSX-Redux (GPL-2.0): el modelo XA sale de psx-spx y DW2-PC. Datos del disco, PCM y trazas solo fuera del repo; en el repo, hashes y métricas. Tests L1 sin disco en nativo, ASan/UBSan y web. `scene_title`, `scene_title_compare`, `reference_newgame_state` y `scene_title_audio_compare` siguen verdes. Sin cambios en `src/`/`include/`. Lecturas del Pack por la costura posicional existente (sirve también al worker web).

**Never:** MDEC, `libpress` real ni comparación de frames de video (3.7). Salida por SDL3 o resampleo del Host (3.9). Calibración de costos de CD/CPU (3.10). `CdlSetfilter`/modo SF: el juego no los usa; siguen rechazados. `Speed` > 1 en PCSX-Redux.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| XA sintético | sectores 4/8 bits × mono/estéreo × 37,8/18,9 kHz | PCM 44,1 kHz con SHA-256 fijo por combinación, valores esperados calculados aparte | N/A |
| Sector no audio | submodo sin bit Audio, o Form 1 | no llega al decodificador; video al anillo | N/A |
| Coding reservado | bits de coding inválidos en el subheader | sector descartado sin sonido | contado en la traza del port |
| Streaming RT | `CdRead2(Stream\|Speed\|RT)` tras `Setloc` sobre OP1 | 150 sectores/s del reloj virtual; audio 37.800 pares/s; anillo con frames reales | N/A |
| Anillo lleno | el juego no libera frames | la lectura se pausa como en el asm/DW2-PC, sin perder sectores de audio ya leídos | N/A |
| Fin del STR | último sector del archivo | `StGetNext` reporta el final; `MOV_playMovie` vuelve | N/A |
| Referencia ausente | sin captura | código 77 (skip) | N/A |
**Decisiones del dueño (2026-10-20):**
1. **Alcance (opción A):** 3.6 implementa el streaming de CD real (`CdRead2`, anillo `St*`) y el XA; el video sigue negro con la duración real del STR. 3.7 queda para MDEC/`libpress` y la comparación de video. Si la duración real de OP1/OP2 cambia los VBlanks de `title` y `newgame`, se recapturan sus referencias y hashes.
2. **Gate (opción A):** el parche de Redux suma un segundo tap a la salida de `FeedXA` (antes de `voldiv`); el port expone el PCM a la salida de su decodificador, antes del SPU. Bloquean: SNR ≥ 40 dB del XA sobre toda la ventana de `MOV_playMovie` de OP1, alineado por correlación cruzada, y la secuencia ordenada de **todas** las escrituras SPU dentro de esa ventana idéntica a la referencia. Se reportan sin bloquear: el PCM final mezclado, la regla B y los desfases (hasta 3.10).

</frozen-after-approval>

## Code Map

- `reference/dw2-pc/backend/xadec.{c,h}` -- decodificador CC0: 4/8 bits, mono/estéreo, zigzag de 7 fases y 29 taps (6 entradas → 7 salidas). Base de `port/backend/xa.c`. Diferencia: para 18,9 kHz DW2-PC duplica cada muestra; psx-spx/Redux promedian la primera con la anterior. Verificarlo contra psx-spx y anotarlo.
- `reference/dw2-pc/psyq/libcd.c:24-61,375+` -- streaming STR CC0 (`CdRead2`, posición a 150 sectores/s, `StGetNext`, fin del stream, filtro de submodo `0x44`). Reutilizar tras compararlo con el asm USA (`asm/us/main/psyq/libcd.s`; `StSetMask` en :3935).
- `port/psyq/libcd_stream.c:4` -- stubs a reemplazar (`StSetRing`/`StSetStream` ignoran argumentos; `StGetNext` devuelve `0xFFFFFFFF`). `port/psyq/libcd.c:134,183` -- `CdRead` cronometrado y `Setmode` ignorado: agregar estado RT y lectura continua.
- `port/backend/cdrom.c:66` -- el Pack guarda 2336 B por sector F2 (subheader + datos); `Size1` reconstruye 2340 B (subheader en el offset 4). Agregar la lectura secuencial sin rehacer la API.
- `port/backend/spu.{h,c}` -- `spu_set_cd_input` (:18) pide un par estéreo por muestra; mezcla con SPUCNT bit 0 y `0x1B0/0x1B2` (:734); la traza ya registra todas las escrituras (:457). Instalar el callback desde la FIFO de XA. Tap WAV del decodificador por variable de entorno, como el WAV del SPU (:87).
- `port/psyq/libspu.c:584` -- `SpuInit` deja el volumen de CD en 0 y la mezcla apagada. Quién lo habilita en DW1 (`SsInit`/`_SsInit`, `SpuSetCommonAttr` en :425) se verifica con la traza de la referencia, no se supone.
- `port/psyq/libpress.c:52` -- stub negro; no tocar salvo para consumir frames reales del anillo.
- `src/mov/mov.c:111-274` -- consumidor (`MOV_startRead` llama `CdRead2(Stream|Speed|RT|Size1)` en :120; tabla de STR con cantidad de frames en :48-55). No tocar.
- `tools/port/reference/pcsx-redux-pcm-tap.patch`, `capture.{lua,py}`, `tools/port/Dockerfile:97` -- parche aplicado en la etapa `pcsx-redux` (SHA `2068784a…`); agregar (decisión 2) el tap de `FeedXA` en el mismo parche. Reconstruir la imagen local con `tools/port/docker_build.sh`. El job `images` necesita push de rama: preguntar antes.
- `tools/port/scenes/run.py:445`, `audio.py:79,238,267` -- `--compare-audio` exige exactamente dos tramos y filtra `< 0x180`; generalizarlo a una ventana única con todas las escrituras, conservando el comportamiento de `title_audio`.
- `tools/port/scenes/title.toml` -- sin inputs hasta después de OP1; base de `xa_op1.toml` (ventana `MOV_playMovie` de `playMovie(0)`).
- `CMakeLists.txt:163` (L1 nativo/Node) y `:438` (clase B con `--audio --reuse`, fixture, lock, skip 77).
- `docs/port/stubs.md:25`, `tools/port/psyq/gen_stubs.py` -- quitar las filas de streaming que pasen a reales.

## Tasks & Acceptance

**Execution:**
- [x] `port/backend/xa.{h,c}` -- decodificador, FIFO y zigzag; atribución CC0.
- [x] `port/backend/tests/test_xa.c` -- L1: 8 combinaciones, coding reservado y SHA-256 fijos con valores esperados calculados aparte.
- [x] `port/psyq/libcd.c`, `port/psyq/libcd_stream.c`, `port/backend/cdrom.{h,c}` -- modo RT, lectura continua, anillo `St*`, clasificación audio/video y conexión a `spu_set_cd_input`.
- [x] `port/psyq/tests/` -- L1 de streaming con un STR sintético en el Pack de test (ritmo, anillo lleno, fin).
- [x] `tools/port/reference/*`, `tools/port/scenes/{run.py,audio.py,xa_op1.toml,test_runner.py}` -- tap XA, ventana única y gate según la decisión 2.
- [x] `CMakeLists.txt`, `.gitlab-ci.yml` -- L1 en nativo/ASan/web; `reference_xa_op1` y `scene_xa_op1_compare` clase B.
- [x] `docs/port/xa.md`, `docs/port/stubs.md`, `docs/port/reference.md`, `gen_stubs.py` -- modelo, atribución, diferencias con Redux, hipótesis y métricas.

**Acceptance Criteria:**
- Given el título y `newgame`, when corren sus Escenas, then siguen pasando contra la referencia (recapturada si la duración real de OP1/OP2 cambia los VBlanks).
- Given dos capturas de la referencia de `xa_op1`, when se comparan, then traza y PCM son idénticos byte a byte.

## Implementation Notes

- 2026-10-20 (Codex, implementación): `CdInit` reproduce `CD_initvol` (0x800B16B8): volumen de CD 0x3fff y SPUCNT 0xc001, verificados en el asm y en las escrituras de Redux previas a `MOV_playMovie`. Dentro de la ventana de OP1 hay 0 escrituras SPU en ambos lados.
- `StGetNext` avanza un período de sector del reloj virtual cuando no hay frame, para que el busy-loop de MOV no gire millones de veces: hipótesis de planificación, a calibrar en 3.10.
- El runner de escenas sube de 10.000 a 40.000 frames (igual que la referencia): con los STR reales, `newgame` no llegaba a `gameLoop`. Se recapturaron las referencias de `newgame` y `title_audio`; los hashes de `title` no cambian (se miden desde `runLandingScreen`).
- `StGetBackloc` devuelve el ID del último frame publicado sin +1 y la ubicación siguiente al último chunk (contrastado con el asm).
- El modelo XA del port no incluye la matriz ATV ni las ganancias analógicas de Redux; aun así OP1 da 54,42 dB.
- Revisión (iteración 0): 7 patches aplicados por la misma sesión de Codex (sincronización del SPU antes del reset XA, `CdRead` abortado termina con -1, tests de mezcla SPU, pausa con PCM encolado, traza de coding reservado, shifts 13–15 y escena XA de un solo segmento).

## Spec Change Log

## Review Triage Log

Iteración 0 (Codex: blind-hunter, verification-gap, edge-case-hunter):

| # | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|
| 1 | Marcador de vuelta pisado por la reserva (`libcd_stream.c:52-57`) | low | Real solo si un frame ocupa más de la mitad del anillo; DW1 usa 24 sectores (`mov.c:115`) y OP1 ronda 5 sectores por frame | rechazado (improbable; el arreglo agrega lógica) |
| 2 | `CdRead2` cancela un `CdRead` pendiente y `CdReadSync(0)` queda esperando (`libcd.c:94`) | low | La cancelación deja `rd` sin completar; DW1 no lo alcanza, pero la corrección es directa | patch |
| 3 | `XaDec_Reset` antes de sincronizar el SPU pierde PCM pendiente al reiniciar tras EOF (`libcd.c:94-97`) | low | `cd_stream_stop` solo sincroniza con `rt.active`; tras EOF el intervalo pendiente se renderiza en silencio | patch |
| 4 | El digest de la imagen en CI no incluye el tap XA | medium | `.gitlab-ci.yml` fija el digest anterior | cierre: job `images` en el push a `main` y commit que fija el digest nuevo (patrón de 3.5) |
| 5 | `cd_stream_sector` no comprueba `ended` | false | Tras el fin del archivo `rt.active=0` y no hay más entregas; la rama de `end_frame` es idempotente | rechazado |
| 6 | `StSetStream` ignora `mode` | low | DW1 llama solo con modo 1 (`mov.c:116`) | rechazado |
| 7 | Falta verificar XA → `CdRead2` → salida del SPU | medium | verification-gap: ningún test mira la salida del SPU con XA real | patch |
| 8 | Overflow de la FIFO sin contador; la captura incluye muestras descartadas | low | El SPU se sincroniza antes de cada sector XA y el régimen es ~2352 pares por sector, lejos de 16384 | rechazado (inalcanzable en DW1; el arreglo agrega estado) |
| 9 | Vectores sin shifts 13–15 | low | `xa_vectors.py` usa `% 13` | patch (solo test) |
| 10 | Pausa con PCM ya encolado no verificada | medium | verification-gap: la pausa del test ocurre antes del primer sector XA | patch |
| 11 | `CdlSetmode` con cambio de velocidad durante el stream | low | DW1 fija el modo antes de `CdRead2` (`mov.c:129-132`) | rechazado |
| 12 | Un callback reemplaza el stream dentro de `stream_event` | false | DW1 pasa callbacks `NULL` a `StSetStream` (`mov.c:116`) | rechazado |
| 13 | Escena XA con dos segmentos sobrescribe el WAV | low | `run.py` usa `run-N-1.xa.wav` fijo; validación directa | patch |
| 14 | Overflow de tamaños RIFF de 32 bits | low | Requiere >4 GB de captura | rechazado |
| 15 | La contrapresión alarga el STR en vez de descartar sectores | maybe-false | Falta contrastar `StCdInterrupt` (asm) con anillo lleno: ¿pausa o descarta? | defer (medium sin verificar) |
| 16 | Auditoría de matriz: el rechazo de coding reservado no se verifica en la traza | low | `test_cd_stream` no comprueba la línea `xa rejected` | patch |

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless'` -- expected: verde; igual con `native-asan` y `web`.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh` (nativo y ASan) -- expected: clase B verde sin omisiones, incluido `scene_xa_op1_compare`.
- `make compare` en `dw-build` -- expected: idéntico.
