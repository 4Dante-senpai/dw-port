# Análisis de DW2-PC como referencia de arquitectura

Fuente: `reference/dw2-pc` (submódulo de solo lectura) @ `816ed0d` (v0.1.5, 2026-10-10).
Licencia CC0 1.0: se puede reutilizar código, con atribución en el archivo que lo copie.
Tamaños medidos con `wc -l`.

## Visión general

DW2-PC compila el C del decomp de Digimon World 2 (1088 funciones, matching) para Windows/Linux
y reemplaza el hardware PS1 y PsyQ por una capa propia sobre SDL3:

```
src/      C del juego (cambios de 64 bits marcados DW2_NATIVE)
psyq/     PsyQ en C para PC (≈8.600 líneas .c+.h)
backend/  GPU PS1 emulada (VRAM + rasterizador), HD renderer, SPU, XA, MDEC, PGXP (≈5.400 líneas)
host/     SDL3: ventana, input, audio, memcard, pack, settings, UI ImGui, updater (≈10.100 líneas .c+.h)
```

Un solo ejecutable (CMake + Ninja). El juego corre en un hilo propio; el hilo principal maneja la
ventana. El reloj de VBlank es de 59,94 Hz exacto (`host/vblank.c`).

## Componentes

Leyenda: **R** = reutilizable tal cual (o casi), **A** = adaptar a DW1, **N** = no sirve en web.

### `psyq/`

| Archivo | Qué hace | DW1 | Notas |
|---|---|---|---|
| `gte.c`, `gte_core.h`, `gte_native.h`, `libgte.c` (≈770) | Modelo C del GTE (todas las operaciones COP2) + funciones `libgte` | **R** (`gte.c`), **A** (`libgte.c`) | Test contra PCSX-Redux en `tests/gte_test.c` + `tests/gte_ref.bin` (sin datos del juego). DW1 usa 56 funciones de `libgte`, DW2-PC define 15. `libgte.c` hace `.incbin` de `ratan_tbl.bin` extraído del exe: en el port web esa tabla debe generarse o leerse del pack. |
| `libgpu.c` (225) | `DrawOTag` recorre el OT con enlaces de 24 bits y manda cada paquete al GPU emulado; helpers de paquetes | **A** | DW1 usa 28 funciones de `libgpu` (DW2-PC define 13). |
| `libgs.c` (640) | 12 funciones `libgs` traducidas del asm retail | **A** (mucho) | DW1 usa 35 de `libgs` + 27 `GsTMD*`; DW2-PC cubre 13. El pipeline TMD (`GsSortObject4J`, `GsTMDfast*`) es trabajo nuevo. |
| `libcd.c` (524) | Protocolo `CdControlF`/`CdGetSector`/callbacks sobre el pack por LBA, XA | **A** | DW1 busca por nombre (`CdSearchFile`) y lee con `CdRead`/`CdReadSync`: otra superficie de API. La simulación de callbacks/colas sirve de modelo. |
| `libetc.c` (61) | VSync / callback de VBlank sobre el reloj del host | **R/A** | DW1 suma `PadInit`/`PadRead` de `libetc` (DW2 usa `libpad`). |
| `libpad.c` (62) | SIO pad | **N** para DW1 | DW1 no usa `libpad`. |
| `libsnd.c`, `snd_seq.c`, `snd_vm.c`, `snd_hw.c`, `libspu.c` (≈5.100) | `libsnd`/`libspu` **traducidos del asm retail de DW2 (Psy-Q 4.7)**, con los globals en sus direcciones retail de DW2 y un modelo de DMA/SPU debajo | **A** (con cuidado) | DW1 se linkeó con `mw_lib41` (otra versión de PsyQ, otras direcciones). La traducción es reutilizable como guía, pero las direcciones y tamaños hay que rehacerlos contra `asm/us/main/psyq/libsnd.s` / `libspu.s`. `snd_hw.c` hace `.incbin` de imágenes `.rodata`/`.data` retail: en web deben salir del pack (del `SLUS_010.32` del usuario), nunca del `.wasm`. |
| `libmcrd.c` (296) | Sistema de archivos de memory card sobre imágenes `.mcd` crudas de 128 KB | **R** | Formato compatible con DuckStation/PCSX-Redux/ePSXe; justo lo que pide el PRD. |
| `libpress.c` | MDEC (`DecDCTvlc`, `DecDCTin/out`) sobre `backend/mdec.c` | **R/A** | DW1 usa 6 funciones de `libpress`. |
| `ps1mem.c/.h` | Arena de 2 MB `Ps1_Ram`, `PS1_RAM(addr)`, `PS1_LINK_PTR(link)` con ventana de 16 MB | **A** | En WASM32 la memoria lineal empieza en 0: la ventana de 16 MB para enlaces de OT de 24 bits es natural si el heap del port está acotado; ver `web-constraints.md`. |
| `psyq_log.c`, `psyq_types.h` | Log, tipos | **R** | |

