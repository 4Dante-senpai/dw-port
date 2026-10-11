# Overlays de Digimon World (USA)

Medido el 2026-10-11 sobre `upstream/main` @ `09c174c`. Fuentes: `config/us/overlay.ld`
(direcciones de carga), tamaños de `disks/us/*_REL.BIN` (disco del usuario, no versionado),
`src/main/utils2.c` (`loadDynamicLibrary`, `REL_BIN_OFFSETS`, `REL_BIN_FILES`) y los call sites de
`loadDynamicLibrary` en `src/`.

## Mecanismo de carga

`loadDynamicLibrary(Overlay lib, uint8_t *isComplete, uint8_t isAsync, FileCallback cb, void *param)`
(`src/main/utils2.c:376`) lee el archivo `REL_BIN_FILES[lib - 1]` del CD **tal cual** a la
dirección fija `REL_BIN_OFFSETS[lib - 1]`:

- `isAsync == 0`: `readFile()` bloqueante.
- `isAsync != 0`: `addFileReadRequestPath()` encola la lectura; `*isComplete` pasa a 1 al terminar.

No hay relocación ni tabla de símbolos: cada overlay está linkeado a su dirección final y llama a
`main` por dirección absoluta. Al recargarlo se pisan su código, `.data` y `.bss` (los `*_bss_us.c`
de cada overlay), que vuelven a sus valores iniciales.

El ejecutable `SLUS_010.32` se carga en `0x80090000` (`config/us/main.yaml`); todos los overlays
viven debajo, en `0x80010000-0x80090000`. El heap empieza después de `_end` y llega hasta la base
del stack (`src/main/main.c:3534-3539`, `InitHeap3`).

## Tabla de overlays

| Id | Archivo | Dirección de carga | Fin | Bytes | Funciones (C) | Cargado desde |
|---:|---|---|---|---:|---:|---|
| 1 | `BTL_REL.BIN` | `0x80052AE0` | `0x80075F7C` | 144540 | 388 | `main/btl.c:379` (combate) |
| 2 | `STD_REL.BIN` | `0x80052AE0` | `0x8007FEC8` | 185320 | 456 | `main/utils2.c:661` (mundo / modo estándar) |
| 3 | `FISH_REL.BIN` | `0x80070000` | `0x8007D96C` | 55660 | 110 | `main/fish.c:124` (pesca) |
| 4 | `EVL_REL.BIN` | `0x80060000` | `0x80069008` | 36872 | 31 | `main/evl.c:108` (evolución) |
| 5 | `KAR_REL.BIN` | `0x80053800` | `0x800639EC` | 66028 | 55 | `main/kar.c:11` (curling) |
| 6 | `VS_REL.BIN` | `0x80052AE0` | `0x80073268` | 133000 | 442 (\*) | `main/main_menu.c:4194, 4497`, `main/main.c:3421` (modo VS / demo) |
| 7 | `MOV_REL.BIN` | `0x80010000` | `0x80083428` | 472104 | 8 | `main/utils2.c:397` (`startMovie`, videos STR) |
| 8 | `DOO2_REL.BIN` | `0x80070000` | `0x8007B6EC` | 46828 | 18 | `dooa/dooa.c:365` (async) |
| 9 | `DOOA_REL.BIN` | `0x80080000` | `0x800878AC` | 30892 | 28 | `main/partner.c:654`, `murd/murd.c:788` |
| 10 | `TRN_REL.BIN` | `0x80088800` | `0x8008F3D4` | 27604 | 45 | `main/map.c:5460` (entrenamiento) |
| 11 | `SHOP_REL.BIN` | `0x80080800` (\*\*) | `0x80088610` | 32272 | 65 (\*\*) | `main/script_common.c:3076, 3084` |
| 12 | `DGET_REL.BIN` | `0x80080800` | `0x80081CF8` | 5368 | 13 | `main/script_common.c:3097` |
| 13 | `TRN2_REL.BIN` | `0x80088800` | `0x8008DCC0` | 21696 | 40 | `main/map.c:5466` |
| 14 | `MURD_REL.BIN` | `0x8007C000` | `0x8007E960` | 10592 | 15 | `main/tamer.c:1703, 1738` |
| 15 | `ENDI_REL.BIN` | `0x80060000` | `0x80061534` | 5428 | 16 | `main/script_interp.c:1850` (final, async) |
| 16 | `EAB_REL.BIN` | `0x80060000` | `0x80062054` | 8276 | 24 | `main/script_interp.c:1844` |

