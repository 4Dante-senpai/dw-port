# Riesgos priorizados

Fecha: 2026-10-11. Basado en las mediciones de `docs/research/`. Prioridad = impacto ×
probabilidad, revisada contra los riesgos de `CLAUDE.md`. Cada riesgo es insumo de un ADR.

| # | Riesgo | Impacto | Prob. | Prioridad |
|---:|---|---|---|---|
| 1 | Capa PsyQ de DW1 mucho mayor que la de DW2-PC | Alto | Cierta | **Crítica** |
| 2 | Bloqueos del loop en el navegador / modelo de hilos | Alto | Alta | **Crítica** |
| 3 | Datos del juego dentro del binario o del deploy | Alto (legal) | Media | **Crítica** |
| 4 | Overlays en un único binario con `wasm-ld` | Alto | Media | Alta |
| 5 | Direcciones absolutas PS1 y punteros embebidos en datos | Alto | Media | Alta |
| 6 | Diferencias de compilador (CodeWarrior vs Clang) | Medio | Alta | Alta |
| 7 | Sin compilador matching: cambios en `src/` sin verificar | Medio | Cierta | Alta |
| 8 | Audio: `libsnd` de otra versión de PsyQ | Medio | Alta | Alta |
| 9 | Sin emulador de referencia para diff de frames | Medio | Media | Media |
| 10 | Rendimiento a 59,94 Hz en web (rasterizador software) | Medio | Media | Media |
| 11 | Soporte de navegador (JSPI, OPFS, SDL3 en worker) | Medio | Media | Media |
| 12 | Divergencia con upstream (merges) | Bajo | Media | Baja |
| 13 | Código de terceros con licencia sin verificar | Bajo | Baja | Baja |

## 1. Capa PsyQ más grande que la de referencia

**Dato:** el juego usa 202 funciones PsyQ (2976 llamadas); DW2-PC define 81 de ellas por nombre
(`psyq-usage.md`). Falta el pipeline TMD de `libgs` (`GsSortObject4J` + 27 `GsTMD*` tomadas por
dirección), OTs y doble buffer de `libgs`, 14 funciones de `libgte` (sin contar las `GsTMD*`), el secuenciador de `libsnd`,
`PadRead`, `CdSearchFile`/`CdRead`, `malloc3`.

**Mitigación:** traducir cada función desde el asm retail de DW1 (`asm/us/main/psyq/*.s`,
disponible tras `make regenerate`), con tests unitarios por función contra valores capturados en
el emulador de referencia; reutilizar el modelo GTE y los backends de DW2-PC. Priorizar por la
pantalla de título (M2): primero lo que se ejecuta hasta el título.

## 2. Bloqueos del loop en el navegador

**Dato:** `VSync`, `DrawSync`, `CdReadSync`, `readFile` síncrono y `setjmp/longjmp` (36 en `main`).
Los tres modelos (PROXY_TO_PTHREAD, ASYNCIFY, JSPI) funcionan en un modelo de juguete
(`web-constraints.md`), pero JSPI está detrás de flag en node 24 y su estado en Firefox está sin
confirmar.

**Mitigación:** `PROXY_TO_PTHREAD` como base (COOP/COEP en el deploy propio). Spike temprano en
M4: el juego completo en worker presentando por OffscreenCanvas/memoria compartida, medido en
Chrome y Firefox. Mantener la interfaz de plataforma (`port/host`) independiente del modelo para
poder cambiar a JSPI.

## 3. Datos del juego en el binario o en el deploy

**Dato:** DW2-PC hace `.incbin` de bytes del exe (`ratan_tbl.bin`, `retail_rodata.bin`,
`retail_data.bin`, `Ovl_LoadArea.bin`). Los overlays de DW1 tienen bloques de datos del disco
como `.rodata` en asm (`asm/us/<ovl>/data/texture.rodata.s`, `model_*.rodata.s`); `src/*_bss_us.c`
y otros `.c` contienen tablas de datos (son parte del decomp de upstream, MIT, y ya están en el
repo público de upstream).

**Mitigación:** el build web no linkea ningún `.s` de `asm/` ni `.incbin` de `assets/`: los
bloques que en PS1 venían embebidos se leen del pack del usuario al iniciar o al cargar el
overlay. Check automático en CI: el `.wasm` y el deploy no pueden contener ninguna secuencia de
N bytes de los archivos del disco (comparación por hashes de ventanas, en el runner privado).
Hook pre-commit ya activo para el repo.

