# Estado del proyecto — DW1 Web Port

Última actualización: 2026-10-17

## Modo de operación

- Compilador matching (`cc_mips.dll`): **DISPONIBLE** desde 2026-10-12 (CodeWarrior for PlayStation
  Release 4, `bin/cc_mips/cc_mips_40.dll`). `make` + `make compare` obligatorios en cada merge
  (regla 2 de `CLAUDE.md`); se corren en la imagen `dw-build` (`docs/port/setup.md`, paso 7).

## Hito actual

- M0 Entorno: **completo** (modo sin compilador matching).
- M1 Inventario: **completo** (`docs/research/`).
- M2 Nativo headless: **completo** (título determinista, sin referencia).
- M3 Paridad nativa: **en curso** (3.1 referencia PCSX-Redux/OpenBIOS del título).
- M4 Web: **en curso** (4.1 frame 0 con WebGL2; 4.2 título web idéntico a nativo).

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

- Historia 2.14 (stubs con comportamiento definido): las 52 entradas restantes
  tienen firmas PsyQ verificadas, avance del reloj y comentario `stub-m2:`. Audio
  silencioso con retornos de éxito; tarjeta ausente con comandos consumidos sin
  espera; video sintético negro con callbacks iterativos sin recursión. El test L1
  `av_stubs` enlaza el overlay `mov` real sobre el Pack sintético: película completada,
  20 slices negros y 8 VBlanks. `frame0` conserva su evidencia mediante una traza
  explícita al alcanzar el límite del Host. Suites sin disco verdes: nativo 32
  aprobadas + 3 omitidas, ASan/UBSan 30 aprobadas + 3 omitidas; web y L0a verdes.
  Las 52 entradas y sus reemplazos en Épica 3 están en `docs/port/stubs.md`, incluida
  la limitación previa de `DRAWENV` sin inicializar en `MOV_playMovie`. Sin cambios
  en `src/`/`include/`. Evidencia: `docs/implementation-artifacts/evidence/story-2-14.log`.

- Historia 2.15 (pantalla de título en nativo headless): marca por símbolo en
  `runLandingScreen`, captura PPM del display en el VBlank 60 posterior a la primera marca,
  traza de eventos sin punteros y runner de la escena `title`. Dos corridas por preset
  (`native-headless` y `native-asan`) producen la misma captura de 320×240 y 256 colores,
  y trazas deterministas dentro de cada preset. Tests L1 de flags, marca ausente/repetida,
  orden de captura, display apagado y fallos del runner; `scene_title` clase B con fixture
  del Pack. Suites sin disco, builds web, L0a y matching verdes. El `DRAWENV` de la película
  no afecta la captura observada; la paridad contra emulador corresponde a M3.
  Revisión (3 capas Codex): `--pack` explícito inexistente falla y 3 tests agregados (marca
  tardía, salida por overlay, errores de E/S); diferida la traza del reloj distinta entre
  presets. Evidencia: `docs/implementation-artifacts/evidence/story-2-15.log`.

- Historia 4.1 (el Juego en wasm con su propio hilo y presentación): preset `web` linkea `dw1.wasm`
  (`-pthread -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`, 64 MiB fijos, sin crecimiento); el Juego
  corre en un pthread con el stack estático de `crt.c` (respetado por Emscripten, `PORT_ASSERT` del
  stack efectivo en `[0, 16 MB)`; no hizo falta `-sPROXY_TO_PTHREAD`) y presenta por WebGL2 en un
  `OffscreenCanvas` transferido (`plat_present`). Playwright 1.64.0 (Chromium y Firefox, en la
  imagen `dw-port`): frame 0, patrón RGB15 de 3 frames con oráculo independiente, corrida normal
  leyendo el canvas, aislamiento COOP/COEP; 0 píxeles distintos. Presentación medida (frame 0 incluye
  inicialización): Chromium ~1,2 ms, Firefox ~1,8 ms por frame. Inventario automático de firmas
  (`tools/port/metrics/fnptr_casts.tsv`): indirectas por `-sEMULATE_FUNCTION_POINTER_CASTS`, directas
  con bloques `DW1_PORT` mínimos verificados contra el asm; stubs PsyQ con prototipo; `setjmp` probado
  en wasm. Revisión (2 iteraciones, 3 capas Codex): la iteración 0 con adaptadores manuales en 52
  archivos se revirtió por descartar argumentos usados. Diferidos: argumento residual de
  `setActiveAnim(0xc)` (trap si existe el NPC) y anomalía del título con `-fsanitize=function`.
  Suites web/nativo/ASan, clase B con disco y `make compare` verdes. Evidencia:
  `docs/implementation-artifacts/evidence/story-4-1/`.

