# Estado del proyecto — DW1 Web Port

Última actualización: 2026-10-30

## Modo de operación

- Compilador matching (`cc_mips.dll`): **DISPONIBLE** desde 2026-10-12 (CodeWarrior for PlayStation
  Release 4, `bin/cc_mips/cc_mips_40.dll`). `make` + `make compare` obligatorios en cada merge
  (regla 2 de `CLAUDE.md`); se corren en la imagen `dw-build` (`docs/port/setup.md`, paso 7).

## Hito actual

- M0 Entorno: **completo** (modo sin compilador matching).
- M1 Inventario: **completo** (`docs/research/`).
- M2 Nativo headless: **completo** (título determinista, sin referencia).
- M3 Paridad nativa: **completo** (Épica 3 cerrada: retrospectiva `accepted-with-open-items`, 2026-10-27).
- M4 Web: **en curso** (4.1 frame 0 con WebGL2; 4.2 título web idéntico a nativo; 4.3 disco propio y Pack en el navegador; 4.4 Shell, capacidades, pausa y juego en vivo).

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
- Historia 4.3 (disco propio y Pack en el navegador): la Shell acepta el `.bin` solo o con su
  `.cue`, por selector o arrastre. El worker de E/S corre la CLI `dw1-pack` compilada a wasm
  (`dw1_pack.js`, mismo C que la herramienta nativa, más el comando `sha256`), lee la Imagen por
  partes con WORKERFS y escribe el Pack directo a un handle síncrono de OPFS. El commit es
  `us.pack.tmp` -> `us.pack` -> sidecar `us.pack.sha256`, y restaura el Pack anterior si algo falla.
  Muestra progreso (incluida la relectura del SHA), «Listo para jugar», aviso si se deniega
  `persist()` y «Borrar datos». Clase A (`web_disc`, 38 casos en Chromium y Firefox): `.bin`/`.cue`,
  arrastre, modo de pista, ISO, byte cambiado, exe JP/PAL (SLUS-01032), selección inválida,
  recarga, borrado y rollback con fallas inyectadas. Clase B (`web_disc_real`): Pack web
  `c01e5c36…0cf6`, idéntico al nativo en ambos navegadores; memoria máxima de 67 MiB (Chromium);
  SM-4 de 81 s (Chromium) y 36 s (Firefox) en el contenedor limitado a 2,5 CPU. Diferido a 4.4: el
  Web Lock también debe serializar el build y el borrado del Pack. Evidencia:
  `docs/implementation-artifacts/evidence/story-4-3/`.
- Historia 4.4 (pantallas de la Shell web y capacidades): la Shell verifica
  aislamiento, `SharedArrayBuffer`, OPFS y Web Locks con un mensaje por capacidad y sin cargar el
  juego; toma el Web Lock `dw1-game` (una segunda pestaña espera con un mensaje; también serializa
  el build y el borrado del Pack, diferido de 4.3); muestra bienvenida y aviso de copia propia,
  disco, progreso, «Jugar», opciones y Memory cards (contenido en 4.5-4.8) y «Borrar datos».
  «Jugar» crea el `AudioContext` en el clic y arranca el juego en la misma página. El hilo del
  juego tiene ritmo de pared en `VSync` (`port/host/web/pace.c`) con pausa manual y por pestaña
  oculta: VBlanks quietos y 0 resincronizaciones al reanudar (60,01 VBlank/s en 100 s). Cada frame
  llega al hilo principal como `ImageBitmap`, así que «Jugar» corre sin límite y muestra el juego
  en vivo (título en 123 s). La imagen `dw-port` suma PulseAudio para Firefox. `ctest --preset web`
  43/43 con disco, en Chromium y Firefox. Evidencia: `docs/implementation-artifacts/evidence/story-4-4/`.

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