### `backend/`

| Archivo | Qué hace | DW1 | Web |
|---|---|---|---|
| `psxgpu.c` (735) | GPU PS1 emulada: VRAM 1024×512, GP0 completo salvo VRAM→CPU | **R** | Sí (C puro) |
| `psxgpu_hd.c` (1244) | Renderer HD por software (escala 1-8×, 16:9, sin wobble), multi-hilo | **R/A** | Sí, con pthreads de Emscripten (requiere COOP/COEP) o en un hilo |
| `psxgpu_hw.c` (866) + `shaders/` | Renderer en GPU con **SDL_GPU (Vulkan, SPIR-V)** | **N** | No: SDL_GPU no tiene backend WebGL. En web hay que escribir un renderer WebGL2 propio. |
| `pgxp.c` (211) | Vértices precisos (sin wobble) enganchados al GTE | **R** | Sí |
| `sharpen.c` (156) | CAS por CPU | **R** | Sí (opcional) |
| `psxspu.c` (516) + `psxspu_gauss.inc` | SPU emulado completo (ADPCM, ADSR, reverb) a 44,1 kHz | **R** | Sí; en web lo alimenta un AudioWorklet |
| `xadec.c` (213) | XA-ADPCM + resampleo 37,8→44,1 kHz | **R** | Sí |
| `mdec.c` (335) | Decodificador MDEC | **R** | Sí |

### `host/`

| Archivo | Qué hace | DW1 | Web |
|---|---|---|---|
| `main.c` (546) | Arranque, CLI, hilo del juego (stack bajo 2 GB con `MAP_32BIT`) | **A** | El truco de stack bajo no aplica (WASM32 ya es 32 bits). El hilo del juego mapea a `PROXY_TO_PTHREAD`. |
| `vblank.c` (165) | Reloj 59,94 Hz sin deriva, catch-up acotado | **R** | Sí; en web, con `emscripten_*`/`requestAnimationFrame` como fuente de tiempo según el modelo de hilos |
| `sdl.c` (1077) | Ventana, presentación con 3 slots entre hilos, hotkeys | **A** | Parte SDL_Renderer sirve con SDL3 en Emscripten (WebGL); la parte SDL_GPU no |
| `input.c` (306) | Teclado + gamepads SDL3 → máscara de botones PS1, remapeo | **R** | Sí (SDL3 usa Gamepad API en web) |
| `audio.c` (120) | Dispositivo SDL3 44,1 kHz alimentado por VBlank | **A** | En web: AudioWorklet (SDL3 en Emscripten usa ScriptProcessor/AudioWorklet según versión; evaluar) |
| `card.c` (98) | `card1.mcd`/`card2.mcd` en carpeta de usuario | **A** | En web: IndexedDB/OPFS + import/export |
| `pak.c` (430), `pakbuild.c` (912), `sha256.c` | Pack verificado por SHA-256 contra manifest; construcción desde `.bin/.cue` | **A** | La lógica de validación sirve; en web corre en el navegador sobre el `File` del usuario y escribe a OPFS |
| `overlay.c` (81) | Rangos `.data+.bss` por overlay (secciones agrupadas con `ld -r`), snapshot inicial y restauración en cada carga | **A** | El mecanismo `__start_/__stop_` de ELF funciona con `wasm-ld`? **Hipótesis a validar**: wasm-ld no soporta linker scripts; ver alternativas en el ADR de overlays. |
| `settings.c`, `settingsui.cpp`, `ui.cpp`, `imgui/` | Settings + UI ImGui | **A/opcional** | En web la UI de opciones puede ser HTML |
| `update.c`, `miniz/`, `monocypher/` | Auto-updater | **N** | Irrelevante en web (el deploy es la actualización) |
| `devui.cpp`, `devedit*.c`, `devsnap.c`, `devdata.c`, `trace.c`, `shot.c` | Herramientas de desarrollo, capturas | **A/opcional** | `shot.c` (PNG) sirve para el diff de frames nativo |
| `winconsole.c`, `dw2.rc` | Windows | **N** | |

