---
title: 'Historia 2.11: libgte y tablas leídas del exe en el Pack'
type: 'feature'
created: '2026-10-15'
status: 'done'
baseline_commit: '56dfa8bb321c21bb0011fc6b519cdd766a29e067'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** las funciones de `libgte` que llama el Juego son stubs generados: matrices, senos y
proyecciones devuelven basura y la geometría no puede coincidir con la consola. Sus tablas
(`rcossin_tbl`, `ratan_tbl`, …) son datos de Sony que no pueden entrar al repo.

**Approach:** traducir del asm de DW1 las 29 funciones de `libgte` que el Juego llama (más sus
internas) a `port/psyq/libgte.c` sobre el GTE del Backend (2.10), y cargar sus tablas desde
`\SLUS_010.32;1` del Pack al arrancar, con offsets generados desde `config/us/`.

## Boundaries & Constraints

**Always:** una función por entrada con comentario `asm:asm/us/main/psyq/libgte.s` (o
`dw2-pc:` + cabecera CC0 si se adapta de DW2-PC y coincide con el asm); cada entrada llama a
`clock_psyq_entry()`; las instrucciones cop2 van por la API `gte_*` de `port/backend/gte.h`;
`-Wall -Wextra -Werror` limpio en `native-headless`, `native-asan` y `web`; offsets y tamaños de
tablas generados en build (como `dw1_rts_info.h`), nunca escritos a mano; sin cambios en
`src/`/`include/`.

**Never:** `.incbin`, bytes de tablas, ni strings del exe en archivos del repo; las 27
`GsTMDfast*`/`GsTMDdiv*` (splat las cuenta en `libgte`, pero son de `libgs`: Historia 2.13);
`InitGeom`/`_patch_gte` (los llama `GsInitGraph`, 2.13); funciones de `libgte` que el Juego no
llama; modelar comandos GTE nuevos salvo que una de las 29 lo emita (entonces se agrega con test).

**Decisiones (autónomas):**
- Alcance = las 29 de `docs/research/psyq-usage.md` § libgte con llamadas > 0 (`ApplyMatrixSV` …
  `RotTransPers3`); "56" del epic incluye las 27 `GsTMD*` de la 2.13.
- Tablas cargadas = las de solo lectura que usan esas funciones: `rsin_tbl` (rsin/rcos vía
  `sin_1`), `SQRT` (`SquareRoot0`), `rcossin_tbl` (`RotMatrix*`), `ratan_tbl` (`ratan2`).
  Tamaño = distancia al símbolo siguiente en `config/us/symbols.txt`; offset en el exe =
  `addr - vram + start` del segmento `main` de `config/us/main.yaml`.
- `MSTACK`/`MSP` (pila de 20 matrices) y los `*_SAVERA` son estado de ejecución: estáticos en C.
  Desborde/vaciado de la pila: mismo efecto que el asm (no apila / no desapila) con un mensaje
  propio en inglés por `printf`.
- Sin Pack, las tablas quedan en cero y el arranque sigue (`frame0` de clase A no las usa).
  Con Pack, la carga ocurre en el Host justo después de `cdrom_open`; si falla, `dw1` sale con 1.
- Esperados de los tests L1: calculados aparte con un script de una sola vez desde la semántica
  del asm, con tablas sintéticas, y escritos como constantes (como 2.9/2.10).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tablas desde el Pack | Pack USA real (clase B) | SHA-256 de cada tabla cargada = SHA-256 de su región del exe leída del `.bin` del disco por un camino independiente | N/A |
