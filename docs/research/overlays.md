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
- `isAsync != 0`: `addFileReadRequestPath()` encola la lectura; la bandera (llamada `isComplete` en la firma) es en realidad "en curso": `file_queue.c` la pone en 1 al encolar y en 0 al terminar (`src/main/file_queue.c:174,241`). Corregido el 2026-10-11 tras la revisión de arquitectura.

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
| 11 | `SHOP_REL.BIN` | `0x80080800` | `0x80088610` | 32272 | 65 (en `main`) | `checkShopMap` desde `newGameScene` confirmado; `loadShopLibrary` estático. Ver evidencia 3.11 abajo. |
| 12 | `DGET_REL.BIN` | `0x80080800` | `0x80081CF8` | 5368 | 13 | `main/script_common.c:3097` |
| 13 | `TRN2_REL.BIN` | `0x80088800` | `0x8008DCC0` | 21696 | 40 | `main/map.c:5466` |
| 14 | `MURD_REL.BIN` | `0x8007C000` | `0x8007E960` | 10592 | 15 | `main/tamer.c:1703, 1738` |
| 15 | `ENDI_REL.BIN` | `0x80060000` | `0x80061534` | 5428 | 16 | `main/script_interp.c:1850` (final, async) |
| 16 | `EAB_REL.BIN` | `0x80060000` | `0x80062054` | 8276 | 24 | `main/script_interp.c:1844` |

(\*) `vs` incluye 3 archivos (`vs_intro`, `vs_combat`, `vs_select`) que en USA están linkeados en
`main`, no en `VS_REL.BIN`.

### SHOP USA: resultado de la historia 3.11

`map_load` **sí carga** `SHOP_REL.BIN`, de forma síncrona, desde
`newGameScene → checkShopMap(0xda) → loadDynamicLibrary(11) → readFile → CdRead`.
La sonda automática `tools/port/reference/shop.lua`, integrada en `capture.py`,
observa la carga real y el rango semiabierto `0x80080840–0x80088610` hasta el final
de la Escena. Los ciclos y VBlanks siguientes parten del retorno del primer
`VSync` del juego (frame 0), no de la marca `MOV_playMovie`.

| Evento | VBlank | Ciclo | `ra` |
|---|---:|---:|---|
| `checkShopMap(0xda)` desde `newGameScene` | 1138 | 641791486 | `0x800ef1f8` |
| `loadDynamicLibrary(11)` | 1138 | 641791809 | `0x800ff980` |
| `readFile` a `0x80080800` | 1138 | 641791847 | `0x800e6228` |
| `CdRead`, 16 sectores, 32768 bytes con padding | 1138 | 642019276 | `0x800a3474` |
| Retorno de `checkShopMap`, trigger `0x29` desactivado | 1171 | 660530961 | `0x800ef1f8` |
| `checkShopMap(0xcc)` desde `loadMap` | 9535 | 5381370524 | `0x800d5df0` |
| Retorno sin carga: `0xcc` no está en `SHOP_MAPS`; trigger desactivado | 9535 | 5381370854 | `0x800d5df0` |

El archivo mide 32272 bytes; la transferencia CD incluye el padding del último
sector. Las dos corridas de la sonda produjeron JSONL idénticos. Solo hubo
**una carga**, **cero ejecuciones** y **5389533 lecturas** de la región, hasta
VBlank 9536, ciclo 5381555041. La traza temporal coincide también con la
referencia final sin la sonda. No se debe describir como una región sin lecturas:

| PC lector | Función (asm USA `psyq/libpress.s`) | Lecturas |
|---|---|---:|
| `0x800e9a5c` | `DecDCTvlc2` | 2224461 |
| `0x800e9a9c` | `DecDCTvlc2` | 919956 |
| `0x800e9aac` | `DecDCTvlc2` | 2224497 |
| `0x800e9c0c` | `DecDCTvlcBuild` | 9387 |
| `0x800e9c7c` | `DecDCTvlcBuild` | 5618 |
| `0x800e9c80` | `DecDCTvlcBuild` | 5614 |