### Pack de datos (`doc/PACK_FORMAT.md`)

- `dw2.pak` v1: cabecera 0x50, índice de entradas de 0x40 bytes (id, LBA, sectores, formato,
  offset, tamaño, SHA-256), datos alineados a 0x800. Formato 0 = Form 1 (2048 bytes/sector),
  formato 1 = sectores con Form 2 (XA/STR) guardados como cuerpo de 2336 bytes.
- Manifest (`configs/USA/pack_manifest.txt`) con un SHA-256 por archivo; se compila dentro del
  binario. Rechaza imágenes que no coinciden; sin modo "mods".
- **DW1:** los archivos están en el árbol ISO9660 (`dumpsxiso` los extrajo con nombre) y el juego
  los busca por nombre (`CdSearchFile`), así que el índice del pack debe ser por **ruta**, no por
  id de tabla. Conviene mantener la idea (índice + SHA-256 + Form 2 crudo para XA/STR) y cambiar la
  clave.

### Overlays

7 overlays de DW2 linkeados juntos; `ld -r` con un script agrupa `.data`/`.bss` de cada uno en una
sección `dw2ovl_<unit>`; `host/overlay.c` guarda la imagen inicial y la restaura en cada
`Ovl_Load`. DW1 tiene 16 overlays con el mismo patrón (ver `overlays.md`). En Emscripten el linker
es `wasm-ld`, que no acepta linker scripts ni `ld -r` con `-T`: el agrupamiento tiene que hacerse
de otra forma (p. ej. compilar cada overlay con `-fdata-sections` y registrar sus variables, o
agruparlas en una sección con `__attribute__((section(...)))` vía un header forzado con
`-include`). **A validar** en una historia técnica temprana.

### Threading

- Un hilo de juego (bloquea en `VSync`, lecturas de CD y el spin de flip) y el hilo principal con
  la ventana; intercambio de frames con 3 slots y semáforos SDL.
- Render HD con N hilos de dibujo (`--hd-threads`).
- En web el equivalente directo es `-sPROXY_TO_PTHREAD` (el `main` del juego corre en un pthread
  y puede bloquear), que exige `SharedArrayBuffer` y por lo tanto COOP/COEP. Ver
  `web-constraints.md`.

### Datos del juego compilados dentro del binario

DW2-PC hace `.incbin` de bytes extraídos del disco en su build (`ratan_tbl.bin`,
`retail_rodata.bin`, `retail_data.bin`, `Ovl_LoadArea.bin`). Para DW1 web esto **no se permite**
(regla 1: nada del juego en el deploy). Todo lo que venga del exe o del disco se lee en tiempo de
ejecución del pack del usuario, o se genera por fórmula (la tabla `SquareRoot0` de `libgs` ya se
genera así en DW2-PC).

## Qué se reutiliza, en resumen

- **Tal cual (con atribución CC0):** `backend/psxgpu.c`, `pgxp.c`, `psxspu.c`, `xadec.c`,
  `mdec.c`, `psyq/gte.c` + test, `psyq/libmcrd.c`, `host/vblank.c`, `host/input.c`, `sha256.c`.
- **Adaptar:** `libgpu`, `libgs`, `libgte`, `libcd`, `libetc`, `libpress`, `libsnd`/`libspu`
  (rehacer contra la versión de PsyQ de DW1), pack y manifest (índice por ruta), overlays (sin
  linker script en `wasm-ld`), `host/sdl.c`, `audio.c`, `card.c`.
- **No sirve en web:** `psxgpu_hw.c` + shaders SPIR-V (SDL_GPU/Vulkan), updater, Windows, el
  truco de stack bajo de `main.c`.
- **Nuevo para DW1:** pipeline TMD de `libgs`, secuenciador SEQ de `libsnd` (si DW1 usa SEQ
  estándar), `PadRead`, `CdSearchFile`/`CdRead`, `malloc3`/`free3`/`InitHeap3`, `rand` de PsyQ,
  renderer WebGL2.
