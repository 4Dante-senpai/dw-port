# Estado del proyecto — DW1 Web Port

Última actualización: 2026-10-16

## Modo de operación

- Compilador matching (`cc_mips.dll`): **DISPONIBLE** desde 2026-10-12 (CodeWarrior for PlayStation
  Release 4, `bin/cc_mips/cc_mips_40.dll`). `make` + `make compare` obligatorios en cada merge
  (regla 2 de `CLAUDE.md`); se corren en la imagen `dw-build` (`docs/port/setup.md`, paso 7).

## Hito actual

- M0 Entorno: **completo** (modo sin compilador matching).
- M1 Inventario: **completo** (`docs/research/`).

## Hecho

- Fase 0: repositorio inicializado desde `jype0/dw_decomp`, remotes configurados
  (`origin` = fork propio, `upstream` con push deshabilitado), `reference/dw2-pc` como submódulo,
  `.gitignore` extendido y hook pre-commit (`tools/port/hooks/`).

- Fase 1: imagen `dw-port` (emsdk 6.0.10, SDL3 3.4.16, Clang 18, CMake, Ninja); disco volcado
  (`SLUS_010.32` sha1 OK); `make regenerate` OK; `make TOOLCHAIN=gcc` OK; smoke test SDL3 nativo y
  web OK. Procedimiento en `docs/port/setup.md`.
- Fase 2: investigación en `docs/research/` (decomp-status, overlays, psyq-usage,
  hardware-access, dw2pc-architecture, web-constraints, risks) y scripts de métricas en
  `tools/port/metrics/`.

- Fase 3: BMAD 6.12.2 instalado (`docs/port/bmad.md`).
- Fase 4.1: product brief (`docs/planning-artifacts/briefs/brief-dw-port-2026-10-11/brief.md`).
- Fase 4.2: PRD aprobado por el dueño el 2026-10-11 con sus supuestos (speed-up queda para v2).

- Fase 4.3: arquitectura aprobada el 2026-10-11
  (`docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md`,
  AD-1..AD-18). AD-18 opción A: NFR-4 enmendado en el PRD. CI en el GitLab propio (AD-16).

- Fase 4.4: épicas e historias (`docs/planning-artifacts/epics.md`, 57 historias; Épica 2 = M2 …
  Épica 7 = M7, Épica 8 continua de decomp/upstream/CI).
- Fase 4.5: sprint planning (`docs/implementation-artifacts/sprint-status.yaml`, todo en `backlog`).

- Fase 5, Historia 2.1 (esqueleto del build): `CMakeLists.txt` + preset `native-headless`; 119 TUs del
  Juego compilan a objetos i686 (`_psstart.c` reemplazado por el crt del port); lista de TUs
  generada (`tools/port/units/tu_list.py`); `char` con signo verificado en el asm original
  (AD-17 enmendado, `docs/port/char-signedness.md`). Evidencia:
  `docs/implementation-artifacts/evidence/story-2-1.log`.

- Historia 2.2 (L0a): `tools/port/ci/preproc_check.sh` compara el preprocesado de los 120 TUs USA
  contra `upstream/main` en las configuraciones gcc y mwcc; inventarios versionados
  `tools/port/metrics/ps1_literals.tsv` (58 filas, 27 a traducir en 2.4) e `isr_spins.tsv` (67
  filas clasificadas, 1 `port_yield`: `MOV_waitForFlip`). CTest 4/4. Evidencia:
  `docs/implementation-artifacts/evidence/story-2-2.log`.

- Historia 2.3 (memoria y overlays): `port/psyq/ps1mem` (arena 2 MB, scratchpad 1 KB, `PS1_RAM`,
  enlaces de OT de 24 bits en una ventana de 16 MB desde el segmento de datos), `port/psyq/overlay` (copia
  y restauración de `dw1ovl_<ovl>`), TUs envoltorio por overlay (15 secciones en USA), preset
  `native-asan`. Evidencia: `docs/implementation-artifacts/evidence/story-2-3.log`.