| Sin Pack | `dw1` sin `--pack` | tablas en cero; `frame0` pasa | N/A |
| Exe ausente o corto | Pack sin `@exe` o región fuera de tamaño | `dw1` sale con 1 y mensaje | error del Host |
| Ángulos | `rsin`/`rcos` con 0, 1024, 4095, 4096, negativos y > 4096 | índice con máscara y signo como el asm | N/A |
| `ratan2` | cuadrantes, x=0, y=0, ambos 0, \|y\|>\|x\| | ángulo del asm | N/A |
| `RotMatrix*` | ángulos de los tres ejes, negativos | matriz del asm con tablas sintéticas | N/A |
| `SquareRoot0` | 0, 1, potencias de 2, `0x7FFFFFFF` | resultado del asm (LZC + `SQRT`) | N/A |
| Pila de matrices | 21 `PushMatrix`; `PopMatrix` vacía | no desborda, mensaje propio, registros GTE como el asm | N/A |
| GTE | `RotTransPers*`, `RotNclip*`, `NormalColorCol*`, `ApplyMatrix*`, `MulMatrix*`, `CompMatrix` | salidas y valor de retorno (OTZ/flag) del asm sobre el modelo 2.10 | N/A |

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libgte.s` -- fuente de verdad (generado por `make regenerate`, no versionado; está en el checkout). Internas: `sin_1`.
- `config/us/symbols.txt:1926-1937` -- `rsin_tbl` 0x80116CDC, `SQRT` 0x801174EC, `MSP`, `MSTACK`, `rcossin_tbl` 0x80117960, `ratan_tbl` 0x8011B960, siguiente `SKELETON_HIRO` 0x8011C170.
- `config/us/main.yaml:57-60` -- segmento `main`: `start: 0x800`, `vram: 0x80090800`.
- `tools/port/units/rts_info.py` + `CMakeLists.txt:85-91` -- patrón de header generado + test Python a copiar.
- `port/backend/gte.h` -- API `gte_mtc2/mfc2/ctc2/cfc2/gte_command`; `port/backend/tests/test_gte.c` -- estilo de tests.
- `port/psyq/libgs.c` -- estilo de entrada PsyQ (`clock_psyq_entry`, comentario `asm:`).
- `port/psyq/psyq.h` + `port/psyq/tests/psyq_prototypes.c` -- prototipos del port contra `mw_lib41/libgte.h`.
- `port/backend/cdrom.{h,c}` -- `cdrom_find_path(PACK_EXE_PATH)`; agregar lectura por offset de un archivo (`pack_read`).
- `port/host/headless/main.c` -- tras `cdrom_open`, llamar a la carga de tablas.
- `tools/port/psyq/gen_stubs.py` -- detecta implementadas; esperado `stubs=140 gte_stubs=0`.
- `tools/port/pack/test_dw1_overlay.sh` + `CMakeLists.txt:296-304` -- patrón clase B con fixture `disc_pack`.
- `tools/port/hooks/pre-commit` -- agregar rechazo de `incbin` en archivos staged de `port/` y `tools/port/`.
- `reference/dw2-pc/psyq/` -- funciones con "En DW2-PC: sí" en psyq-usage; reusar solo si coinciden con el asm.

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/psyq/gte_tables.py` + test Python -- genera `dw1_gte_tables.h` (offset en el exe y tamaño de las 4 tablas); test con YAML/símbolos sintéticos.
- [x] `port/psyq/libgte.c` -- 29 funciones + `sin_1`, pila de matrices, tablas en arrays estáticos, `libgte_load_tables()` y un setter para tests.
- [x] `port/backend/cdrom.{h,c}` -- lectura de un archivo del Pack por offset.
- [x] `port/host/headless/main.c` -- carga de tablas tras `cdrom_open`; imprime `port: libgte table <nombre> sha256 <hex>` por tabla.
- [x] `port/psyq/tests/test_libgte.c` -- matriz de la I/O con tablas sintéticas.
- [x] `tools/port/psyq/test_gte_tables_disc.sh` -- clase B: SHA de las regiones leídas del `.bin` (LBA de `@exe` en `us.tsv`, sectores raw) contra las líneas de `dw1 --pack`.
- [x] `CMakeLists.txt` -- generador, `libgte.c` en `dw1_psyq`, tests `gte_tables`, `libgte`, `gte_tables_disc` (fixture `disc_pack`).
- [x] `tools/port/hooks/pre-commit` -- rechazo de `incbin`.
- [x] `docs/STATUS.md` (métrica PsyQ implementadas/stubs) y `docs/implementation-artifacts/evidence/story-2-11.log`.

**Acceptance Criteria:**
- Given `native-headless` y `native-asan` sin `DW1_DISC_DIR`, when `ctest`, then todo pasa (`libgte`, `gte_tables`, `gte`, `frame0` incluidos) y gen_stubs imprime `stubs=140 gte_stubs=0`.
- Given `DW1_DISC_DIR`, when `ctest -R 'pack_disc|dw1_overlay|gte_tables_disc'`, then pasan en ambos presets nativos.
- Given el preset `web`, when se construye, then compila sin errores.
- Given el repo, when `grep -rn incbin port/ tools/port/` (excluido el hook), then no hay resultados, y el hook rechaza un archivo staged con `.incbin`.
- Given L0a, when `preproc_check.sh`, then OK.