- Historia 4.2 (worker de E/S y título en el navegador): `port/web/io_worker.js` abre el handle
  síncrono de OPFS (`dw1/us.pack`) antes del arranque y sirve lecturas posicionales por un buzón en
  memoria wasm compartida (`port/host/web/io_mailbox.c`, un slot con mutex entre lectores); el hilo
  del juego espera con `emscripten_futex_wait` y nunca toca OPFS. `pack.c` tiene una única costura
  de lectura posicional (nativo `FILE*`, web buzón) con la misma validación. Escena `title` en web:
  Chromium y Firefox dan el mismo SHA-256 que `native-headless`
  (`14d5bb08…30e7e7`), también con `navigator.storage` envenenado en los pthreads (0 accesos
  medidos; control positivo: 1 por lector). Clase A: 4 lectores concurrentes, 96 pedidos con bytes
  verificados y espera acotada con respuestas demoradas; Pack ausente/corrupto y rangos inválidos.
  Revisión (3 capas Codex): eliminado un mutex nativo especulativo y agregado el conteo real de
  accesos al guard. Suites web (19), nativo, ASan y clase B con disco verdes. Evidencia:
  `docs/implementation-artifacts/evidence/story-4-2/`.

- Historia 8.5 (entorno de testing en el homelab): job `testing` de clase B en `main` compila nativo y
  web, arma el Pack, valida el título y despliega en el host de deploy (`HUB_DOCKER_HOST`) una imagen
  nginx sin bytes del disco (chequeo `check_context.py`) construida en ese daemon; el Pack va a un
  volumen con nombre vía la API de Docker, solo si cambió su hash. Playwright Chromium contra la URL
  desplegada da el título nativo; rollback automático y job manual `testing-rollback`. `dw1.html` sin
  parámetros es un lanzador ("Cargar Pack", "Jugar"). Uso en `docs/port/testing-env.md`. El digest de
  `dw-port` en CI se actualizó a la imagen con Playwright. Evidencia (job 279 verde):
  `docs/implementation-artifacts/evidence/story-8-5/`.
- Historia 3.1 (emulador de referencia y captura automática): `tools/port/Dockerfile` compila
  PCSX-Redux `2068784a…8b3f` y OpenBIOS en la etapa `pcsx-redux` (sobre la etapa `toolchain`), y a
  `dw-port` solo pasan el binario, `openbios.bin` y Xvfb. `tools/port/reference/capture.py` + `capture.lua`
  detectan el frame 0 (retorno del primer `VSync`, VBlank 822) y `runLandingScreen` (VBlank 8642)
  con breakpoints, y capturan el display GP1 en marca+60 con la misma conversión PPM que el port.
  Seis corridas dieron el mismo SHA-256 `56132366…260d` (320×240, 256 colores), fijado en
  `title.toml`. Referencia **provisional** (OpenBIOS); la repetición con el BIOS del dueño queda en
  `deferred-work.md`. Para 3.2: el port difiere en 10.674 píxeles (caja x 1..128, y 0..217, rayos
  animados). CTest con disco 42/42 (`reference_title` incluido); en CI, pipeline 92 verde con `reference_title` real en `native-disc` (imagen del job `images` 305, timeout de 3 h, runner con 12 GB). Revisión (3 capas Codex): 2
  patches, 1 diferido (test de inputs del Lua, para 3.3) y 6 rechazados. Uso en `docs/port/reference.md`.
  Evidencia: `docs/implementation-artifacts/evidence/story-3-1.log`.