- Historia 2.4 (traducción de direcciones): 12 bloques `DW1_PORT` (27 literales + los 15
  `<ovl>_START`) con `PS1_RAM_ADDR()` constante; `ps1_literals.tsv` sin pendientes y con chequeo de
  pares rama port / rama PS1; `loadMMD` usa offsets relativos. Evidencia:
  `docs/implementation-artifacts/evidence/story-2-4.log`.

- Historia 2.5 (reloj virtual): `port/psyq/clock.c` (ciclos a 33.868.800 Hz, VBlank sin deriva,
  cola de eventos determinista, interrupciones enmascaradas en handlers), `VSync`/`VSyncCallback(s)`/
  `InterruptCallback`/`ResetCallback` y `SetRCnt`/`GetRCnt`/`ResetRCnt` traducidos del asm de DW1.
  Evidencia: `docs/implementation-artifacts/evidence/story-2-5.log`.

- Historia 2.6 (linkeo y frame 0): ejecutable `dw1` (`native-headless`) con los 119 objetos del
  Juego, la Capa PsyQ del port y stubs generados en build (182 PsyQ + 11 GTE); **llega al frame 0**
  (retorno del primer `VSync`, ciclo 565.044) también bajo ASan/UBSan. ASan encontró un string sin
  terminador en `src/main/efe.c` (corregido con `DW1_PORT`). Evidencia:
  `docs/implementation-artifacts/evidence/story-2-6.log`.

- Historia 2.7 (Manifest y Pack): `port/host/pack/` (BIN/CUE, ISO9660, Manifest, Pack todo o nada,
  `verify`, SHA-256 adaptado de DW2-PC) y la CLI `dw1-pack`; `tools/port/manifest/us.tsv` con 818
  archivos + `@exe` (4 `F2`: `MOVIE/*.STR`), coincide con la extracción de `dumpsxiso`. Pack USA
  determinista (SHA-256 `c01e5c36…0cf6`). Test clase B `pack_disc` (skip sin `DW1_DISC_DIR`).
  Evidencia: `docs/implementation-artifacts/evidence/story-2-7.log`.

- Historia 2.8 (`libcd` sobre el Pack): lector del Pack, `port/backend/cdrom` (sectores de 2048 y
  2340 B con MSF reconstruido) y `port/psyq/libcd.c` (`CdSearchFile`, `CdRead`, `CdReadSync`,
  `CdControl*`, `CdSync`, `CdReadCallback`, `CdInit`, `CdIntToPos`/`CdPosToInt`) sobre el reloj
  virtual (seek y sector a 75/150 sectores/s, hipótesis a calibrar en 3.10). `dw1 --pack` carga
  `MOV_REL.BIN` en `mov_START` con el SHA-256 del Manifest y restaura sus datos una vez. Desvío
  anotado: solo se copian los bytes del archivo del último sector (ASan: `MAPHEAD_SCRIPT_BUFFER`).
  Evidencia: `docs/implementation-artifacts/evidence/story-2-8.log`.

- Build matching: `cc_mips.dll` de la Release 4 provista por el dueño; build limpio de los 120 TUs
  con los 13 bloques `DW1_PORT` actuales y `make compare` → `Compiled binaries match with target
  binaries`.

- CI (AD-16): `origin` empuja a GitHub y al GitLab propio (segunda push URL; hosts en
  `docs/port/local-infra.md`, no versionado). Primer pipeline verde en `a8d2a2b` (runner con tag
  `dw1-data`): `native`
  (23/23, 2 skips de clase B), `native-disc` (`pack_disc`, `dw1_overlay`) y `matching`
  (`Compiled binaries match with target binaries`). Disco y `cc_mips_40.dll` en
  `/srv/dw-port-private` del runner, montados como `/private:ro` (`docs/port/setup.md`).
  Imágenes `dw-build`/`dw-port` publicadas en el registry del proyecto por el job `images` y
  fijadas por digest en `.gitlab-ci.yml`. AD-16 enmendado: dos push URL en `origin` y runner
  `dw1-data` sin *protected* (desvío aprobado por el dueño el 2026-10-12).

