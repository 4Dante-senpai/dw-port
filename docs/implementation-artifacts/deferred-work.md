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

- source_spec: `docs/implementation-artifacts/spec-8-5-entorno-de-testing-en-el-homelab.md`
  summary: Presentación continua en el navegador: con una corrida sin fin el canvas queda en blanco, porque `plat_present` dibuja en el `OffscreenCanvas` del hilo del juego (AD-9) y el navegador solo compone ese frame cuando el hilo vuelve a su bucle de eventos, cosa que no pasa hasta que termina la corrida.
  evidence: Reproducido 2026-10-17 en Chromium con el build web: `?pack=1&frames=300` muestra el título al terminar; `?pack=1&frames=1000000000` sigue en blanco a los 15 s. Los tests no lo detectaron porque leen el framebuffer con `glReadPixels`. Requiere una decisión de arquitectura (por ejemplo, presentar desde el hilo principal con `requestAnimationFrame` leyendo un buffer compartido, o ceder el hilo con JSPI/ASYNCIFY) antes de las historias interactivas de la Épica 4 (4.5 en adelante). Mientras tanto, "Jugar" usa 300 frames.

- source_spec: `docs/implementation-artifacts/spec-3-1-emulador-de-referencia-y-captura-automatica.md`
  summary: Repetir la captura de referencia de `title` con el BIOS real del dueño (`capture.py title --bios <ruta>`, fuera del repo) y registrar en `docs/port/reference.md` si coincide con OpenBIOS o qué difiere (pregunta abierta 3 del PRD).
  evidence: Decisión del dueño (2026-10-17, opción 1B): la historia 3.1 cierra con la referencia OpenBIOS marcada provisional; la corrida con BIOS real depende del dueño. Resuelto el 2026-10-18 con SCPH-1001: `title` coincide; en `newgame` coincide el estado, pero la semilla de `rand` del BIOS real es `0x24040001`, no 0 (`docs/port/reference.md`, "Resultado"). Decisión cerrada por el dueño (spec E3, `spec-e3-semilla-de-rand-del-bios-real.md`): el port y OpenBIOS parcheado usan `0x24040001`; New Game con OpenBIOS parcheado coincide con SCPH-1001 en estado, `rand` y frame. Cerrado.

- source_spec: `docs/implementation-artifacts/spec-3-1-emulador-de-referencia-y-captura-automatica.md`
  summary: Verificar en `tools/port/reference/capture.lua` el efecto real de los inputs por VBlank (pulsación, mantenimiento y liberación en el latch previsto) con una secuencia no vacía.
  evidence: Revisión de 3.1 (medium): `test_capture.py` reemplaza el adaptador Lua por un comentario y la escena `title` usa `inputs = []`, así que borrar `pad.setOverride` no rompe ningún test. Corresponde a la historia 3.3, la primera con inputs.

- source_spec: `docs/implementation-artifacts/spec-e2-retro-a1-gssortot-sin-terminales.md`
  summary: Decidir contra el Emulador de referencia si `ClearOTagR` debe restaurar los paquetes terminales `nop2` → `sys_nop` del original (GP0 `0x80` con `0x80000000, 0, 0, 0x00000002` y cuatro NOP al final de cada OT).
  evidence: `asm/us/main/psyq/libgpu.s` `ClearOTagR` y `asm/us/main/data/psyq/libgpu.data.s:66-84`; el port los omite (`port/psyq/libgpu.c`, comentario de `ClearOTagR`), así que `DrawOTag` no ejecuta esa copia de VRAM ni su costo en `DrawSync`. Puede afectar al bit de máscara y al ritmo; se resuelve en la 3.2 (diff de píxeles) o en la 3.10 (costos).

