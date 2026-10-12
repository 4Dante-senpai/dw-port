---
title: 'Historia 2.8: libcd y modelo del CD sobre el Pack'
type: 'feature'
created: '2026-10-12'
status: 'done'
baseline_commit: '4f00447dd399ad1e5e5e62422bddaef4e6084734'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `libcd` son stubs: el Juego no lee ningún archivo del disco y los overlays nunca se
cargan ni reinician sus datos.

**Approach:** Lector del Pack (`port/host/pack`), modelo del CD en `port/backend/cdrom` (sectores
de 2048 y 2340 B por LBA) y `port/psyq/libcd.c` con las funciones de `libcd` que llama el Juego
sobre el reloj virtual. `dw1 --pack <archivo>` abre el Pack y verifica que sea del Manifest
versionado.

## Boundaries & Constraints

**Always:** semántica de `asm/us/main/psyq/libcd.s` (comentario `asm:` por función); toda entrada
avanza el reloj (`clock_psyq_entry`); callbacks en eventos del reloj; sin cambios en `src/`;
`native-headless` y `native-asan` verdes; sin bytes del disco en el repo.

**Never:** `St*` y `CdRead2` (streaming de video: 2.14/3.7); XA/CD-DA; errores de lectura ni
reintentos simulados.

**Decisiones (autónomas):**
- Funciones: `CdInit`, `CdControl`/`B`/`F` (`CdlNop`, `CdlSetloc`, `CdlSetmode`, `CdlPause`; otro
  comando → `PORT_ASSERT`), `CdSync` (devuelve `CdlComplete`), `CdRead`, `CdReadSync`,
  `CdReadCallback`, `CdSearchFile`, `CdIntToPos`, `CdPosToInt`. Salen de los stubs generados.
- `CdRead(n, buf, mode)`: 1 al empezar (cancela una lectura en curso); tamaño por sector según
  `mode & 0x30`: 0 → 2048, `CdlModeSize1` → 2340 (cabecera MSF BCD + modo 2 reconstruida +
  2336 del Pack); otro → `PORT_ASSERT`. Un evento por sector copia ese sector a `buf`. Primer sector
  a `CDROM_SEEK_CYCLES` + 1 sector; cada sector cuesta `CLOCK_HZ/75` o `CLOCK_HZ/150` según
  `CdlModeSpeed` [hipótesis: seek = `CLOCK_HZ/10`; ambos se calibran en 3.10]. Al último:
  `CdReadCallback(CdlComplete, result)`.
- `CdReadSync(0)` espera con `clock_wait_until`; `CdReadSync(1)` devuelve los sectores restantes.
- `CdSearchFile`: ruta exacta del índice (`\DIR\NAME;1`); copia `pos`, `size` (tamaño ISO: el
  del Manifest para `F1`, `sectores × 2048` para `F2`) y `name`; devuelve `fp` o NULL; cuesta seek
  + 1 sector [hipótesis]. El límite de 64 entradas por directorio de PsyQ no se modela.
- Sectores: `F1` → datos de usuario (ceros tras `size`); `F2` en 2048 B → los 2048 B tras el
  subheader. `F1` en `Size1` → `PORT_ASSERT` (el Juego lee en `Size1` solo `MOVIE/*.STR`,
  verificado en `src/`). LBA fuera de todo archivo del índice → `PORT_ASSERT` (ceros en release).
- Sin `--pack` el disco está vacío: lecturas en ceros y `CdSearchFile` = NULL (modo de `frame0`).
- Overlays: al terminar una lectura que empieza en la LBA de un `*_REL.BIN` y cubre sus sectores,
  `libcd` llama una vez a `port_overlay_loaded(lib)` (id por nombre de archivo, como
  `REL_BIN_FILES`) y traza `overlay <lib> <ruta> dest=<dir PS1> sha256=<hash de los bytes en dest>`
  en `DW1_TRACE`. `dw1 --until-overlay` sale con 0 tras la primera.