- Historia 2.9 (`libc`, `libapi`, `libetc` deterministas): `rand`/`srand` con el LCG del BIOS
  (A(2Fh)/A(30h); semilla de arranque `0x24040001` como hipótesis), `InitHeap3`/`malloc3`/`free3`
  traducidos del asm (direcciones PS1 verificadas en L1) y `PadInit`/`PadRead` sin input del host.
  `gen_stubs.py` cuenta la `libc` del port como implementada (con test). Cadenas, `mem*` y
  `sprintf` siguen en la libc del host (hipótesis para la Épica 3). Evidencia:
  `docs/implementation-artifacts/evidence/story-2-9.log`.

- Historia 2.10 (modelo GTE y macros inline, cerrada): `port/backend/gte` adapta
  el modelo de DW2-PC e implementa RTPS/RTPT, NCLIP, AVSZ3/4, MVMVA y NCCS/NCCT; las once
  macros dejan de ser stubs (`gte_stubs=0`). Test L1 con vectores propios, memoria sin alinear,
  saturaciones/FIFOs/flags, abortos y reloj sin avance. Preset `web`: 119 TUs del Juego y
  Backend compilados con emcc, sin linkear el ejecutable. Suites nativas, ASan/UBSan, L0a,
  `frame0`, `pack_disc` y `dw1_overlay` verdes. Sin cambios en `src/`/`include/`. Revisión
  (3 capas Codex): `gen_stubs` falla si quedaría un stub `port_gte_*`, job de CI `web-objects`
  y test MVMVA reforzado. Evidencia: `docs/implementation-artifacts/evidence/story-2-10.log`.

- Historia 2.11 (`libgte` y tablas del EXE): 29 entradas traducidas del asm USA sobre el
  Backend GTE, con pila de 20 matrices y aritmética de 32 bits; offsets y tamaños generados
  desde `config/us/`. El Host carga `rsin_tbl`, `SQRT`, `rcossin_tbl` y `ratan_tbl` del Pack,
  publica sus SHA-256 y rechaza un EXE ausente/corto. Tests con tablas sintéticas, valores
  esperados calculados aparte y comparación de hashes contra sectores raw del disco.
  `native-headless`, ASan/UBSan, web (ahora incluye PsyQ), L0a y clase B verdes; hook contra
  inclusión binaria con test `pre_commit` en CTest. Revisión (3 capas Codex): `gte_tables_disc`
  agregado al job `native-disc` del CI y test del hook; 10 hallazgos rechazados. Sin cambios en `src/`/`include/` ni
  datos del disco añadidos. Evidencia: `docs/implementation-artifacts/evidence/story-2-11.log`.

- Historia 2.12 (renderer software y `libgpu`): Backend GPU con VRAM privada 1024×512,
  primitivas planas/Gouraud/texturadas, CLUT, dithering, mezcla, máscaras y transferencias;
  adaptación CC0 de DW2-PC sin dependencias de presentación. Las 28 funciones usadas de
  `libgpu`, más `DrawOTag`/`ClearOTagR`, implementan paquetes, TIM y listas con enlaces PS1.
  `DrawSync` espera el evento de fin en el reloj virtual; los costes por palabra/píxel y
  las transferencias inmediatas quedan como hipótesis a calibrar en 3.10. Tests de píxeles
  calculados aparte y 30 SHA-256 de VRAM, ABI (tamaños y offsets), OT y ciclos verdes; suites completas nativa
  (33, 3 omitidas sin disco) y ASan/UBSan (31, 3 omitidas); con disco 3/3, incluido
  `frame0`; build web con `-Werror` y L0a verdes. Sin cambios en `src/`/`include/`.
  Evidencia: `docs/implementation-artifacts/evidence/story-2-12.log`.