- Retrospectiva de la Épica 2 (`docs/implementation-artifacts/epic-2-retro-2026-10-17.md`):
  veredicto `accepted-with-open-items`; CTest con disco 42/42 en `19487fb`. 6 acciones en
  `sprint-status.yaml`; la más urgente es A1 (`GsSortOt` pierde las primitivas del bucket 0 de la
  OT hija porque `ClearOTagR` omite los paquetes terminales), antes de comparar escenas que lo usan.
- Acciones A1 y A2 de esa retro cerradas: `GsSortOt` reenlaza el último nodo de la OT hija
  (test de alcanzabilidad; los terminales `nop2`/`sys_nop` quedan en `deferred-work.md` para
  3.2/3.10) y CI corre `scene_title` en `native-disc` y la suite `native-asan` con disco
  (`native-asan-disc`), fallando si un test se saltea. Pipeline 96 verde.
- Resto de la retro: A4 (diferidos F4/F6/F7 en `deferred-work.md`), A5 (`epics.md` reconciliado en
  2.3, 2.6, 2.9 y 2.11) y A6 (tabla de desvíos del asm en `docs/port/build.md`) cerradas. A3: la
  historia 8.2 pasa a `in-progress` (pipeline A/B hecho en la Épica 2); le falta probar que un MR
  desde un fork no dispara la clase B.

## En curso

- Fase 5: Épica 4 (M4) en curso; historias 4.1 y 4.2 cerradas. Historia 8.5 cerrada. Épica 3 (M3) en curso: 3.1 cerrada; próxima 3.2 (runner de Escenas y pantalla de título exacta). El resto de la Épica 4 sigue después de la Épica 3.

## Bloqueos

- Ninguno. Pendiente del dueño: repetir la captura de `title` con su BIOS real (no bloquea).

## Métricas

| Métrica | Valor |
|---|---|
| Funciones del juego en C / total (USA) | 2741 / 2741 |
| % descompilado del juego (bytes) | 100 % (931.800 B); PsyQ en asm: 612 funciones, 144.348 B (se reemplaza) |
| Funciones PsyQ usadas / cubiertas por DW2-PC | 202 / 81 |
| Archivos C del juego que compilan a wasm32 en el preset `web` | 119 / 119 (+ `_psstart.c` reemplazado) |
| Archivos C del juego que compilan a i686 en el build del port | 119 / 119 (+ `_psstart.c` reemplazado) |
| Bloques `DW1_PORT` en `src/`/`include/` | 79 apariciones del guard (`docs/port/src-changes.md`) |
| Funciones PsyQ usadas: implementadas / stubs generados / libc del host | 187 (52 mínimas M2) / 0 / 12 (de 199) |
| Macros inline GTE implementadas / stubs | 11 / 0 |
| Archivos del disco en el Manifest USA | 818 (+ `@exe`) |
| Escenas que funcionan (nativo) | 1 (título; referencia capturada, todavía no idéntica: 3.2) |
| Escenas que funcionan (web) | 1 (título, igual a nativo, Chromium y Firefox) |
| Llamadas con firma distinta inventariadas / sin resolver | 1031 / 0 (`fnptr_casts.tsv`) |

## Notas

- Pregunta abierta (memlog de la arquitectura): el VBlank del reloj usa 59,94 Hz (NFR-1); psx-spx da
  ~59,83 Hz para NTSC no entrelazado. Se mide en la Historia 3.10 contra el Emulador de referencia.

- El contexto Docker por defecto de esta máquina (`docker-server`) apunta a un host remoto; los
  comandos del port usan `DOCKER_CONTEXT=default` (daemon local) para que los bind mounts vean el
  repo. Durante la Fase 1 se lanzó por error un primer build contra el host remoto
  (`<docker-server>`): quedó la imagen intermedia `dw-build` y `ubuntu:noble` en ese servidor.

- `origin` tenía una rama `master` previa con solo `LICENSE` (historia no relacionada); se dejó
  intacta y el trabajo se publica en `origin/main`.