- Historia 3.2 (runner de Escenas y título exacto): `run.py --compare` valida la referencia
  fijada y compara RGB555, informa diferencias con caja y PPM local, y descuenta excepciones
  declaradas y validadas antes del juego. Interpolación UV por intersecciones de aristas 16.16
  inferida en sala limpia del TIM y la captura (sin leer SoftGPU); el título nativo y ASan
  dan `56132366…260d`, idéntico a referencia, sin excepciones. Web conserva esa igualdad en
  Chromium y Firefox (4 casos). Test sintético de las 256×240 muestras del quad y SHA de VRAM
  existentes verdes. `scene_title_compare` requiere el fixture de referencia y entra en CI
  clase B. Revisión (3 capas Codex): 5 parches (gradientes UV por triángulo y fila, lock entre
  tests de título, casos negativos del runner); diferida la verificación de la regla UV en otras
  primitivas. Sin cambios en `src/`, `include/` ni capturador de referencia. Paridad general de
  primitivas y comparación con BIOS del dueño siguen diferidas.
  Verificación: builds nativo/ASan/web; suites completas 37+6 omisiones y 35+6 omisiones;
  comparación con disco real 3/3 (Pack, referencia nueva y port); web 3/3, 4 casos de navegador.
  Evidencia: `docs/implementation-artifacts/evidence/story-3-2.log`.

- Corrección E3 (ritmo nativo y texto Shift-JIS), tras la prueba manual de la 3.9: la ventana
  ritmaba un período por frame y DW1 usa `VSync(3)`, así que corría 3 veces más rápido y desbordaba
  el audio. Ahora la pared sigue los VBlanks virtuales y, durante las cargas, el productor de audio
  espera al dispositivo en vez de descartar muestras (juego real: 0 desbordes y 0 underruns a 1200
  frames, pared 25,52 s contra 25,51 s virtuales). El build del port compila copias de las fuentes con
  literales CP932 (`tools/port/sjis_literals.py`), porque Clang no soporta `-fexec-charset`; la grilla
  de nombres ya no muestra "△". PCSX-Redux se parchea para aplicar dithering a polígonos texturados
  planos con modulación, como el hardware. Escena `naming` exacta salvo 663 px del modelo 3D
  (diferido). Con disco: native 15/15, headless 79/79, ASan 77/77, web 38/38; `make compare` idéntico.
  Reapertura: segfault al tocar una máquina del gimnasio (`addFileReadRequest` copiaba `*loc` con
  `loc = NULL` en las cargas por nombre); arreglado detrás de `DW1_PORT` y cubierto por `overlay_probe`.
  El binario nativo imprime un backtrace al crashear (resolver con `llvm-symbolizer-18`). Mismos totales.
  Segunda reapertura: segfault en el primer combate contra Agumon (`handleBattleStart` leía
  `ENTITY_TABLE[i]` NULL en batallas predefinidas); arreglado detrás de `DW1_PORT`.
  Spec: `docs/implementation-artifacts/spec-e3-ritmo-y-texto-sjis-nativo.md`.

- Historia 3.9 (preset `native` con ventana SDL3): binario i686 con `DW1_HOST=sdl` sobre SDL3 3.4.16
  i686 (`/opt/sdl3/native32` en la imagen). Ventana 640x480 redimensionable con letterbox y escalado
  nearest; el hilo del Juego presenta a 59,94 Hz de pared y el hilo principal dibuja. Teclado al estilo
  DuckStation y gamepad posicional, muestreados por VBlank (`port_pad_latch`); los `--inputs` de una escena
  tienen prioridad. Audio SPU a la frecuencia del dispositivo con un remuestreador propio (AD-10: ajuste
  máximo ±2 %, 0 underruns al 99 % y al 101 %). CLI compartida con headless (`native_cli`). `docker_run.sh --gui`
  para WSLg (X11, Wayland, Pulse, `/dev/input`). Controles en `docs/port/controls.md`. La imagen fija
  `python3:amd64`, porque las dev libs `:i386` lo cambiaban a 32 bits. Revisión (3 capas Codex): 12 hallazgos,
  7 parches y 5 rechazados. Verificación con disco: headless 76/76, ASan 74/74, native 11/11; título SDL
  idéntico a `title.toml`; web verde; `make compare` idéntico. Sin probar: ventana, audio y gamepad físicos.
  Evidencia: `docs/implementation-artifacts/evidence/story-3-9.log`.

- Historia 3.8 (Memory card nativa): `libmcrd` traducida del asm USA sobre una capa de BIOS de tarjeta
  propia (`port/psyq/bios_card.c`: eventos, dispositivo `bu`, directorio y bloques, adaptada del OpenBIOS
  MIT de PCSX-Redux) y un modelo de sectores con eventos del reloj (costos de hipótesis para 3.10).
  `port/host/card.c` guarda el `.mcd` crudo de 131.072 bytes al terminar cada comando de escritura, con
  temporal + `fsync` + `rename`; `--card1/--card2` en el host nativo; en web la tarjeta sigue ausente
  (Épica 4). `run.py` le da al port las mismas tarjetas nuevas que a Redux. Escenas `card_newgame`
  (New Game crea el save en la ranura 1; `.mcd` final idéntico byte a byte al de Redux) y
  `card_continue` (Redux carga la tarjeta del port; estado tras `loadSavegame` idéntico). Decisión 2 del
  dueño: `movie0_skip` ahora exige el mismo VBlank que Redux (122 en ambos). CI: timeouts de
  `native-asan-disc` 4 h y `native-disc` 3 h, en serie. Revisión (3 capas Codex): 18 hallazgos, 4 parches
  y 14 rechazados con evidencia. Verificación con disco: nativo 75/75 y ASan 73/73 sumando la corrida
  de `movie0_skip`; web 36/36; `make compare` idéntico. Evidencia: `docs/implementation-artifacts/evidence/story-3-8.log`.

- Historia 3.7 (MDEC, `libpress` y videos STR): `port/backend/mdec` modela el MDEC según psx-spx
  (RLE, IQ, IDCT que conserva la fracción entre pasadas, YUV→RGB15/24; adaptado de DW2-PC, CC0) y
  `port/psyq/libpress.c` traduce del asm de DW1 `DecDCTReset/In/Out/vlc2` y sus callbacks; las tablas
  IQ/IDCT/VLC se cargan del ejecutable del Pack (sin bytes en el repo, AD-12). El fin de cada
  macroblock es un evento del reloj (decisión 1; 1024 ciclos, hipótesis para 3.10) y `CdlPause`
  cobra la latencia de psx-spx (decisión 3). `src/mov/mov.c` solo suma `clock_advance(1024)` bajo
  `DW1_PORT` en `MOV_waitForFlip`. Gates: escena `movie0` (OP1 completo, 3636 frames: el original
  nunca envía el último al MDEC) dentro de 4/255 máx. y 0,5/255 media por canal, salvo 4 componentes
  azules a 5/255 aceptados como desvío conocido (decisión 5; medias 0,219/0,169/0,252); A/V contra la
  referencia (decisión 2); `movie0_skip` (Start) vuelve exactamente 1 VBlank tarde (123 frente a 122:
  costo GPU de `ClearImage` y fase de `PadRead`), aceptado hasta la Historia 3.10 (decisión 7).
  Revisión (3 capas Codex): 14 hallazgos, 4 parches y 10 rechazados con evidencia. `newgame` ahora
  decodifica el video real y tarda el doble; sus timeouts suben (600 s → 1800 s por corrida).
  Verificación con disco en el PC del dueño: web 35/35 (3 con disco omitidas), nativo 68/68, ASan 66/66 (ver evidencia);
  `make compare` idéntico; XA de OP1 sigue a 54,42 dB. Diferidos en `deferred-work.md`: residuo de
  color MDEC y +1 VBlank de Start. Evidencia: `docs/implementation-artifacts/evidence/story-3-7.log`.