## 4. Overlays con `wasm-ld`

**Dato:** 16 overlays con regiones solapadas (`overlays.md`); su `.data`/`.bss` se reinicia en
cada carga. DW2-PC agrupa las secciones con `ld -r` + linker script; `wasm-ld` no acepta linker
scripts.

**Mitigación:** spike en la primera historia de M2: agrupar por overlay con
`__attribute__((section("dw1ovl_<unit>")))` aplicada por un header forzado (`-include`) o con
`#pragma clang section`, verificar que `wasm-ld` emite `__start_/__stop_` para secciones de datos
con nombre C (soportado en ELF; **a verificar** en wasm). Alternativa sin linker: generar con
splat/el `*_bss_us.c` la lista de variables de cada overlay y registrar su imagen inicial.

## 5. Direcciones absolutas y punteros en datos

**Dato:** 0 accesos a registros de hardware desde el juego; 484 referencias al scratchpad en 54
funciones; ~10 macros con direcciones `0x80xxxxxx` fijas (`hardware-access.md`); alta densidad de
candidatos a punteros en `CHDAT/MMD*/*.MMD`.

**Mitigación:** arena de 2 MB + `PS1_RAM()`; scratchpad en headers PsyQ propios; macros
redefinidas detrás de `DW1_PORT`. Validar la hipótesis de los MMD leyendo su cargador antes de
diseñar; si son punteros, cargar esos archivos en la misma dirección de la arena.

## 6. Diferencias de compilador

**Dato:** el C compila con GCC MIPS (`make TOOLCHAIN=gcc`) y 97/120 archivos compilan a wasm32 sin
cambios. Quedan riesgos de comportamiento: signedness de `char`, desbordes con signo, orden de
evaluación, accesos fuera de rango que en PS1 eran inocuos, `-fstrict-aliasing`.

**Mitigación:** compilar el juego con `-funsigned-char`/`-fwrapv`/`-fno-strict-aliasing` como
DW2-PC; build nativo con ASan/UBSan para depurar; comparar estado (RAM de la arena) contra el
emulador en puntos fijos.

## 7. Sin compilador matching

**Dato:** `cc_mips.dll` no disponible; no se puede correr `make compare`.

**Mitigación:** todo cambio en `src/` detrás de `DW1_PORT` (no altera el build PS1 por
construcción: el preprocesador lo elimina); CI verifica que el preprocesado de cada `.c` sin
`DW1_PORT` es idéntico al de `upstream/main` (sustituto barato de `make compare`); `make
TOOLCHAIN=gcc` debe seguir compilando. Registrar en `docs/port/non-matching.md` cualquier función
nueva.

## 8. Audio: `libsnd` de otra versión

**Dato:** DW1 linkea `mw_lib41`; la traducción de `libsnd`/`libspu` de DW2-PC (≈5.100 líneas)
es de Psy-Q 4.7 y usa direcciones de globals de DW2.

**Mitigación:** emular el SPU (`backend/psxspu.c`) y traducir el `libsnd` de DW1 desde su propio
asm, usando el de DW2-PC como guía; tests con capturas de registros SPU del emulador.

## 9. Sin emulador de referencia

**Mitigación:** historia de M3 para un contenedor separado con PCSX-Redux (OpenBIOS, sin BIOS de
Sony) en modo headless con Lua para capturar frames y RAM; DuckStation regtest como alternativa
si el usuario provee una BIOS fuera del repo.

## 10. Rendimiento en web

**Mitigación:** rasterizador software a 1× primero (VRAM 1024×512, carga baja), presentado como
textura WebGL; medir en M4; renderer WebGL2 en M5. El render HD multi-hilo de DW2-PC necesita
pthreads (disponibles con COOP/COEP).

## 11. Soporte de navegador

**Mitigación:** matriz de pruebas Playwright (Chromium y Firefox) desde M4; detección de
funcionalidades en la shell con mensaje claro (sin `crossOriginIsolated`, sin OPFS).

## 12. Divergencia con upstream

**Mitigación:** cambios en `src/` mínimos y registrados en `docs/port/src-changes.md`; merge de
`upstream/main` periódico en una rama propia con el build del port y `make TOOLCHAIN=gcc` como
checks.

## 13. Licencias de terceros

**Mitigación:** DW2-PC es CC0 (reutilizable con atribución). SydMontague/DW1-Code y DW1-SydPatches
y PsyCross: verificar licencia antes de copiar código; usarlos solo como documentación hasta
entonces.