La atribución al video se apoya en esos PCs y en `src/mov/mov.c`: `MOV_playMovie`
construye la tabla y `MOV_decodeFrame` la consulta. `MOV_VLC_TABLE` ocupa
`0x80072424–0x80083428` (`config/us/symbols_mov.txt`), superpuesto con SHOP;
además, cargar MOV pisa `0x80010000–0x80083428`. Estos conteos observan la región
física durante toda la Escena, también después de reutilizarla; **no prueban que
las lecturas consuman datos originales de SHOP**.

El análisis estático reproducible `tools/port/metrics/shop_overlay.py` verifica
las 134296 palabras de texto de `main` contra el asm USA y lista:

- Seis `jal` dentro del rango compartido: `0x800aadac`, `0x800de7bc` y
  `0x800de800` llaman a `DOOA_tick` (`0x80084644`); `0x800dec30` y `0x800dec48`
  a `DOOA_getSequenceState` (`0x80084810`); `0x80105ddc` a
  `initTournamentSchedule` de DGET (`0x80081a80`). Ninguna identifica una entrada
  de la tienda de `main`.
- Un candidato lineal al rango: `lui` en `0x800a51e8` y `addiu` en
  `0x800a51f8`, en el delay slot de `jal applyMMD`, forman `0x8008788c`.
  El asm lo identifica como `DOOA_REINCARNATION_SEQ + 0x24`.
  Dos palabras de datos,
  `0x8012b994` y `0x8012b998`, contienen `0x80080800`: las entradas SHOP y DGET
  de `REL_BIN_OFFSETS`. El análisis no resuelve todos los punteros calculados.
- `shop/script_menu` está en `main`, `0x80106d28–0x8010cb3c` (6021 instrucciones).
  Comparado con el código de SHOP desde `+0x40` hasta el último `jr ra` y su delay
  slot (7688 palabras), solo coincide una secuencia normalizada de al menos 16
  instrucciones: 18 palabras en `main:0x8010bf94` / `SHOP:0x800871e4`.
  Se enmascaran destinos J/JAL, pares de relocación RAM separados por hasta
  cuatro instrucciones y offsets de GP; las constantes ordinarias se conservan.
  **El comparador no confirma que sea una copia reubicada**, ni esa coincidencia
  demuestra equivalencia.
  El límite de código del overlay se infiere del último retorno, como declara
  el informe; no sustituye un mapa de símbolos propio de ese archivo USA.

Resultado acotado: la carga de SHOP persiste en USA y no se observa ejecución
suya en `map_load`; la región se reutiliza para video. No hay base para eliminar
la carga ni declarar el archivo inútil en toda la partida. `map_load` no entra
a una tienda. `loadShopLibrary` (comando de script `0x23`) queda cubierto **solo
por análisis estático**: encola `loadDynamicLibrary(11, …, 1, …)`; no se ejecutó
ese comando en esta investigación. No se cambió `src/` ni `REL_BIN_OFFSETS`.

Reproducción dentro del contenedor, con el disco privado y salidas externas:

```sh
export DW1_DISC_DIR=/dw/game_rom/extracted
export DW1_REFERENCE_DIR=/tmp/dw-reference
python3 tools/port/reference/capture.py map_load --shop-probe --state \
  --out "$DW1_REFERENCE_DIR/map_load" --max-vblanks 40000
python3 tools/port/metrics/shop_overlay.py
cmake --build --preset native-headless -j 2
python3 tools/port/scenes/run.py map_load --build native-headless --compare
ctest --preset native-headless -R 'map_load|reference_capture_unit|shop_overlay_unit|scene_(runner|flags|state_unit|timing_unit|audio_unit|audio_runtime)$'
```

Los JSONL detallados quedan fuera del repo. El resumen versionado, los hashes,
la comparación de checkpoints (incluidos `checkShopMap` y `checkShopMap#2`) y
los resultados de tests están en
`docs/implementation-artifacts/evidence/story-3-11.log`. Se conserva el hash
visual fijado de `map_load`: agregar observadores no altera el juego ni el
punto final de captura.

Observación del linker script (Historia 2.4): en USA `dget_START` también vale `0x80080800`
(`config/us/overlay.ld`), así que `SHOP` y `DGET` usan la misma región; `shop_START` no sale del
linker script sino de la constante a mano de `utils2.c`, que el port traduce con `PS1_RAM_ADDR`.

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