- source_spec: `docs/implementation-artifacts/spec-2-8-libcd-y-modelo-del-cd-sobre-el-pack.md`
  summary: En el último sector de un archivo, el port copia solo los bytes del archivo; la PS1 copia el sector entero de 2048 B (registrado en la retro de la Épica 2, F4).
  evidence: spec 2.8:135-140 (aprobado en su checkpoint porque el sector entero desborda `MAPHEAD_SCRIPT_BUFFER` en nativo). En la consola ese desborde escribe sobre la memoria vecina, así que el estado del port puede divergir. Se resuelve en la 3.10/3.11: identificar con el Emulador de referencia qué queda después del buffer y si el Juego depende de esos bytes.
- source_spec: `docs/implementation-artifacts/spec-2-5-reloj-virtual.md`
  summary: Los root counters sin IRQ no se reinician al llegar al target con reset-at-target; `GetRCnt` devuelve la cuenta acumulada con wrap de 16 bits (retro de la Épica 2, F6).
  evidence: `port/psyq/libapi.c` (`counter_value` solo hace `& 0xFFFF`; `arm` programa reinicios solo con `mode & 0x10`); `port/psyq/tests/test_clock.c` espera `12500 & 0xFFFF` con target 1000. Hoy `src/` no llama `GetRCnt`. Se corrige con el test cuando la 3.5 (`libsnd`) use los root counters.
- source_spec: `docs/implementation-artifacts/spec-2-12-renderer-software-y-libgpu.md`
  summary: `DrawOTag` con una OT que no termina imprime un aviso y retorna en lugar de usar `PORT_ASSERT` en debug (retro de la Épica 2, F7).
  evidence: `port/psyq/libgpu.c` `DrawOTag` (corte tras 0x100000 paquetes); la convención de invariantes es `PORT_ASSERT` (`port/psyq/port.h`). Agregar el assert y un test de abort la próxima vez que se toque `libgpu`.

- source_spec: `docs/implementation-artifacts/spec-3-2-runner-de-escenas-y-pantalla-de-titulo-exacta.md`
  summary: Paridad general del rasterizador `gpu_soft` con SoftGPU de PCSX-Redux en sala limpia (escenas de prueba renderizadas en el emulador; reglas de bordes e interpolación de color/UV para triángulos y quads arbitrarios), sin leer su código GPL.
  evidence: Decisión del dueño (2026-10-17, opción B empezando por C): la 3.2 solo corrige la regla que hace pasar `title`; las demás Escenas (NFR-8) pueden encontrar otras diferencias del rasterizador.

- source_spec: `docs/implementation-artifacts/spec-3-2-runner-de-escenas-y-pantalla-de-titulo-exacta.md`
  summary: Verificar contra el Emulador de referencia la regla UV de `scan_uv` (`port/backend/gpu_soft/gpu_soft.c`) en primitivas inclinadas, UV decrecientes, winding inverso, recorte y V fraccionario.
  evidence: Revisión de 3.2 (maybe-false, medium sin verificar): la regla se infirió solo del quad del título y se aplica a todos los triángulos texturados; los SHA de VRAM existentes no cambian, pero no tienen referencia de consola. Se resuelve con escenas de prueba renderizadas en PCSX-Redux (parte de la paridad general en sala limpia).

- source_spec: `docs/implementation-artifacts/spec-3-3-inputs-por-vblank-y-comparacion-de-estado.md`
  summary: Validar en `state_vars.txt` que la lectura declarada no exceda el objeto PS1 original (hoy solo se controla el tamaño nativo y los límites de RAM).
  evidence: Revisión de 3.3, hallazgo #4: `config/us/symbols.txt` no registra tamaños; habría que derivarlos de la dirección del símbolo siguiente. Las 15 variables actuales se revisaron a mano.
- source_spec: `docs/implementation-artifacts/spec-3-3-inputs-por-vblank-y-comparacion-de-estado.md`
  summary: Canonicalizar los punteros al área fusionada `TAMER_MODEL_BUFFER` + `MAIN_D_801A8B98` (en nativo se ven como `TAMER_MODEL_BUFFER+0x7800...` y en PS1 como `MAIN_D_801A8B98+...`).
  evidence: Revisión de 3.3, hallazgo #6: ninguna variable actual apunta a esas áreas. Resolverlo cuando una Escena compare un puntero a ellas.

- source_spec: `docs/implementation-artifacts/spec-e3-semilla-de-rand-del-bios-real.md`
  summary: Paridad de píxeles de New Game: `scene_newgame_state` solo informa el diff de imagen; el port difiere de la referencia en 402 píxeles (caja 87..217, 46..121) también con la semilla nueva.
  evidence: Revisión de E3 (blind-hunter y verification-gap). `run.py --state` captura el `ValueError` de imagen y sigue; con OpenBIOS parcheado la referencia ya coincide con SCPH-1001 (`48322f1c…e508`), así que una vez cerrado el diff podría exigirse el frame.

- source_spec: `docs/implementation-artifacts/spec-e3-capturas-de-referencia-mas-rapidas.md`
  summary: Incluir el SHA-256 del binario de PCSX-Redux en `state_fingerprint` para que `--reuse` no acepte referencias de otro emulador.
  evidence: Con `reference_newgame_state --reuse`, reconstruir la imagen local con otro PCSX-Redux mantiene la huella (escena, scripts, símbolos, OpenBIOS) y reutiliza la referencia vieja; en CI lo cubre la clave de caché por `.gitlab-ci.yml`. Requiere cambiar la huella, que la historia excluía.

- source_spec: none
  summary: Descartado (2026-10-18): graphify (`graphifyy==0.9.92`, grafo AST local) como skill de navegación de código para Claude Code y Codex, porque no redujo tokens.
  evidence: Medición con n=1 y `codex-run.sh container`, misma pregunta (callers de `AddPrim` y `addObject`, definición de `GsSortClear`), comparada con `codex_usage.py`. Sin skill: 228k de input (29k sin caché) y 7 llamadas. Con skill: 255k (57k sin caché) y 8 llamadas. Respuestas equivalentes. Codex contrasta las aristas INFERRED con `rg` y termina haciendo el trabajo dos veces. Límites del grafo: no indexa `asm/`, no modela structs de headers y parsea parcialmente unos 26 archivos. Si se reevalúa: medir antes preguntas de impacto multinivel y caminos entre funciones, donde `rg` no alcanza. No usar `graphify install`, `graphify claude install` ni `graphify codex install`: `AGENTS.md` es symlink de `CLAUDE.md` y la sección quedaría duplicada, y el hook PreToolUse gasta tokens. No instalarlo vía `requirements.txt` (archivo de upstream; el volumen `dw_venv` tapa `/dw/.venv`): usar un venv en `/opt/graphify` desde `tools/port/Dockerfile`. Excluir con `.graphifyignore`: `reference/ external/ permuter/ .claude/ _bmad/ docs/ text/ config/`. La implementación descartada estaba en el commit local `703daa2`, ya borrado.
- source_spec: `docs/implementation-artifacts/spec-3-6-xa-y-sectores-form-2.md`
  summary: Verificar contra `StCdInterrupt` del asm USA si, con el anillo STR lleno, la consola descarta sectores en vez de pausar la lectura como hace el port (medium, sin verificar).
  evidence: El port reintenta el mismo LBA y alarga la película; se resuelve leyendo `asm/us/main/psyq/libcd.s` (StCdInterrupt, 800B3468) o con una escena que llene el anillo en la referencia (3.7/3.10).
- source_spec: `docs/implementation-artifacts/spec-3-7-mdec-libpress-y-videos-str.md`
  summary: Redondeo de color del MDEC: cuatro componentes azules de OP1 difieren 5/255 de PCSX-Redux (2318 (144,80), 2587 (224,64), 3439 (176,96), 3453 (176,96)), aceptados como desvío conocido (decisión 5).
  evidence: psx-spx no especifica la precisión fija de YUV→RGB; las variantes probadas sobre los 3636 frames (`build/verification-3-7-gates/`) no cumplen máximo y media a la vez. Se resuelve con documentación del hardware o pruebas en una consola real.
