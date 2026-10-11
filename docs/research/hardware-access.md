# Accesos a hardware, scratchpad y direcciones fijas (USA)

Medido el 2026-10-11 sobre `upstream/main` @ `09c174c`. Herramientas:

- `tools/port/metrics/hardware_access.py us`: recorre el código **original** de las 2741 funciones
  del juego (los `.s` por función de splat) y cuenta `lui` de direcciones que splat no pudo
  convertir en símbolo, más las instrucciones COP2 (GTE).
- `tools/port/metrics/pointer_scan.py disks/us`: candidatos a punteros PS1 dentro de los archivos
  del disco (solo cuenta; no imprime datos del juego).
- `grep` sobre `src/` para ubicar el C que produce cada acceso.

Las librerías PsyQ (asm) quedan fuera: el port las reemplaza completas, con sus accesos a
registros de GPU/SPU/CD/DMA/interrupciones.

## 1. Registros de hardware (`0x1F801000-0x1F802FFF`)

**Ningún acceso desde el código del juego.** Todo el acceso a GPU, SPU, CD-ROM, DMA, timers,
interrupciones, pad y memory card pasa por PsyQ. Es la condición que hace viable reemplazar el
hardware por la capa `port/psyq` + `port/backend` sin tocar el C del juego.

## 2. Scratchpad (`0x1F800000-0x1F8003FF`, 1 KB)

| Unidad | Referencias (`lui`) | Funciones |
|---|---:|---:|
| btl | 102 | 7 |
| dooa | 1 | 1 |
| fish | 147 | 14 |
| kar | 2 | 1 |
| main | 6 | 4 |
| murd | 1 | 1 |
| std | 113 | 14 |
| vs | 112 | 12 |
| **total** | **484** | **54** |

Direcciones más usadas: `0x1F800018` (51), `0x1F800000` (49), `0x1F800038` (42), `0x1F8000A4`
(25), `0x1F800200` (24), `0x1F800098` (24). Funciones con más accesos: `FISH_tickHookedFish` (32),
`STD/BTL/VS_renderRibbonStrip` (31 c/u), `STD/BTL/VS_renderRadialWaves` (24 c/u).

En C aparece como la macro PsyQ `getScratchAddr(n)` (32 usos fuera de los `*_bss_*.c`, p. ej. el
último argumento de `GsSortObject4J`) y como punteros a structs en el scratchpad dentro de los
efectos. **Plan:** el port provee sus propios headers PsyQ, donde `getScratchAddr` y cualquier
constante `0x1F800000` apuntan a un arreglo de 1 KB del port. Ningún cambio en `src/` debería
hacer falta si todo acceso pasa por la macro; los que sean literales numéricos en C se ubican con
`grep -rn 0x1f80 src` (hoy solo en `src/mov/mov_bss_us.c`, datos) y van detrás de `DW1_PORT`.

## 3. Direcciones absolutas de RAM (`0x80000000-0x801FFFFF`)

56 `lui` sin símbolo en 35 funciones (candidatos: `0x80000000` suele ser una constante de bit de
signo, no una dirección). Las direcciones reales confirmadas en `src/`:

| Dirección | Uso en C | Archivos |
|---|---|---|
| `0x80010000` (19) | buffer temporal para leer bancos de sonido VH/VB (región de `MOV_REL`) | `main/sound.c` (`loadMusicFont`, `loadDigimonSounds`, `VSLoadSounds`) |
| `0x80053800`, `0x80054838`, `0x80054D00`, `0x80055328` | modelos TMD embebidos en el overlay cargado (`BTL_FINISHER_MODEL`, `BTL_CONFUSION_MODEL`, `BTL_STUN_MODEL`, `BTL_BUFF_MODEL`) | `main/btl.c:24-27`, `std/std_setup.c:28-31` |
| `0x80038000` | `STD_TMD_BUFFER` / `VS_TMD_BUFFER` | `std/std_hud.c:29`, `vs/vs_hud.c:20` |
| `0x80044800` | `DOOA_SHARD_BUFFER` | `dooa/dooa.c:34` |
| `0x8008E000` | `DOOA_ORDERING_TABLE_1` (OT en región fija) | `dooa/dooa.c:37` |
| `0x80088800` | inicio de `TRN`/`TRN2` | overlays |
| `0x80080800` | `shop_START` | `main/utils2.c:24` |
| `0x80052AE0`... | `*_START` de los overlays (`REL_BIN_OFFSETS`) | `main/utils2.c` |