- Historia 3.6 (XA y sectores Form 2): en DW1 el XA solo aparece intercalado en los cuatro
  `MOVIE/*.STR` (1 de cada 8 sectores, ADPCM 4 bits estéreo 37,8 kHz). `port/backend/xa` decodifica
  los 8 formatos con el zigzag de psx-spx (adaptado de DW2-PC, CC0) y alimenta la entrada de CD del
  SPU; `CdRead2` y el anillo `St*` leen el STR real del Pack (decisión 1 del dueño: el streaming
  pasa a 3.6; el video sigue negro hasta MDEC en 3.7, pero dura lo que el STR). `CdInit` reproduce
  `CD_initvol`. El parche de Redux suma un tap XA en `FeedXA` (Redux no mezcla el XA en el SPU).
  Gate (decisión 2): escena `xa_op1`, OP1 completo, SNR 54,42 dB (umbral 40) sin desfase y 0
  escrituras SPU en ambos lados; el PCM mezclado es diagnóstico. El runner pasa a 40.000 frames
  (con los STR reales `newgame` no llegaba a `gameLoop`); se recapturaron `newgame` y
  `title_audio`. Revisión (3 capas Codex): 7 parches, 7 rechazados (inalcanzables en DW1), 1 diferido
  (anillo lleno: ¿la consola descarta o pausa?). `make compare` idéntico; web 31/31, nativo 59/59 y
  ASan 57/57 con disco, sin omisiones. Sin cambios en `src/`/`include/`. Evidencia:
  `docs/implementation-artifacts/evidence/story-3-6.log`. CI: pipeline 122 verde con `dw-port@sha256:fd5b68d5…` (tap XA):
  native-disc 18/18 y native-asan-disc 57/57. Recapturar las referencias tras cambiar Redux superó el
  timeout de 1 h (pipeline 120), así que los dos jobs con disco pasan a 2 h.

- Historia 3.5 (`libsnd` de DW1 y traza de audio): las 21 entradas de `libsnd` que llama el Juego,
  su secuenciador SEQ (`SsSeqCalledTbyT`: notas, controles, NRPN, tempo, bucles, fin) y las
  entradas de `libspu` que alcanza están traducidas del asm de DW1 (registro de voz de `0x36`
  bytes, distinto de DW2); ya no quedan `stub-m2:` en `libsnd`. Traza de escrituras SPU y WAV en
  el port y en la referencia; parche de PCSX-Redux que toma el PCM del mezclador antes del
  dispositivo (dos capturas idénticas byte a byte). Escena `title_audio` en dos tramos. Gate por
  contenido (decisiones 3 y 4 del dueño, AD-15 enmendado): tramo 1 744/744 escrituras de voz y
  tramo 2 612/612 desde el primer note-on nuevo; regla B por VBlank y SNR se reportan sin
  bloquear hasta 3.10, porque el port aún no modela costos de CPU/CD (desfase de escena previo a
  libsnd, `docs/port/audio-parity.md`). RCnt2 conserva su retícula al atenderse tarde.
  Contenedores del port limitados a 2,5 CPUs / 9 GB tras saturar el host Proxmox. Revisión
  (3 capas Codex): 9 parches, 2 conservados por fidelidad al asm, 1 rechazado (SNR bloqueante,
  contrario a la decisión 3). CTest nativo 45, ASan 43, web 25 y disco 16/16 nativo y 52/52 ASan
  verdes. Sin cambios en `src/`/`include/`. Evidencia:
  `docs/implementation-artifacts/evidence/story-3-5.log`. CI: pipeline 116 verde (native, web-objects,
  native-disc 16/16, native-asan-disc 52/52, matching, testing) con `dw-port@sha256:1d19dce2…`, publicada
  por el pipeline 115 con el tap PCM. El fallo de `native` en el 115 fue contención del runner
  durante el build en frío de `images`; pasó en el 116.

- Historia 3.4 (SPU emulado y `libspu`): `port/backend/spu` adapta el SPU de DW2-PC (CC0):
  24 voces ADPCM con interpolación gaussiana, ADSR, ruido, PMON, reverb, KON/KOFF/ENDX, DMA y
  entrada de CD por callback; genera las muestras pendientes antes de cada acceso a registro, una
  cada 768 ciclos (26.459.973 en 35.964 VBlanks, exacto). Las 6 entradas de `libspu` que llama el
  Juego están traducidas del asm (ya no son `stub-m2:`). Vectores L1 calculados aparte
  (`tools/port/audio/spu_vectors.py`). Revisión (3 capas Codex): 6 parches (DMA encadenada,
  overflow del cursor de reverb, tests de PMON, pitch fraccionario y EON). CTest nativo 46,
  ASan 44, web 26 y disco 6/6 verdes. Sin cambios en `src/`/`include/`. Evidencia:
  `docs/implementation-artifacts/evidence/story-3-4.log`.