## Implementation Notes

- `port/psyq/libgte.c`: 29 entradas + `sin_1`, pila de 20 matrices estática, tablas en arrays
  estáticos con `libgte_load_tables`/`libgte_set_table`/`libgte_table` (`port/psyq/libgte.h`);
  aritmética de 32 bits con desplazamientos sobre `uint32_t` para no depender de UB.
- `cdrom_read_file` sobre `pack_read`; el Host imprime el SHA-256 de cada tabla y sale con 1 si
  falta el exe o es corto (test `libgte_host` con el Pack sintético y el digest del Manifest).
- Desvío menor: el preset `web` ahora también compila `dw1_psyq` (y `PadIdentifier` en
  `libetc.c` lleva `unused` para compilar limpio con emcc); `dw1` compila con `-Werror`.
- Hook pre-commit: rechaza `incbin` en archivos staged de `port/` y `tools/port/`; probado con
  un índice Git temporal (evidencia).

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `gte_tables_disc` no corre en CI: `native-disc` filtra `pack_disc\|dw1_overlay` (blind + verification gap) | medium | Verificado en `.gitlab-ci.yml:65`. **patch**: filtro con `gte_tables_disc`. |
| 2 | El rechazo de `incbin` del hook no tiene test de regresión (verification gap) | medium | Pre-verificado: solo una corrida manual en la evidencia. **patch**: `tools/port/hooks/test_pre_commit.sh` en CTest (`pre_commit`): rechaza en `port/` y `tools/port/`, acepta limpio. |
| 3 | Con `NDEBUG`, `PORT_ASSERT` no frena `ratan2`/`SquareRoot0` (blind, edge ×2) | false | Todos los presets son Debug; mismo criterio que 2.8 (#9), 2.9 (#8) y 2.10 (#4). |
| 4 | Tablas corruptas del mismo tamaño se aceptan: `pack_open` no verifica datos (blind) | low | El Pack solo se escribe tras verificar todo el Manifest (`pack_build`); corrupción posterior del archivo afecta a todo `libcd` por igual. Rechazado. |
| 5 | `gte_tables.py` no valida límite superior ni tamaño mínimo por tabla (blind) | low | Las regiones salen de `symbols.txt` del matching; la clase B compara los cuatro hashes. Agrega ramas. Rechazado. |
| 6 | El test de disco repite la fórmula de offsets del generador (blind) | low | Regiones contrastadas a mano con los `dlabel` de `libgte.data.s` (0x86CDC/0x874EC/0x87960/0x8B960) durante la planificación. Rechazado. |
| 7 | Falta prueba de clase A de carga exitosa (blind) | low | La carga exitosa la cubre `gte_tables_disc`, ahora en CI (#1); un EXE sintético agrega fixture. Rechazado. |
| 8 | Las pruebas de error del Host fallan siempre en `rsin_tbl` (blind) | low | El borrado de tablas tras fallo es una ruta única en `libgte_load_tables`. Rechazado. |
| 9 | El script de esperados no se versiona (blind) | false | Decisión del spec (como 2.9/2.10): script de una sola vez; método y tablas sintéticas en la evidencia. |
| 10 | Rotaciones sin índices 1024/2048/3072/4095 de `rcossin_tbl` (blind) | low | Requiere vectores nuevos; el enmascarado está cubierto por `rsin`/`rcos`. Rechazado. |
| 11 | STATUS.md da la historia por hecha durante la revisión (blind) | low | Queda correcto al mergear (paso 5). Rechazado. |
| 12 | El hook no reconoce rutas Unicode citadas por `git diff --numstat` (edge) | low | Patrón preexistente de todo el hook; no hay rutas no ASCII en `port/`. Rechazado. |


## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && env -u DW1_DISC_DIR ctest --preset $p; done'` -- expected: verde, `stubs=140 gte_stubs=0`
- ídem con `DW1_DISC_DIR=/dw/game_rom/extracted` y `-R 'pack_disc|dw1_overlay|gte_tables_disc'` -- expected: 3/3 sin skips
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web'` -- expected: exit 0
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK
