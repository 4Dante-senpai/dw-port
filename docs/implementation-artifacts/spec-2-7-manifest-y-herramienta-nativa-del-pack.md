---
title: 'Historia 2.7: Manifest y herramienta nativa del Pack'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: 'a52cfdf805b3974232ae7dd49a92a2b5bc9c1e63'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El Juego nativo no tiene de dónde leer los archivos del disco y el Pack web (4.3) no
tiene una referencia byte a byte: no existen el Manifest, el parser BIN/CUE + ISO9660 ni el Pack.

**Approach:** Un C compartido en `port/host/pack/` (imagen, ISO9660, Manifest, validación todo o
nada, escritura y verificación del Pack, SHA-256) y una CLI `dw1-pack` (`manifest`, `build`,
`verify`) en `tools/port/pack/`. El Manifest USA se genera del disco del usuario y se versiona.

## Boundaries & Constraints

**Always:** el Manifest y los tests no llevan bytes del disco (solo rutas, LBAs, sectores, modo,
tamaño, SHA-256); el Pack se escribe a un temporal y se renombra solo si todo validó; la salida es
determinista; lectura por sector (nunca la imagen entera en memoria); `-Wall -Wextra -Werror`;
pasa en `native-headless` y `native-asan`.

**Never:** `libcd` ni lectura del Pack desde el Juego (2.8); worker web (4.3); EDC/ECC.

**Decisiones (autónomas):**
- Manifest TSV: `#` comentario, filas `ruta lba sectores modo tamaño sha256` ordenadas por LBA y
  ruta; ruta como la pide el Juego (`\MOVIE\OP1.STR;1`). Fila extra `@exe` = mismos campos que
  `\SLUS_010.32;1`: se valida primero, leyendo por LBA, y si falla el mensaje dice que solo se
  soporta USA (SLUS-01032) y nombra el archivo.
- Modo: `F2` si algún sector del extent tiene el bit Form 2 en el submode; si no, `F1`.
  `F1`: tamaño ISO, datos de usuario de 2048 B. `F2`: `sectores × 2336` (subheader + datos); se
  exige tamaño ISO = `sectores × 2048` para que 2.8 lo derive.
- Pack (LE): cabecera de 64 B (`DW1PACK\0`, `format_version` 1, cantidad, SHA-256 del Manifest,
  offset del índice y de los datos), entradas de 128 B (ruta[64], lba, sectores, modo, tamaño,
  offset, sha256), datos alineados a 2048 en orden de LBA con relleno en cero. El slack del último
  sector F1 no se guarda [hipótesis: el Juego no lo lee; se valida en 2.8].
- Imagen: `.cue` (`TRACK 01 MODE2/2352`, primer `FILE ... BINARY`) o `.bin` crudo; un `.iso` de
  2048 B/sector se detecta por `CD001` en `16 × 2048 + 1` y se rechaza explicando XA/video.
- `.gitignore`: `pack/` pasa a `/pack/` (tapaba `port/host/pack/`); se agrega `*.pack` al
  `.gitignore` y al hook.
- Clase B: test CTest `pack_disc` que sale con 77 (skip) sin `DW1_DISC_DIR`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Disco sintético OK | `.cue` + `.bin` generados por el test | Pack con cabecera, índice y datos alineados en orden de LBA; `verify` OK | N/A |
| Byte cambiado | un byte de cualquier archivo | falla nombrando la ruta | sin Pack ni temporal |
| Exe distinto | otro `SLUS_010.32` | "solo USA (SLUS-01032)" + ruta | sin Pack |
| Modo de pista | `.cue` con `MODE1/2352` | rechazo nombrando el modo | sin Pack |
| ISO 2048 | `.iso` de 2048 B/sector | rechazo explicando XA/video | sin Pack |
| Archivo de más/menos | Manifest sin una fila | falla nombrando la ruta | sin Pack |
| Pack corrupto | un byte cambiado en el Pack | `verify` falla nombrando la ruta | salida ≠ 0 |

</frozen-after-approval>

## Code Map

- `reference/dw2-pc/host/sha256.{c,h}` @ 816ed0d -- adaptar (CC0) a `port/host/pack/sha256.{c,h}`.
- `reference/dw2-pc/host/pakbuild.c:164-310` -- referencia de parseo de `.cue` y detección de ISO 2048; no copiar SDL.
- `src/main/file_queue.c:130`, `src/mov/mov.c:48` -- forma de ruta `\DIR\NAME;1` que pasa el Juego a `CdSearchFile`.
- `port/psyq/port.h` -- `PORT_ASSERT` (solo invariantes; los errores de entrada devuelven mensaje).
- `CMakeLists.txt:122-162` -- patrón de librería + test L1; `CMakePresets.json` sin cambios.
- `.gitignore:28`, `tools/port/hooks/pre-commit` -- patrones de datos prohibidos.
- `disks/us/us.xml` -- 818 archivos, 4 `mixed` (`MOVIE/*.STR`, tamaño múltiplo de 2336).
- `game_rom/Digimon World.7z` -- la imagen cruda; se extrae a `game_rom/extracted/` (ignorado) como en `docs/port/setup.md` paso 3.

## Tasks & Acceptance

**Execution:**
- [x] `.gitignore`, `tools/port/hooks/pre-commit` -- `/pack/`, `*.pack`.
- [x] `port/host/pack/sha256.{c,h}` -- SHA-256 adaptado de DW2-PC.
- [x] `port/host/pack/pack.{c,h}` -- imagen, ISO9660, Manifest (leer/escribir), `pack_build`, `pack_verify`; errores en un buffer.
- [x] `tools/port/pack/dw1_pack.c` -- CLI `manifest <img> <tsv>`, `build <img> <tsv> <pack>`, `verify <pack> <tsv>`; salida 0/1/2.
- [x] `port/host/pack/tests/test_pack.c` -- disco sintético y todos los casos de la matriz, vector FIPS de SHA-256 y Pack idéntico en dos construcciones.
- [x] `tools/port/pack/test_pack_disc.sh` -- clase B: `manifest` igual a `us.tsv`, `build` dos veces con el mismo SHA-256, `verify`.
- [x] `CMakeLists.txt` -- `dw1_pack_lib`, `dw1-pack`, tests `pack` y `pack_disc` (`SKIP_RETURN_CODE 77`).
- [x] `tools/port/manifest/us.tsv` -- generado del disco USA.
- [x] `docs/port/setup.md` -- cómo generar y validar el Pack.

**Acceptance Criteria:**
- Given la imagen USA en `DW1_DISC_DIR`, when `ctest -R pack_disc`, then pasa, `us.tsv` tiene 818 filas + `@exe` y el hook pre-commit lo acepta.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde (`pack_disc` skip)
- ídem con `DW1_DISC_DIR=/dw/game_rom/extracted` en `native-headless` -- expected: `pack_disc` pasa
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK

## Implementation Notes

- `us.tsv`: 818 filas + `@exe` (LBA 148338, 330 sectores); 4 `F2` (`MOVIE/*.STR`). Las 818
  SHA-256 y tamaños coinciden con la extracción independiente de `dumpsxiso` en `disks/us/`.
- Pack USA: SHA-256 `c01e5c3680ffdf50d0342f16ec978c6d107a33529688dae6a817bd63947f0cf6`, idéntico en
  dos construcciones (`pack_disc` pasa en `native-headless` y `native-asan`, ~35 s).
- Cada sector leído se valida (sync, dirección MSF = LBA + 150, modo 2); sin `INDEX` del `.cue`.
- Evidencia: `docs/implementation-artifacts/evidence/story-2-7.log` (`native-headless` 20/20 y
  `native-asan` 18/18 con `pack_disc` en skip; `pack_disc` con el disco en los dos presets; L0a OK).
- El Pack no lleva relleno después del último archivo; `verify` exige relleno en cero y nada más
  allá del final.

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `pack_verify`: cabecera, relleno, truncado y bytes al final sin test (verification gap) | medium | Gap verificado por el revisor. **patch**: cuatro casos nuevos en `test_pack.c`. |
| 2 | Resolución del `FILE` del `.cue` relativo a su carpeta y con espacios solo la prueba `pack_disc` (skip) | medium | Gap verificado. **patch**: caso `pack_sub/pack t.cue`. |
| 3 | Hook pre-commit y `.gitignore` sin test | medium | Gap verificado; el hook no tenía test antes de esta historia. **defer**: historia propia para todos los patrones del hook. |
| 4 | `us.tsv` fuera del diff revisado; "el hook lo acepta" sin evidencia | false | Lo excluí del diff por tamaño (solo hashes); el commit corre el hook sobre él y la evidencia lo registra. |
| 5 | Sin `.gitattributes`: `us.tsv` con CRLF rompe `parse_sha` y cambia el SHA-256 del Manifest | low | Verificado: no existe `.gitattributes`. **patch**: `tools/port/manifest/*.tsv text eol=lf`. |
| 6 | `pack_disc` no afirma el SHA-256 conocido del Pack | medium | Verificado: solo compara dos builds entre sí. **patch**: SHA esperado en el script. |
| 7 | El SHA-256 de `F2` incluye el EDC de Form 2; un dump con EDC en cero se rechazaría | maybe-false | AD-11 define `F2` como los 2336 bytes crudos. Se resuelve con un segundo dump de otra herramienta. **defer** (medium sin verificar). |
| 8 | `F1` sin subheader/EDC: una lectura `CdlModeSize1` (2340 B) de un archivo F1 no se puede servir | maybe-false | Para `F2`, 4 + 2336 = 2340 sí alcanza. Falta saber si el Juego lee archivos F1 en `Size1`. **defer** a la 2.8 (medium sin verificar). |
| 9 | `parse_row` no acota `lba + sectores` (desborde en `scan`) | low | Verificado: `e->lba + i` puede dar la vuelta; el hash luego falla con un mensaje engañoso. **patch**: `lba + sectores <= MAX_SECTORS`. |
| 10 | `@exe` no se compara con la fila `\SLUS_010.32;1` | low | Real pero solo con un Manifest editado a mano; el guard agrega una rama. Rechazado. |
| 11 | `PORT_ASSERT(pos == end)` solo loguea en release y el Pack se renombra igual | low | El único camino alcanzable es un Manifest sin filas (#12); con ese guard queda como invariante. Agrupado con #12. |
| 12 | Manifest solo con `@exe` (`m.n == 0`): `PORT_ASSERT` aborta en build/verify | low | Verificado: `layout` da `end = 2048` y `pos = 64`. **patch**: `manifest_read` rechaza un Manifest sin filas, con test. |
| 13 | `pack_manifest` no detecta un error de escritura de `fprintf` y escribe en el lugar | low | Verificado: solo se revisa `fclose`. **patch**: `ferror` antes de `fclose`. El temporal + rename no hace falta: `us.tsv` se recupera con git. |
| 14 | `remove(tmp)` en fallos tempranos borra un `<pack>.tmp` ajeno | low | Real, pero el nombre es propio del tool y builds concurrentes al mismo destino no son un uso real. Rechazado. |
| 15 | Comentario de `pack.h` ("nothing is left at pack") inexacto si ya había un Pack | low | Verificado. **patch**: comentario corregido. |
| 16 | Caminos de error sin test (imagen truncada, sync, `.cue` sin `FILE`, Manifest desordenado, F2 con tamaño ISO raro) | low | Mensajes reales sin test; los de impacto (#1, #2, #12) se cubren. El resto, rechazado por costo/uso. |
| 17 | `.cue` con `\`, pregap o pistas 02+ | low | La pista 1 de datos de un disco PS1 empieza en `00:00:00`; el `.cue` USA tiene una sola pista. Rechazado. |
| 18 | `STATUS.md`/`sprint-status.yaml` sin cerrar | false | Se actualizan en el paso de cierre de la historia. |
| 19 | `pack_disc` con `DW1_DISC_DIR` sin `.cue` falla con un mensaje confuso; varios `.cue` | low | Verificado. **patch**: mensaje explícito. Fallar (no skip) es correcto con una configuración mala. |
| 20 | `test_pack` deja `pack_t.*` en el directorio de build | low | Directorio de build ignorado. Rechazado. |
| 21 | `buf` de 64 KiB en el stack de `pack_verify`; la librería también compila para Emscripten (stack 64 KiB) | medium | Verificado: `dw1_pack_lib` está fuera del `if(NOT EMSCRIPTEN)`. **patch**: `static`. |
| 22 | `<out>.pack.tmp` de un build interrumpido no está ignorado ni lo rechaza el hook | medium | Verificado: `*.pack` no cubre `.pack.tmp`. **patch**: `.gitignore` y hook. |