Además, 28 casts de literal `0x80xxxxxx` a puntero en `src/` (12 de ellos en archivos `_bss_` de
esta u otras versiones). **Plan:** en el port, toda dirección `0x80xxxxxx` se traduce a una arena del
tamaño de la RAM PS1 (2 MB), `PS1_RAM(addr) = arena + (addr & 0x1FFFFF)`, como hace DW2-PC
(`reference/dw2-pc/psyq/ps1mem.h`). Las macros de cada archivo (`BTL_FINISHER_MODEL`, ...) se
redefinen detrás de `#ifdef DW1_PORT` (cambio mínimo en `src/`, registrado en
`docs/port/src-changes.md`). Los modelos que en PS1 viven dentro del overlay cargado se copian a
esas direcciones de la arena desde el pack al "cargar" el overlay.

## 4. GTE (COP2)

| Unidad | Instrucciones COP2 | Funciones |
|---|---:|---:|
| btl | 52 | 6 |
| fish | 3 | 1 |
| kar | 4 | 1 |
| main | 88 | 18 |
| std | 50 | 5 |
| vs | 50 | 5 |
| **total** | **247** | **36** |

Vienen de las macros `gte_*` de `inline_c.h` / `gte.h` usadas en el C (p. ej. `main/partner.c`,
`main/overworld.c`, `main/effect_common.c`, `murd/murd.c`, `doo2/doo2.c`, `evl/evl.c`). En el port
esas macros se redefinen en los headers PsyQ propios como llamadas a un modelo C del GTE (DW2-PC
ya tiene uno: `reference/dw2-pc/psyq/gte.c` + `gte_native.h`, con test contra PCSX-Redux).

## 5. Punteros embebidos en archivos del disco

Palabras de 32 bits alineadas en `0x80010000-0x801FFFFF`. Con datos aleatorios la densidad
esperada es ~0,5 por cada 1000 palabras.

| Directorio | Archivos | Con candidatos | Palabras | Candidatos | Densidad (/1000) |
|---|---:|---:|---:|---:|---:|
| (raíz) | 21 | 17 | 7426906 | 4389 | 0.59 |
| CARD | 1 | 1 | 473088 | 124 | 0.26 |
| CHDAT | 179 | 176 | 3216754 | 13845 | **4.30** |
| DOOR | 33 | 0 | 4518 | 0 | 0.00 |
| ETCDAT | 22 | 6 | 95517 | 10 | 0.10 |
| ETCHI | 5 | 1 | 628932 | 89 | 0.14 |
| ETCNA | 18 | 0 | 45635 | 0 | 0.00 |
| MAP | 484 | 337 | 34805121 | 8998 | 0.26 |
| MOVIE | 4 | 4 | 38230976 | 147852 | 3.87 |
| SCN | 2 | 1 | 207387 | 1 | 0.00 |
| SOUND | 7 | 2 | 2051277 | 5 | 0.00 |
| STDDAT | 44 | 16 | 342120 | 50 | 0.15 |

Archivos con densidad alta: `CHDAT/MMD1/DIGI.MMD` (54,7), `CHDAT/MMD5/EDIG.MMD` (41,3),
`CHDAT/MMD1/TOKO.MMD` (40,4), y en general los `CHDAT/MMD*/*.MMD` (modelos y animaciones de
Digimon); `SHOP_REL.BIN` (20,6), `TRN2_REL.BIN` (12,5), `TRN_REL.BIN` (10,1) y `SLUS_010.32`
(18,8) son código/datos linkeados, donde los punteros son esperables.

**Hipótesis (no verificada):** los `.MMD` contienen punteros absolutos ya resueltos o tablas que
el juego parchea al cargar. `MOVIE` (STR) tiene densidad alta por ser bitstream comprimido, no
por punteros. Validar leyendo el código que carga los MMD (`main/model.c`, búsqueda de
`.MMD` en `src/main/file_table_us.c`) antes de diseñar la traducción. Si son punteros, el port
tiene dos opciones: cargar el archivo en la misma dirección de la arena (los punteros siguen
siendo válidos vía `PS1_RAM`) o reescribirlos al cargar.

## 6. Resumen para la arquitectura

- Sin registros de hardware en el código del juego: la frontera es la API PsyQ.
- Scratchpad y GTE se resuelven en headers PsyQ propios del port.
- Direcciones absolutas: arena de 2 MB + `PS1_RAM()`; unas 10 macros en `src/` a cambiar detrás de
  `DW1_PORT`.
- WASM32 tiene punteros de 32 bits y es little-endian como PS1, pero la memoria lineal empieza en
  0: los enlaces de OT de 24 bits (`addr & 0xFFFFFF`) funcionan si toda la memoria que se enlaza
  en OTs queda por debajo de 16 MB en la memoria lineal de WASM (en nativo 64-bit DW2-PC lo logra
  con base de imagen fija en `0x01000000`).