- Historia 3.3 (inputs por VBlank y comparación de estado): `run.py --state` alinea port y
  referencia por puntos de control (`símbolo#n`), aplica inputs relativos a cada uno y compara
  15 variables tipadas (`state_vars.txt`, punteros normalizados) y la secuencia de `rand`. La
  Escena `newgame` (título, menú, New Game, nombres, hasta `gameLoop`) coincide en 9 puntos de
  control, 15 variables y 16 llamadas a `rand`, en nativo y ASan; la referencia se captura dos
  veces y exige igualdad exacta. Cambios en `src/` bajo `DW1_PORT` (`src-changes.md`): área
  contigua para `BOYS.MMD`, escritura de `clearMapDigimon` por la dirección 0, guardas NULL de
  `script_menu.c`, `NAMING_CTRL_BOXES`, residuo de a1 en `setActiveAnim`, `DRAWENV` de
  `MOV_playMovie` a cero (corrige el `av_stubs` ASan del pipeline 100) y `renderMapOverlays`.
  Revisión (3 capas Codex): 9 parches, 2 diferidos, 1 rechazado. CTest nativo 49, ASan 47, web 26
  y disco 13/13 verdes; `make compare` idéntico. Evidencia:
  `docs/implementation-artifacts/evidence/story-3-3.log`.

- Historia E3 (semilla de `rand` del BIOS real): el port arranca `rand` con `0x24040001`, la
  semilla de SCPH-1001 derivada de la traza de New Game (206 llamadas antes de la primera salida
  natural). `tools/port/Dockerfile` parchea `s_currentSeed` de OpenBIOS al mismo valor en una capa
  propia (falla si el patrón cambia), así que CI reproduce el hardware real sin BIOS propietario. El
  sello de las referencias de estado usa el SHA-256 de `openbios.bin`. Con OpenBIOS parcheado,
  New Game coincide con SCPH-1001 en estado, 16 `rand` y frame (`48322f1c…e508`); título sin
  cambios. Nativo 49/49 y ASan 47/47 con disco sin omisiones; `make compare` OK. CI: imagen
  `dw-port@sha256:e79c381a…6f45` (job `images` 387) y pipeline 104 verde en la rama. Sin cambios en
  `src/`/`include/`. Evidencia: `docs/implementation-artifacts/evidence/story-e3/` y `docs/port/reference.md`
  ("Verificación con OpenBIOS parcheado").

- Capturas de referencia más rápidas (E3): `capture.py` ejecuta las dos pasadas en
  paralelo con Xvfb independientes (displays iniciales 199/299). `--reuse` valida huella,
  archivos y pin antes de reutilizar; `reference_newgame_state` lo usa. Los jobs
  `native-disc` y `native-asan-disc` conservan `build/reference/` en la caché privada
  del runner `dw1-data`, con clave por `.gitlab-ci.yml` y `when: always`, sin artifacts.
  New Game: 1560,271 s secuenciales → 871,147 s en paralelo (44,2 % menos espera
  bajo carga); fixture con caché: 0,13 s. Ambas trazas, frames y sello coinciden
  byte a byte con la captura secuencial (frame `48322f1c…e508`).
  Tests del capturador 37/37; builds y suites nativa/ASan sin fallos (49/47 pruebas,
  con ocho omisiones de disco por preset en la ejecución sin `DW1_DISC_DIR`, cubiertas
  después por clase B 12/12 en ambos presets vía `ctest_no_skip.sh`, sin omisiones).
  CI pipeline 106: verde; `reference_newgame_state` en frío 941,54 s y caché guardada.
  Pipeline 108 (job 421): caché restaurada, `reference_newgame_state` 0,13 s; ctest de
  `native-disc` 1370 s → 412 s.
  Diferido: identidad del binario de PCSX-Redux en la huella (`deferred-work.md`).
  Detalles y mediciones en
  `docs/implementation-artifacts/spec-e3-capturas-de-referencia-mas-rapidas.md`.

- Historia 3.10 (calibración de costos y ritmo nativo): el reloj virtual calibrado contra PCSX-Redux da diferencia 0 en los 15 puntos de control de `title`, `movie0`, `movie0_skip` y `map_load`. Antes el port llegaba entre 286 y 800 VBlanks antes.
  La tasa de VBlank medida en Redux es 564398 ciclos, es decir 60,0087 Hz. NFR-1, AD-8 y `CLAUDE.md` se actualizaron con ese valor.
  AD-8 permite ahora calibrar también la forma del modelo: seek por distancia, CPU medida por función y servicio de IRQ de la BIOS. Las Escenas mantienen Start presionado para no depender de la fase del pad.
  La prueba de 10 minutos de pared dio menos de 1 VBlank de error y 0 resincronizaciones. Pasaron las suites native-headless 82/82, native-asan 80/80, native 92/92 y web 39/39, todas sin omisiones, y `make compare` dio idéntico.
  Detalles en `docs/port/clock-calibration.md`. Queda diferido el orden del costo de memcpy/memset (`deferred-work.md`).

- Historia 3.11 (hipótesis pendientes de los overlays): queda cerrada la hipótesis de `SHOP_REL.BIN` en USA (`docs/research/overlays.md`).
  En `map_load`, Redux muestra una sola carga, síncrona: `newGameScene → checkShopMap(0xda) → loadDynamicLibrary(11) → readFile → CdRead`, 16 sectores a `0x80080800`. `checkShopMap(0xcc)` desde `loadMap` no carga, porque el mapa no está en `SHOP_MAPS`.
  Hasta el final de la Escena nada ejecuta código en `0x80080840–0x80088610`. Las lecturas de esa región son todas de `DecDCTvlc2`/`DecDCTvlcBuild`, porque la tabla VLC de MOV la reutiliza.
  El análisis estático (`tools/port/metrics/shop_overlay.py`) solo encuentra referencias de `main` a DOOA, DGET y `REL_BIN_OFFSETS`, y no confirma que el archivo sea una copia reubicada de la tienda de `main`.
  Límite: `map_load` no entra a una tienda, y `loadShopLibrary` (script 0x23) se verificó solo estáticamente. Se agregó la sonda `capture.py map_load --shop-probe` y `map_load` suma los checkpoints `checkShopMap` y `checkShopMap#2`, con diferencia 0. Pasaron 12/12 tests de CTest con disco. Sin cambios en `src/`.

- Retrospectiva de la Épica 3 (`docs/implementation-artifacts/epic-3-retro-2026-10-26.md`): veredicto `accepted-with-open-items`. Durante la retro se corrigieron dos gates:
  - `--state` ahora falla por diferencia de imagen, salvo que la Escena declare `image_informative` con motivo, píxeles y caja. Las diferencias tienen que quedar dentro de la caja. Lo declaran `newgame` y las dos tarjetas.
  - `native_wall_10min` estaba en rojo desde la 3.10. `next_due` recorría los 32 slots de la cola en cada consulta de VSync; ahora cachea el mínimo. En este servidor dio 0 resincronizaciones en dos corridas.
  La espera del productor de audio se conserva como excepción aceptada de AD-8.
  Suites con disco: native-headless 83/83, native-asan 81/81 y native 94/94. Quedan abiertas las acciones A2 y A4–A8, y F15: `--frames 1200` da 2 underruns, igual que en `main` (`deferred-work.md`).

- Historia 8.1 (métricas automáticas): `tools/port/metrics/status_table.py` calcula funciones del
  Juego en C / total, funciones PsyQ con comentario de fuente (187 / 202, todas `asm:`), bloques
  `DW1_PORT` (117) y filas de `src-changes.md` (117), y Escenas que pasan a partir del JUnit de
  CTest. `--update` reescribe el bloque marcado de la sección Métricas. El test de clase A
  `status_table_repo` falla si los bloques y las filas de un archivo no coinciden (NFR-10) o si
  una función PsyQ del port pierde su comentario de fuente. `src-changes.md` tenía 36 bloques sin
  fila (secciones en prosa de 3.3, 3.7 y 3.10) y una fila con el símbolo equivocado
  (`main.c`: `memcpy`, no `checkCameraMovement`). Sin cambios en `src/`. Verificación con disco y
  `asm/` en el contenedor: `make compare` OK, 2741 / 2741 funciones, `status_table_*` 2/2 en
  `native-headless`, y parseo de JUnit reales de `scene_title`, `scene_title_audio` y
  `web_scene_title`. Revisión con subagentes Claude: 10 parches, 1 diferido (`--check` de
  `STATUS.md`). Evidencia: `docs/implementation-artifacts/evidence/story-8-1/README.md`. Spec:
  `docs/implementation-artifacts/spec-8-1-metricas-automaticas-en-status-md.md`.