- SHA-256 de `us.tsv` en build (`file(SHA256)`) → `pack_open` rechaza un Pack de otro Manifest.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Buscar | `CdSearchFile("\\MOVIE\\OP1.STR;1")` en Pack sintético | `pos` = LBA, `size` ISO, devuelve `fp` | inexistente → NULL |
| Leer F1 | `CdRead(n, buf, CdlModeSpeed)` | bytes del archivo; `CdReadSync(0)` vuelve en seek + n × `CLOCK_HZ/150` | N/A |
| Leer Size1 | archivo F2, `CdlModeSize1` | 2340 B por sector con MSF de la LBA | F1 en Size1 → abort en debug |
| Fuera del índice | LBA sin archivo | abort en debug (hijo con `RLIMIT_CORE=0`) | ceros en release |
| Callback | `CdReadCallback(f)` | `f(CdlComplete, …)` en el ciclo del último sector | N/A |
| Pack ajeno | Pack de otro Manifest | `dw1 --pack` sale ≠ 0 con mensaje | N/A |
| Overlay | Pack USA, `dw1 --until-overlay` | una línea `overlay`; bytes en el `*_START` del overlay con el SHA-256 del Manifest | N/A |

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libcd.s` -- `CdSearchFile` (2836: NULL si no empieza con `\`, copia 0x18 B), `CdRead` (3406), `CdReadSync` (3481), `cd_read_retry` (3269), `CdIntToPos`/`CdPosToInt` (586/656, BCD y +150).
- `external/psyq_headers/mw_lib41/include/libcd.h` -- `CdlFILE`, `CdlLOC`, constantes `Cdl*`.
- `src/main/file.c`, `file_queue.c`, `script_interp.c:2785`, `efe.c:1022`, `efe_table.c` -- usos (siempre `CdlModeSpeed`); `utils2.c:385` `loadDynamicLibrary`, `REL_BIN_FILES`/`REL_BIN_OFFSETS`.
- `port/host/pack/pack.{c,h}` -- agregar lector (`pack_open/close/find_path/find_lba/read`) reusando el formato y `Entry`.
- `port/psyq/clock.h` -- `clock_schedule`, `clock_wait_until`, `clock_psyq_entry`; `port/psyq/libetc.c` es el modelo de estilo.
- `port/psyq/overlay.{c,h}` -- `port_overlay_loaded`; agregar id por nombre de archivo.
- `port/psyq/stub.c` -- traza `DW1_TRACE`; generalizar a `port_trace(fmt, …)`.
- `port/host/headless/main.c`, `port/psyq/crt.{c,h}` -- opciones de `dw1`.
- `tools/port/psyq/stub_returns.tsv` -- quitar `CdControl`/`CdRead`/`CdReadSync`.
- `build/.../generated/dw1_overlay_starts.h` -- direcciones PS1 de cada `<ovl>_START` (varios overlays comparten inicio: identificar por nombre).
- `tools/port/pack/test_pack_disc.sh` -- dejar el Pack en el directorio de build como fixture.

## Tasks & Acceptance

**Execution:**
- [x] `port/host/pack/pack.{c,h}` -- lector del Pack; valida cabecera, versión, SHA del Manifest e índice.
- [x] `port/backend/cdrom.{c,h}` -- Pack abierto, búsqueda por ruta y lectura de sector 2048/2340.
- [x] `port/psyq/libcd.c`, `port/psyq/psyq.h` -- funciones y modelo de tiempo.
- [x] `port/psyq/overlay.{c,h}`, `port/psyq/stub.c` -- id por nombre; `port_trace`.
- [x] `port/host/headless/main.c`, `port/psyq/crt.{c,h}` -- `--pack`, `--until-overlay`.
- [x] `port/psyq/tests/test_libcd.c` -- matriz sobre el Pack sintético de `test_pack` (fixture CTest).
- [x] `tools/port/pack/test_dw1_overlay.sh` -- clase B (skip 77 sin `DW1_DISC_DIR`).
- [x] `CMakeLists.txt`, `tools/port/psyq/stub_returns.tsv`, `tools/port/pack/test_pack_disc.sh` -- wiring y fixtures.
- [x] `docs/port/setup.md` -- correr `dw1` con el Pack.

**Acceptance Criteria:**
- Given los dos presets sin `DW1_DISC_DIR`, when `ctest`, then todo pasa, `frame0` incluido.
- Given `DW1_DISC_DIR`, when `ctest -R 'pack_disc|dw1_overlay'`, then el primer `*_REL.BIN` queda en su `*_START` con el SHA-256 de `us.tsv` y hay una sola llamada a `port_overlay_loaded`.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde
- ídem con `DW1_DISC_DIR=/dw/game_rom/extracted` -- expected: `pack_disc` y `dw1_overlay` pasan
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK

## Implementation Notes

- **Holgura de sector (desvío a revisar):** `cdrom_read_sector` entrega el sector completo con
  ceros tras `size` (como dice la decisión), pero `libcd` solo copia a memoria los bytes de datos
  del archivo (`cdrom_read_sector` devuelve cuántos son); en el disco vacío no escribe nada. Motivo:
  ASan encontró que `readFile(MAPHEAD.SCN)` lee 13 sectores (26624 B) en `MAPHEAD_SCRIPT_BUFFER`
  (25000 B); en la PS1 la holgura cae en `SCRIPT_OFFSET_TABLE`, que se carga justo después, pero en
  nativo pisa otra variable global. Arreglarlo en `src/` estaba fuera de los límites.
  [hipótesis: el Juego nunca usa esos bytes; se valida contra la referencia en 3.10]
- `CdSearchFile` devuelve `fp` (la consola devuelve su entrada de caché; el Juego solo compara
  con NULL y -1) y cobra seek + 1 sector también cuando no encuentra nada o no hay Pack.
- `pack_open` además rechaza archivos que comparten sectores (también `manifest_read`), para que
  la búsqueda por LBA sea unívoca; `us.tsv` no tiene ninguno.
- Sin Pack, el frame 0 pasa del ciclo 565.044 al 127.700.052: cada `CdSearchFile` y cada lectura
  ahora cuestan tiempo de CD.

- Revisión: el caso `Size1` de `overlay_check` no se puede probar en Debug (un `F1` en `Size1`
  aborta antes en `cdrom_read_sector`, por diseño).
- Evidencia: `docs/implementation-artifacts/evidence/story-2-8.log` (`native-headless` 23/23 y
  `native-asan` 21/21 sin disco; `pack_disc` y `dw1_overlay` con el disco en los dos presets; L0a OK).
- Cierra el diferido de la 2.7 sobre `F1` en `CdlModeSize1`: fuera de `MOVIE/*.STR` el Juego lee
  siempre con `CdlModeSpeed` (2048 B).

## Spec Change Log

- **Holgura del último sector (implementación, aprobado en el checkpoint de la Fase 5):** la
  decisión congelada pedía copiar el sector entero con ceros tras `size` (y ceros en el disco
  vacío). ASan mostró que eso desborda `MAPHEAD_SCRIPT_BUFFER` en nativo. Se copia solo la parte
  del archivo y el disco vacío no escribe. Estado malo evitado: corrupción de globals nativos
  vecinos. KEEP: `cdrom_read_sector` sigue entregando el sector completo; la hipótesis queda para
  la 3.10. El bloque congelado no se tocó: lo cambia solo el humano.

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `overlay_check` (restauración, traza, `dest`, `port_overlay_done`) solo lo alcanza `dw1_overlay`, que se salta sin disco (verification gap + blind) | medium | Gap verificado: el Pack sintético no tiene ningún `*_REL.BIN`. **patch**: `\MOV_REL.BIN;1` sintético y casos en `test_libcd` (lectura completa, parcial, con offset y `Size1`). |
| 2 | "Una sola llamada a `port_overlay_loaded`" se verifica en vacío: `--until-overlay` sale en la primera línea | medium | Verificado. **patch**: lo cubre el caso L1 de #1 (dos lecturas completas dan dos líneas; las otras, ninguna). |
| 3 | Regla nueva de `manifest_read` (filas que comparten sectores) sin test | low | Gap verificado. **patch**: caso `build_fails(..., "shares sectors")`. |
| 4 | `! grep` no corta el script con `set -e` | low | Verificado (POSIX). Hoy lo tapa que ASan/UBSan abortan. **patch**: `if grep …; then exit 1; fi`. |
| 5 | `awk -v` procesa los `\` de la ruta: con gawk/busybox el test falla | low | Verificado: en el contenedor hay mawk, que no los procesa. **patch**: `ENVIRON`. |
| 6 | `CdReadSync(0)` desde un handler con la lectura terminada cae en `CLOCK_FATAL` | low | Verificado en `clock_wait_until`. El Juego no lo hace hoy. Corrección directa. **patch**. |
| 7 | `dw1_overlay` sin límite rápido: si la detección falla, corre hasta el `TIMEOUT` de 600 s | low | Verificado. **patch**: `--frames 300` con un mensaje claro. |
| 8 | `port_overlay_done` débil puede quedar NULL si cambia el orden de linkeo | low | `crt.o` siempre entra porque define `port_run_game`. Rechazado. |
| 9 | `test_libcd` depende de un build Debug (las aserciones de abort) | low | Todos los presets son Debug. Rechazado. |
| 10 | `CdSearchFile` deja `pos.track` sin tocar | low | El Juego copia `pos` pero `CdlSetloc` usa solo 3 bytes. Rechazado. |
| 11 | Tamaño ISO de los `F2` y latencia nula de los comandos sin `[hipótesis]` | low | Verificado (regla 8 de CLAUDE.md). **patch**: comentarios. |
| 12 | `off_t` de 32 bits en i686: offsets ≥ 2 GiB | false | `pack.c` define `_FILE_OFFSET_BITS 64` en su primera línea. |
| 13 | El hash del overlay y `port_trace.log` se generan en toda corrida | low | El archivo por defecto ya existía desde la 2.6 (stubs); el hash es una vez por carga. Rechazado. |
| 14 | `sprint-status`/`STATUS.md`/Triage Log incompletos | false | Se completan al cerrar la historia. |
| 15 | `CdRead` con `sectors <= 0` o `CdlModeSize0` en release | low | `Size0` es `PORT_ASSERT` (abort en debug); el Juego no lo usa. Rechazado. |
| 16 | Archivos sobrantes de `test_libcd`; `dw1 --pack` sin archivo sin test; `CdControlB(Setloc)` sin test | low | Directorio de build ignorado; el resto se rechaza por costo/uso. |
