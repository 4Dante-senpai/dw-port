# Epic 2 Context: El Juego arranca en nativo hasta la pantalla de título (M2)

<!-- Compiled from planning artifacts. Edit freely. Regenerate with compile-epic-context if planning docs change. -->

## Goal

Compilar el Juego USA completo (C de `dw_decomp`) como binario Linux i686 headless sobre una Capa
PsyQ propia, alimentarlo con un Pack construido desde el disco del usuario y llegar a la pantalla de
título (`runLandingScreen`, tras `playMovie(0)` en `src/main/main.c`) con el Renderer software y una
captura determinista. La exactitud contra el Emulador de referencia queda para la Épica 3. Es el
primer tramo prioritario del proyecto.

## Stories

- Story 2.1: Esqueleto del build del port y lista generada de TUs
- Story 2.2: Checks L0a y los inventarios de literales y spins
- Story 2.3: Memoria PS1, enlaces de OT y reinicio de overlays
- Story 2.4: Traducción de direcciones PS1 en `src/` e `include/`
- Story 2.5: Reloj virtual y semántica de `VSync`
- Story 2.6: Linkeo completo y arranque hasta el frame 0
- Story 2.7: Manifest y herramienta nativa del Pack
- Story 2.8: `libcd` y modelo del CD sobre el Pack
- Story 2.9: `libc`, `libapi` y `libetc` deterministas
- Story 2.10: Modelo GTE y macros inline
- Story 2.11: `libgte` y tablas leídas del exe en el Pack
- Story 2.12: Renderer software y `libgpu`
- Story 2.13: `libgs` con el pipeline TMD
- Story 2.14: Stubs con comportamiento definido para audio, video y Memory card
- Story 2.15: Pantalla de título en nativo headless

## Requirements & Constraints

- El juego corre como C compilado; nunca se interpreta ni recompila MIPS.
- Ningún dato del disco ni de CodeWarrior entra al repo: manifests solo con hashes, tamaños y LBAs. Los bytes que ya reproduce el C de `src/`/`include/` están permitidos.
- Todo cambio en `src/`/`include/` va detrás de `#ifdef DW1_PORT ... #else <original> #endif`, con una fila en `docs/port/src-changes.md`; el preprocesado sin `DW1_PORT` debe quedar idéntico a `upstream/main`.
- Sin `cc_mips.dll`: no hay build PS1 matching; funciones nuevas en `src/` se validan por comportamiento y se registran en `docs/port/non-matching.md`.
- El ritmo es 59,94 Hz exactos (1001/60000 s) sin deriva acumulada.
- Ninguna historia cierra sin evidencia automática (tests, logs, hashes).
- Docs en español; código, comentarios y commits en inglés (Conventional Commits).

## Technical Decisions

- **Capas:** Juego (`src/`) → Capa PsyQ (`port/psyq`) → Backend (`port/backend`) y Host (`port/host`, interfaz `plat_*`). El Backend no llama a `plat_*`. Herramientas en `tools/port/`.
- **TUs:** la lista se genera de `mk/version/us.mk` y `config/us/*.yaml`; no se linkea `asm/`; no existe `port/recomp/`.
- **Native:** i686 (`-m32`), `_Static_assert(sizeof(void *) == 4)`; `native-headless` sin SDL; `-no-pie`; la ventana de OT de 16 MB empieza en el segmento de datos.
- **Flags del juego, idénticos en todos los presets:** `-fsigned-char -fwrapv -fno-strict-aliasing -fno-common -DDW1_PORT -DLANGUAGE_C -DVERSION_US`; `-ffile-prefix-map`, `SOURCE_DATE_EPOCH`. Presets: `native`, `native-headless`, `native-asan`, `web`.
- **PsyQ:** una implementación por función en `port/psyq/<lib>.c` con comentario de fuente `dw2-pc:<archivo>` o `asm:asm/us/main/psyq/<lib>.s` (manda el asm de DW1); headers del port antes que los de upstream; macros GTE inline redefinidas como llamadas al GTE del Backend. Archivos adaptados de DW2-PC llevan `/* Adapted from DW2-PC (CC0): <ruta> @ 816ed0d */`.
- **Memoria:** arena `Ps1_Ram[0x200000]` con `PS1_RAM(addr)`; scratchpad de 1 KB; traducción de direcciones solo en macros `DW1_PORT` y en entradas PsyQ con direcciones enteras; `__rts_info_t__.h` del port renombra `_end`/`_stack_*`. Enlaces de OT solo con `PS1_LINK`/`PS1_LINK_PTR`; stack del hilo del juego dentro de la ventana.
- **Overlays:** TU envoltorio con `#pragma clang attribute push(__attribute__((section("dw1ovl_<ovl>"))), apply_to = variable(is_global))`; snapshot al arrancar; restore en `loadDynamicLibrary` (sync y async).
- **Tiempo:** `port/psyq/clock.c` dueño único (ciclos a 33.868.800 Hz + cola de eventos); toda entrada PsyQ avanza el reloj; callbacks solo en el hilo del juego; CD a 75/150 sectores/s; `DrawSync` por costo; `VSync(n)` con semántica PsyQ. Loops de espera sin PsyQ → `port_yield()` bajo `DW1_PORT`.
- **Pack:** Manifest `tools/port/manifest/us.tsv` (ruta, LBA, sectores, modo F1/F2, tamaño, SHA-256 + `SLUS_010.32`); un solo C en `port/host/pack/`; validación todo o nada; `CdlModeSize1` reconstruido en `cdrom`.
- **Tablas PsyQ** (`rcossin_tbl`, `ratan_tbl`, …) se leen de `SLUS_010.32` en el Pack con offsets de `config/us/main.yaml`; prohibido `.incbin`.
- **Renderer software** (VRAM 1024×512×16, desde `dw2-pc/backend/psxgpu.c`) es el oráculo, detrás de `port/backend/gpu.h`.
- **Tests:** L0a (preprocesado, sin datos), L1 (unit con CTest en `native-headless`, sin datos), L2 (Escenas con datos, `tools/port/scenes/<id>.toml`). Errores: `PORT_ASSERT` para invariantes. Docker siempre con `DOCKER_CONTEXT=default`.

## Cross-Story Dependencies

- 2.1 base de todas; 2.2 antes de tocar `src/` (2.4); 2.3 y 2.5 antes del linkeo (2.6); 2.7 antes de 2.8 y 2.11; 2.12 antes de 2.13; 2.15 integra todo.
- Después de la Épica 2 siguen las historias 4.1 y 4.2 (título en el navegador) antes de la Épica 3.
- El disco USA está disponible localmente (clase B corre a mano en el contenedor `dw-port`).