## En curso

- Fase 5: la Épica 3 (M3) está cerrada, con su retrospectiva hecha. Sigue la Épica 4 (M4); las historias 4.1, 4.2, 4.3 y 4.4 ya están cerradas, y también la 8.1 y la 8.5.

## Bloqueos

- Ninguno.

## Métricas

### Automáticas

Generadas con `tools/port/metrics/status_table.py --update docs/STATUS.md` (Historia 8.1);
para las Escenas, agregar `--junit <archivo>` por cada `ctest --output-junit` de los presets
nativo y web. La herramienta falla si un archivo de `src/`/`include/` tiene distinta cantidad de
bloques `DW1_PORT` que de filas en `docs/port/src-changes.md` (NFR-10; test `status_table_repo`).

<!-- status_table:begin -->
| Métrica | Valor |
|---|---|
| Funciones del Juego en C / total (USA) | 2741 / 2741 |
| Funciones PsyQ implementadas / usadas (por comentario de fuente) | 187 / 202 (`dw2-pc:` 0, `asm:` 187, `stub-m2:` 0) |
| Bloques `DW1_PORT` en `src/` e `include/` | 117 |
| Filas de `docs/port/src-changes.md` | 117 |
| Escenas que pasan (nativo) | sin verificar (sin resultados de CTest; pasar `--junit`) |
| Escenas que pasan (web) | sin verificar (sin resultados de CTest; pasar `--junit`) |
<!-- status_table:end -->

Una métrica que no se puede calcular en el entorno aparece como `sin verificar (<motivo>)`:

- Funciones del Juego en C / total: requiere `asm/us/` (`make regenerate`).
- Escenas que pasan (nativo y web): requieren los resultados de CTest de los tests `scene_*` y
  `web_scene_*`, que necesitan el disco y las referencias de PCSX-Redux. Hace falta correr la suite
  completa de Escenas: la Historia 8.1 solo corrió las de título para probar el parseo de JUnit.

### Manuales

| Métrica | Valor |
|---|---|
| Funciones del juego en C / total (USA) | 2741 / 2741 |
| % descompilado del juego (bytes) | 100 % (931.800 B); PsyQ en asm: 612 funciones, 144.348 B (se reemplaza) |
| Funciones PsyQ usadas / cubiertas por DW2-PC | 202 / 81 |
| Archivos C del juego que compilan a wasm32 en el preset `web` | 119 / 119 (+ `_psstart.c` reemplazado) |
| Archivos C del juego que compilan a i686 en el build del port | 119 / 119 (+ `_psstart.c` reemplazado) |
| Funciones PsyQ usadas: implementadas / stubs generados / libc del host | 187 (sin comportamiento M2: la Memory card es real desde 3.8) / 0 / 11 (de 198) |
| Macros inline GTE implementadas / stubs | 11 / 0 |
| Archivos del disco en el Manifest USA | 818 (+ `@exe`) |
| Escenas que funcionan (nativo) | 10 (`card_newgame`: crea el save, `.mcd` idéntico a Redux; `card_continue`: carga el save con estado idéntico; `movie0`: OP1 completo por MDEC, 3636 frames dentro de tolerancia salvo 4 componentes aceptados; `movie0_skip`: salida con Start en el mismo VBlank que la referencia; `xa_op1`: XA de OP1 a 54,42 dB contra la referencia; título idéntico a referencia OpenBIOS, sin excepciones; `newgame` con estado idéntico en 9 puntos de control; `map_load` con estado idéntico en 12 puntos de control, incluida la carga de SHOP; `naming`: grilla de nombres exacta salvo el modelo 3D; `title_audio` con escrituras de voz idénticas en 2 tramos) |
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