- Historia 2.13 (`libgs` con el pipeline TMD): las 35 entradas usadas de `libgs` y las
  27 `GsTMDfast*`/`GsTMDdiv*` están implementadas desde el asm USA. Estado global tipado,
  matrices y luces, mapeo y agrupación TMD, culling, subdivisión, OT, primitivas 2D y doble
  buffer; enlaces mediante `PS1_LINK`/`PS1_LINK_PTR`. Test L1 de las 27 entradas con campos
  calculados aparte y 27 SHA-256 de paquetes/OT (enlaces normalizados para nativo y ASan),
  subdivisión a dos niveles, ABI, utilidades y VRAM. Suites sin disco: nativo 34 pruebas y
  ASan/UBSan 32, con tres omisiones por suite; las tres pruebas de disco pasan tanto en nativo como en ASan/UBSan.
  Revisión: 1 corrección (las `GsTMDdiv*` descartaban caras por FLAG; el asm recorta en `RCpoly*`)
  y 4 tests agregados. Build web, L0a y `make compare` verdes. Sin cambios en `src/`/`include/` ni en el Backend. Se conserva
  `[hypothesis]` sobre la llegada de los TMD del disco sin mapear; la paridad de escenas
  contra emulador corresponde a M3. Evidencia: `docs/implementation-artifacts/evidence/story-2-13.log`.

## En curso

- Fase 5: próxima historia 2.14 (stubs con comportamiento definido para audio, video y Memory card).

## Bloqueos

- Emulador de referencia (PCSX-Redux / DuckStation headless) no instalado: planificado como
  historia de M3.

## Métricas

| Métrica | Valor |
|---|---|
| Funciones del juego en C / total (USA) | 2741 / 2741 |
| % descompilado del juego (bytes) | 100 % (931.800 B); PsyQ en asm: 612 funciones, 144.348 B (se reemplaza) |
| Funciones PsyQ usadas / cubiertas por DW2-PC | 202 / 81 |
| Archivos C del juego que compilan a wasm32 en el preset `web` | 119 / 119 (+ `_psstart.c` reemplazado) |
| Archivos C del juego que compilan a i686 en el build del port | 119 / 119 (+ `_psstart.c` reemplazado) |
| Bloques `DW1_PORT` en `src/`/`include/` | 13 (`docs/port/src-changes.md`) |
| Funciones PsyQ usadas: implementadas / stubs / libc del host | 135 / 52 / 12 (de 199) |
| Macros inline GTE implementadas / stubs | 11 / 0 |
| Archivos del disco en el Manifest USA | 818 (+ `@exe`) |
| Escenas que funcionan (nativo) | 0 |
| Escenas que funcionan (web) | 0 |

## Notas

- Pregunta abierta (memlog): llamadas por puntero con tipo distinto en el Juego; en wasm abortan. Se
  resuelven en la Historia 4.1.

- Pregunta abierta (memlog de la arquitectura): el VBlank del reloj usa 59,94 Hz (NFR-1); psx-spx da
  ~59,83 Hz para NTSC no entrelazado. Se mide en la Historia 3.10 contra el Emulador de referencia.

- El contexto Docker por defecto de esta máquina (`docker-server`) apunta a un host remoto; los
  comandos del port usan `DOCKER_CONTEXT=default` (daemon local) para que los bind mounts vean el
  repo. Durante la Fase 1 se lanzó por error un primer build contra el host remoto
  (`<docker-server>`): quedó la imagen intermedia `dw-build` y `ubuntu:noble` en ese servidor.

- `origin` tenía una rama `master` previa con solo `LICENSE` (historia no relacionada); se dejó
  intacta y el trabajo se publica en `origin/main`.