(\*) `vs` incluye 3 archivos (`vs_intro`, `vs_combat`, `vs_select`) que en USA están linkeados en
`main`, no en `VS_REL.BIN`.

(\*\*) **Hipótesis a validar:** en USA el código de `shop/script_menu.c` está linkeado dentro de
`main` (`config/us/main.yaml:132`, `config/us/main.ld:156`) y `shop_START` es una constante
`0x80080800` (`src/main/utils2.c:24`), mientras que en EU `SHOP` es un overlay de verdad
(`config/eu/overlay.ld:185`). `SHOP_REL.BIN` existe en el disco USA y se sigue cargando; falta
confirmar si en USA su contenido se usa o es un resto. Validar con un breakpoint de lectura en
`0x80080800` en el emulador de referencia.

## Regiones y exclusión mutua

Agrupado por rangos que se pisan (si dos rangos se solapan, no pueden estar cargados a la vez):

- `0x80010000-0x80083428`: **MOV** pisa casi toda la región; se carga solo para reproducir un video
  (`startMovie` hace `CdInit` / `ResetGraph` antes y `ResetGraph(3)` después).
- `0x80052AE0-...`: **STD**, **BTL** y **VS** (mismo inicio) se excluyen entre sí. **KAR**
  (`0x80053800`) también se solapa con ellos.
- `0x80060000-...`: **EVL**, **EAB**, **ENDI** se excluyen entre sí y se solapan con STD/BTL/VS.
- `0x80070000-...`: **FISH** y **DOO2**; solapan con STD.
- `0x8007C000`: **MURD**, solapa con STD (hasta `0x8007FEC8`).
- `0x80080000`/`0x80080800`: **DOOA**, **SHOP**, **DGET**; no solapan con STD, sí entre sí.
- `0x80088800`: **TRN**, **TRN2**; no solapan con STD, sí entre sí.

Coexistencias posibles según las direcciones: STD + (DOOA | SHOP | DGET) + (TRN | TRN2).

## Uso de la región de overlays como buffer

La región baja también se usa como memoria temporal por dirección absoluta, por ejemplo
`0x80010000` como destino de lecturas de bancos de sonido (`readVHBFile` / `readVHBFileSectors` en
`main/sound.c`: `loadMusicFont`, `loadDigimonSounds`, `VSLoadSounds`); ver `hardware-access.md`.
En el port esa región tiene que existir como memoria real (arena de RAM PS1), aunque el código de
los overlays sea nativo.

## Dependencias

- Todos los overlays dependen de `main` (llamadas directas a funciones y datos de `main` por
  dirección absoluta resuelta en el link).
- Overlay → overlay: `DOOA` carga `DOO2` (`dooa/dooa.c:365`); `MURD` carga `DOOA`
  (`murd/murd.c:788`). No se encontraron llamadas directas entre overlays (el linker script usa
  `NOCROSSREFS`).
- Los overlays incluyen datos binarios del disco embebidos como `.rodata` en asm
  (`asm/us/<ovl>/data/texture.rodata.s`, `model_*.rodata.s`, `header.rodata.s`, ver
  `config/us/btl.yaml`). En el port esos bloques **no pueden compilarse dentro del `.wasm`**
  (son datos del juego): se cargan del pack en tiempo de ejecución.

## Implicaciones para el port (insumo del ADR de overlays)

1. Linkear el código y datos de los 16 overlays en un único binario (como DW2-PC), con
   `.data`/`.bss` de cada overlay agrupados en un rango propio.
2. `loadDynamicLibrary` en el port: restaurar `.data` a su imagen inicial y poner `.bss` a cero
   (lo que hacía la recarga), cargar del pack los bloques de datos embebidos y marcar
   `*isComplete` (inmediato o diferido, para respetar el timing async).
3. Llamadas a funciones de overlay desde `main` por puntero: verificar en `src/` que todas pasen
   por símbolos y no por direcciones numéricas (pendiente, ver `hardware-access.md`).
