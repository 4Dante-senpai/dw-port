# Trabajo diferido

- source_spec: `docs/implementation-artifacts/spec-2-1-esqueleto-del-build-del-port.md`
  summary: Fijar `SOURCE_DATE_EPOCH` en el build del port (AD-17) junto con el check de `.wasm` reproducible.
  evidence: AD-17 lo exige y no está en `CMakeLists.txt`; hoy ningún TU usa `__DATE__`/`__TIME__`, así que se verifica con NFR-7 en la Historia 4.10.

- source_spec: `docs/implementation-artifacts/spec-2-6-linkeo-y-frame-0.md`
  summary: `rand`/`srand` con la semántica de PsyQ (RAND_MAX 0x7FFF) en vez de la libc del host.
  evidence: `randomLimit` (`src/main/math.c`) hace `(limit * rand()) >> 15`; con glibc el resultado sale de rango. Historia 2.9.
- source_spec: `docs/implementation-artifacts/spec-2-6-linkeo-y-frame-0.md`
  summary: Stubs que devuelven NULL para funciones que reservan memoria o devuelven punteros (`malloc3`, `CdSearchFile`).
  evidence: el Juego recibe NULL sin aviso; se resuelve con `malloc3` real (2.9) y stubs con comportamiento definido (2.14).
- source_spec: `docs/implementation-artifacts/spec-2-6-linkeo-y-frame-0.md`
  summary: Inventario de llamadas por puntero con tipo distinto antes del build web (resuelto en 4.1).
  evidence: `tools/port/metrics/fnptr_casts.tsv` + CTest `fnptr_inventory`; web usa `EMULATE_FUNCTION_POINTER_CASTS`. Native-asan conserva `-fno-sanitize=function`.

- source_spec: `docs/implementation-artifacts/spec-2-7-manifest-y-herramienta-nativa-del-pack.md`
  summary: Test del hook pre-commit y del `.gitignore` para todos los patrones prohibidos (incluidos `*.pack` y `*.pack.tmp`).
  evidence: ningún test corre `tools/port/hooks/pre-commit`; quitar `|pack` del regex o volver a `pack/` en `.gitignore` deja todo verde.
- source_spec: `docs/implementation-artifacts/spec-2-7-manifest-y-herramienta-nativa-del-pack.md`
  summary: Confirmar que el SHA-256 de las filas `F2` (2336 B con EDC de Form 2) acepta dumps de otras herramientas.
  evidence: medium sin verificar; se resuelve validando un segundo dump (otra herramienta) de un disco USA contra `us.tsv`.
- source_spec: `docs/implementation-artifacts/spec-2-7-manifest-y-herramienta-nativa-del-pack.md`
  summary: Verificar si el Juego lee archivos `F1` en `CdlModeSize1` (2340 B), que el Pack no puede servir sin subheader ni EDC/ECC.
  evidence: medium sin verificar; Historia 2.8 (trazar los `CdControl(CdlSetmode)` del Juego).
- source_spec: `docs/implementation-artifacts/spec-2-12-renderer-software-y-libgpu.md`
  summary: Verificar contra el Emulador de referencia el orden de copia de filas de GP0 80 (`MoveImage`) con solape, incluida la envoltura 511→0.
  evidence: `gpu_soft.c` elige el orden por `dy > sy` (semántica memmove); si el hardware copia siempre de arriba hacia abajo, las copias solapadas hacia abajo difieren. Se resuelve con un caso sintético en DuckStation (Historia 3.1).

- source_spec: `docs/implementation-artifacts/spec-2-13-libgs-con-el-pipeline-tmd.md`
  summary: Validar con los TMD del Pack que llegan sin mapear (bit 0 de flags en 0) antes de `GsMapModelingData`.
  evidence: `[hypothesis]` anotada en `port/psyq/libgs.c`; si alguno llegara mapeado con direcciones PS1, `GsLinkObject4` apuntaría fuera de la memoria del port. Se resuelve leyendo el campo flags de cada TMD del disco (Historia 3.11); la especificación 2.15 difiere expresamente esa validación.

- source_spec: `docs/implementation-artifacts/spec-2-14-stubs-con-comportamiento-definido-para-audio-video-y-memory.md`
  summary: `MOV_playMovie` (`src/mov/mov.c:263`) pasa un `DRAWENV` local sin inicializar a `PutDrawEnv`; con los stubs de 2.14 ese camino ya se ejecuta en el port.
  evidence: lectura de stack sin inicializar (UB en C, igual que en la PS1); puede dejar estado de dibujo dependiente de la pila y romper la captura determinista de 2.15. Comprobación 2.15: las dos corridas de `title` en cada preset (`native-headless` y `native-asan`) dan el mismo SHA-256 de captura y de reloj dentro de cada preset. La captura también coincide entre presets; no se exige igualdad de trazas entre builds. No afecta la captura observada del título; no se modifica `MOV_playMovie`. La lectura sin inicializar sigue pendiente para el video real de la Épica 3.

- source_spec: `docs/implementation-artifacts/spec-2-15-pantalla-de-titulo-en-nativo-headless.md`
  summary: La traza de eventos del reloj de la Escena `title` difiere entre `native-headless` y `native-asan` (SHA-256 `1dec875a…` frente a `befd2a43…`), aunque la captura coincide.
  evidence: cada preset es determinista consigo mismo, pero el tiempo virtual no debería depender del build; candidatos: costos de `DrawSync` que dependen del contenido de paquetes con stack sin inicializar (`DRAWENV` de `MOV_playMovie`) o del layout de memoria. Se resuelve comparando las dos trazas hasta el primer evento distinto (antes de la Historia 3.10, calibración del reloj).
- source_spec: `docs/implementation-artifacts/spec-4-1-el-juego-en-wasm-con-su-propio-hilo-y-presentacion.md`
  summary: `scriptStartAnimation`/`scriptSetEntityWalking` llaman `setActiveAnim(0xc)` con un solo argumento, pero la definición consume `animId`; el port hace trap si existe un NPC con `scriptId` 12.
  evidence: asm `scriptStartAnimation` (0x80105540) no carga `a1` antes de `jal setActiveAnim`; `a1` es el residuo de `scriptNPCStartAnimation` (igual a `animationId` si ningún NPC coincide, residuo de `startAnimation` si coincide). Severidad medium sin verificar; se resuelve trazando `a1` en el emulador de referencia (épica 3) o con el compilador matching.
- source_spec: `docs/implementation-artifacts/spec-4-1-el-juego-en-wasm-con-su-propio-hilo-y-presentacion.md`
  summary: con `-fsanitize=function` en `native-asan`, la escena del título (clase B) produjo un solo color en lugar de 256, sin diagnóstico de ASan/UBSan.
  evidence: observado en la iteración 0 de la historia 4.1 (adaptadores manuales, luego revertidos); el control con `-fno-sanitize=function` dio 256 colores. Causa no aislada (maybe-false, medium si es real): repetir con el árbol final y `-fsanitize=function` para ver si persiste; la hipótesis previa es `DRAWENV` sin inicializar en `MOV_playMovie`.
