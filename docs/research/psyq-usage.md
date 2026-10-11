# Funciones PsyQ que usa el juego (USA)

Medido el 2026-10-11 sobre `upstream/main` @ `09c174c` con
`tools/port/metrics/psyq_usage.py us` (después de `make regenerate`).

## Método

- Funciones PsyQ = `glabel` de `asm/us/main/psyq/<lib>.s` (las 12 librerías linkeadas en
  `SLUS_010.32`).
- Llamadas = instrucciones `jal` / `j` hacia esas funciones en el código **original** de cada
  función del juego (los `.s` por función que splat genera para los subsegmentos C; 2741
  funciones de `main` y los 15 overlays). Las macros de `libgpu.h` / `gte.h` que se expanden
  inline (`setRGB0`, `gte_*`) no son llamadas y no aparecen aquí: los accesos GTE inline se
  cuentan en `hardware-access.md`.
- "Definidas en DW2-PC": hay una definición C con ese nombre en `reference/dw2-pc/psyq/*.c`.
  Es un indicador de reutilización, no garantía de compatibilidad (DW2 usa PsyQ 4.x con GCC; DW1
  se linkeó con `mw_lib41`, ver `mk/version/us.mk`).

## Resumen y detalle por librería

| Librería | Funciones en el binario | Funciones llamadas por el juego | Llamadas (jal/j) | Referencias por dirección | Definidas en DW2-PC |
|---|---:|---:|---:|---:|---:|
| libapi | 54 | 3 | 13 | 0 | 0 |
| libc | 20 | 16 | 430 | 0 | 0 |
| libcd | 64 | 16 | 44 | 0 | 12 |
| libds | 2 | 0 | 0 | 0 | 0 |
| libetc | 26 | 4 | 32 | 0 | 2 |
| libgpu | 71 | 28 | 826 | 0 | 13 |
| libgs | 49 | 35 | 960 | 0 | 13 |
| libgte | 81 | 56 | 554 | 54 | 15 |
| libmcrd | 66 | 11 | 61 | 0 | 9 |
| libpress | 15 | 6 | 8 | 0 | 5 |
| libsnd | 119 | 21 | 41 | 0 | 8 |
| libspu | 45 | 6 | 7 | 0 | 4 |
| **total** | 612 | 202 | 2976 | 54 | 81 |

### libapi

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `malloc3` | 7 | 0 | main, std | no |
| `free3` | 5 | 0 | main, std | no |
| `InitHeap3` | 1 | 0 | main | no |

### libc

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `rand` | 154 | 0 | btl, doo2, dooa, endi, evl, kar, main, std, trn, trn2, vs | no |
| `memcpy` | 52 | 0 | btl, main, std, vs | no |
| `strlen` | 50 | 0 | btl, dget, main, shop, std, trn, vs | no |
| `strcpy` | 48 | 0 | btl, dget, main, shop, vs | no |
| `longjmp` | 36 | 0 | main | no |
| `strcat` | 33 | 0 | btl, main | no |
| `abs` | 25 | 0 | btl, kar, main, std, vs | no |
| `exit` | 7 | 0 | btl, fish, std, vs | no |
| `sprintf` | 7 | 0 | fish, main | no |
| `strrchr` | 7 | 0 | main | no |
| `printf` | 3 | 0 | btl, std, vs | no |
| `strncpy` | 3 | 0 | main | no |
| `memset` | 2 | 0 | dget, main | no |
| `bzero` | 1 | 0 | main | no |
| `setjmp` | 1 | 0 | main | no |
| `strcmp` | 1 | 0 | main | no |

### libcd

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `CdControl` | 9 | 0 | main, mov | sí |
| `CdReadSync` | 6 | 0 | main | no |
| `CdIntToPos` | 5 | 0 | main | sí |
| `CdSearchFile` | 5 | 0 | main, mov | no |
| `CdPosToInt` | 4 | 0 | main | sí |
| `CdRead` | 4 | 0 | main | no |
| `CdInit` | 2 | 0 | main | sí |
| `CdControlB` | 1 | 0 | mov | sí |
| `CdRead2` | 1 | 0 | mov | sí |
| `StCdInterrupt` | 1 | 0 | mov | sí |
| `StFreeRing` | 1 | 0 | mov | sí |
| `StGetBackloc` | 1 | 0 | mov | no |
| `StGetNext` | 1 | 0 | mov | sí |
| `StSetRing` | 1 | 0 | mov | sí |
| `StSetStream` | 1 | 0 | mov | sí |
| `StUnSetRing` | 1 | 0 | mov | sí |

### libetc

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `VSync` | 15 | 0 | btl, fish, main, mov, std, vs | sí |
| `PadRead` | 14 | 0 | btl, main, mov, std, vs | no |
| `ResetCallback` | 2 | 0 | main | sí |
| `PadInit` | 1 | 0 | main | no |

### libgpu

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `AddPrim` | 173 | 0 | btl, doo2, dooa, eab, evl, kar, main, murd, shop, std, trn, trn2, vs | sí |
| `GetClut` | 150 | 0 | btl, dooa, fish, kar, main, shop, std, vs | no |
| `DrawSync` | 134 | 0 | btl, doo2, dooa, endi, evl, fish, main, mov, murd, std, trn, trn2, vs | sí |
| `SetPolyFT4` | 117 | 0 | btl, doo2, dooa, eab, evl, kar, main, murd, shop, std, vs | no |
| `LoadImage` | 62 | 0 | btl, doo2, dooa, endi, evl, main, mov, std, vs | sí |
| `SetSemiTrans` | 47 | 0 | btl, doo2, dooa, eab, evl, main, murd, std, vs | no |
| `GetTPage` | 43 | 0 | btl, dooa, main, shop, std, vs | no |
| `StoreImage` | 15 | 0 | doo2, dooa, endi, evl, main, murd | sí |
| `SetPolyF4` | 14 | 0 | btl, main, std, vs | sí |
| `ClearImage` | 13 | 0 | main, mov, std | sí |
| `SetPolyFT3` | 9 | 0 | btl, doo2, dooa, evl, std, vs | no |
| `LoadClut` | 7 | 0 | main | no |
| `MoveImage` | 6 | 0 | fish, main, std | sí |
| `ResetGraph` | 6 | 0 | main | sí |
| `SetLineF2` | 5 | 0 | btl, eab, evl, std, vs | no |
| `SetDispMask` | 3 | 0 | main, mov | sí |
| `SetDrawLoad` | 3 | 0 | btl, std, vs | no |
| `SetLineF4` | 3 | 0 | btl, std, vs | no |
| `OpenTIM` | 2 | 0 | main | no |
| `ReadTIM` | 2 | 0 | main | no |
| `SetDrawOffset` | 2 | 0 | main | no |
| `SetGraphDebug` | 2 | 0 | main | sí |
| `SetLineF3` | 2 | 0 | main | no |
| `SetPolyGT4` | 2 | 0 | std, vs | no |
| `PutDispEnv` | 1 | 0 | mov | sí |
| `PutDrawEnv` | 1 | 0 | mov | sí |
| `SetDefDispEnv` | 1 | 0 | mov | sí |
| `SetPolyG4` | 1 | 0 | main | no |

### libgs

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `GsGetWorkBase` | 177 | 0 | btl, doo2, dooa, eab, evl, fish, kar, main, murd, shop, std, trn, trn2, vs | no |
| `GsSetWorkBase` | 168 | 0 | btl, doo2, dooa, eab, evl, fish, kar, main, murd, shop, std, trn, trn2, vs | no |
| `GsSetLsMatrix` | 63 | 0 | btl, dooa, fish, kar, main, murd, std, vs | sí |
| `GsSetProjection` | 49 | 0 | btl, dooa, kar, main, std, vs | sí |
| `GsSortSprite` | 45 | 0 | btl, doo2, main, murd, std, trn, trn2, vs | no |
| `GsSortObject4J` | 42 | 0 | btl, dooa, fish, kar, main, murd, std, vs | no |
| `GsSetLightMatrix` | 41 | 0 | btl, dooa, fish, kar, main, murd, std, vs | no |
| `GsMapModelingData` | 35 | 0 | btl, dooa, eab, fish, kar, main, std, vs | no |
| `GsSetRefView2` | 35 | 0 | btl, dooa, kar, main, std, vs | sí |
| `GsClearOt` | 32 | 0 | btl, dooa, kar, main, murd, std, vs | no |
| `GsLinkObject4` | 30 | 0 | btl, dooa, fish, kar, main, std, vs | no |
| `GsInitCoordinate2` | 23 | 0 | dooa, kar, main, std, vs | sí |
| `GsMulCoord0` | 21 | 0 | btl, fish, std, vs | no |
| `GsSortOt` | 21 | 0 | dooa, kar, main, murd, std, vs | no |
| `GsGetTimInfo` | 20 | 0 | btl, dooa, main, std, vs | sí |
| `GsSortBoxFill` | 19 | 0 | btl, main, shop, std, trn, trn2, vs | no |
| `GsSetFlatLight` | 16 | 0 | main, murd, std, vs | sí |
| `GsGetLs` | 15 | 0 | dooa, kar, main, murd, std, vs | sí |
| `GsGetLw` | 15 | 0 | dooa, kar, main, murd, std, vs | sí |
| `GsSetView2` | 14 | 0 | std, vs | no |
| `GsGetActiveBuff` | 11 | 0 | btl, main, std, vs | no |
| `GsDrawOt` | 10 | 0 | btl, main, std, vs | no |
| `GsSortPoly` | 10 | 0 | btl, main, std, vs | no |
| `GsSwapDispBuff` | 10 | 0 | btl, main, std, vs | no |
| `GsGetLws` | 9 | 0 | dooa, main, std, vs | no |
| `GsSetOrign` | 9 | 0 | btl, main, std, vs | no |
| `GsSortClear` | 8 | 0 | btl, main, std, vs | no |
| `GsSetAmbient` | 4 | 0 | main, std, vs | sí |
| `GsSetLightMode` | 2 | 0 | std, vs | sí |
| `GsDefDispBuff` | 1 | 0 | main | no |
| `GsInit3D` | 1 | 0 | main | sí |
| `GsInitGraph` | 1 | 0 | main | sí |
| `GsMulCoord3` | 1 | 0 | main | sí |
| `GsSetNearClip` | 1 | 0 | main | no |
| `GsSortFastSprite` | 1 | 0 | btl | no |

### libgte

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `ApplyMatrixSV` | 103 | 0 | btl, doo2, dooa, endi, evl, kar, main, std, vs | sí |
| `RotMatrix` | 51 | 0 | btl, fish, kar, main, std, vs | no |
| `RotMatrixYXZ` | 44 | 0 | btl, fish, main, std, vs | sí |
| `RotMatrixZYX` | 44 | 0 | btl, doo2, dooa, eab, evl, main, std, vs | no |
| `ratan2` | 43 | 0 | fish, kar, main | sí |
| `ScaleMatrix` | 39 | 0 | btl, fish, main, std, vs | sí |
| `TransMatrix` | 37 | 0 | btl, fish, kar, main, std, vs | no |
| `rsin` | 27 | 0 | btl, fish, kar, main, std, vs | sí |
| `rcos` | 24 | 0 | btl, fish, kar, main, std, vs | sí |
| `ApplyMatrixLV` | 23 | 0 | btl, dooa, eab, evl, fish, main, std, vs | sí |
| `SetRotMatrix` | 16 | 0 | btl, fish, main, std, vs | sí |
| `SetTransMatrix` | 16 | 0 | btl, fish, main, std, vs | sí |
| `PopMatrix` | 12 | 0 | btl, std, vs | sí |
| `PushMatrix` | 12 | 0 | btl, std, vs | sí |
| `MulMatrix0` | 9 | 0 | btl, doo2, fish, main, std, vs | no |
| `RotTransPers4` | 9 | 0 | btl, fish, main, std, vs | no |
| `ApplyMatrix` | 8 | 0 | fish, main | no |
| `TransposeMatrix` | 7 | 0 | btl, main, std, vs | sí |
| `NormalColorCol3` | 4 | 0 | doo2, main | no |
| `RotNclip3` | 4 | 0 | doo2, main | no |
| `RotNclip4` | 4 | 0 | doo2, main | no |
| `ApplyTransposeMatrixLV` | 3 | 0 | fish | no |
| `RotTransPers` | 3 | 0 | btl, std, vs | sí |
| `SquareRoot0` | 3 | 0 | kar, std, vs | sí |
| `CompMatrix` | 2 | 0 | doo2, main | no |
| `MulMatrix` | 2 | 0 | fish | sí |
| `NormalColorCol` | 2 | 0 | doo2, main | no |
| `SetLightMatrix` | 2 | 0 | doo2, main | no |
| `RotTransPers3` | 1 | 0 | main | no |
| `GsTMDdivTF3NL` | 0 | 2 | main | no |
| `GsTMDdivTF4L` | 0 | 2 | main | no |
| `GsTMDdivTF4NL` | 0 | 2 | main | no |
| `GsTMDdivTG3NL` | 0 | 2 | main | no |
| `GsTMDdivTG4NL` | 0 | 2 | main | no |
| `GsTMDdivTNF3` | 0 | 2 | main | no |
| `GsTMDdivTNF4` | 0 | 2 | main | no |
| `GsTMDdivTNG3` | 0 | 2 | main | no |
| `GsTMDdivTNG4` | 0 | 2 | main | no |
| `GsTMDfastF3L` | 0 | 2 | main | no |
| `GsTMDfastF4L` | 0 | 2 | main | no |
| `GsTMDfastF4NL` | 0 | 2 | main | no |
| `GsTMDfastG3L` | 0 | 2 | main | no |
| `GsTMDfastG4L` | 0 | 2 | main | no |
| `GsTMDfastNF4` | 0 | 2 | main | no |
| `GsTMDfastTF3L` | 0 | 2 | main | no |
| `GsTMDfastTF3NL` | 0 | 2 | main | no |
| `GsTMDfastTF4L` | 0 | 2 | main | no |
| `GsTMDfastTF4NL` | 0 | 2 | main | no |
| `GsTMDfastTG3L` | 0 | 2 | main | no |
| `GsTMDfastTG3NL` | 0 | 2 | main | no |
| `GsTMDfastTG4L` | 0 | 2 | main | no |
| `GsTMDfastTG4NL` | 0 | 2 | main | no |
| `GsTMDfastTNF3` | 0 | 2 | main | no |
| `GsTMDfastTNF4` | 0 | 2 | main | no |
| `GsTMDfastTNG3` | 0 | 2 | main | no |
| `GsTMDfastTNG4` | 0 | 2 | main | no |

### libmcrd

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `MemCardSync` | 36 | 0 | main | sí |
| `MemCardAccept` | 6 | 0 | main | sí |
| `MemCardExist` | 4 | 0 | main | sí |
| `MemCardGetDirentry` | 4 | 0 | main | no |
| `MemCardReadFile` | 3 | 0 | main | sí |
| `MemCardWriteFile` | 3 | 0 | main | sí |
| `MemCardCreateFile` | 1 | 0 | main | sí |
| `MemCardDeleteFile` | 1 | 0 | main | no |
| `MemCardFormat` | 1 | 0 | main | sí |
| `MemCardInit` | 1 | 0 | main | sí |
| `MemCardStart` | 1 | 0 | main | sí |

### libpress

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `DecDCTout` | 2 | 0 | mov | sí |
| `DecDCToutCallback` | 2 | 0 | mov | sí |
| `DecDCTReset` | 1 | 0 | mov | sí |
| `DecDCTin` | 1 | 0 | mov | sí |
| `DecDCTvlc2` | 1 | 0 | mov | sí |
| `DecDCTvlcBuild` | 1 | 0 | mov | no |

### libsnd

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `SsVabClose` | 5 | 0 | main | sí |
| `SsUtGetVBaddrInSB` | 4 | 0 | main | no |
| `SsVabOpenHeadSticky` | 4 | 0 | main | no |
| `SsVabTransBody` | 4 | 0 | main | sí |
| `SsVabTransCompleted` | 4 | 0 | main | sí |
| `SsInit` | 2 | 0 | main | sí |
| `SsSeqPlay` | 2 | 0 | main | no |
| `SsUtAutoVol` | 2 | 0 | main | no |
| `SsUtKeyOffV` | 2 | 0 | main | no |
| `SsEnd` | 1 | 0 | main | no |
| `SsQuit` | 1 | 0 | main | no |
| `SsSeqClose` | 1 | 0 | main | no |
| `SsSeqOpen` | 1 | 0 | main | no |
| `SsSeqSetVol` | 1 | 0 | main | no |
| `SsSeqStop` | 1 | 0 | main | no |
| `SsSetMVol` | 1 | 0 | main | sí |
| `SsSetTableSize` | 1 | 0 | main | sí |
| `SsSetTickMode` | 1 | 0 | main | sí |
| `SsStart` | 1 | 0 | main | no |
| `SsUtAllKeyOff` | 1 | 0 | main | sí |
| `SsUtKeyOnV` | 1 | 0 | main | no |

### libspu

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `SpuGetKeyStatus` | 2 | 0 | main | no |
| `SpuClearReverbWorkArea` | 1 | 0 | main | sí |
| `SpuSetReverb` | 1 | 0 | main | sí |
| `SpuSetReverbDepth` | 1 | 0 | main | no |
| `SpuSetReverbModeParam` | 1 | 0 | main | sí |
| `SpuSetReverbVoice` | 1 | 0 | main | sí |

## Observaciones

- **202 funciones PsyQ distintas, 2976 llamadas.** `libgs` (960), `libgpu` (826), `libgte`
  (554) y `libc` (430) concentran el 93 % de las llamadas.
- **DW2-PC cubre 81 de las 202** por nombre. Lo que falta es sobre todo:
  - `libgs`: el pipeline de TMD (`GsLinkObject4`, `GsSortObject4J`, `GsMapModelingData` y las
    27 funciones `GsTMDfast*` / `GsTMDdiv*`, que splat cuenta bajo `libgte` por su límite de archivo), los OT de `libgs` (`GsClearOt`, `GsSortOt`,
    `GsDrawOt`, `GsSortSprite`, `GsSortPoly`, ...) y el doble buffer (`GsDefDispBuff`,
    `GsSwapDispBuff`). DW2 casi no usa `libgs`; DW1 dibuja sus modelos 3D con él. Es el bloque
    nuevo más grande de la capa PsyQ.
  - `libgte`: funciones de matriz y proyección (`RotMatrix`, `RotTransPers3/4`, `RotNclip3/4`,
    `NormalColorCol*`, ...) sobre el modelo de GTE que DW2-PC ya tiene (`psyq/gte.c`).
  - `libsnd`: el secuenciador SEQ (`SsSeqOpen/Play/Stop/Close/SetVol`, `SsStart`, `SsEnd`,
    `SsUtKeyOnV/OffV`, `SsUtAutoVol`, `SsVabOpenHeadSticky`). DW2-PC implementa el secuenciador
    de su propio formato de música; hay que verificar si DW1 usa SEQ estándar.
  - `libetc`: DW1 lee el pad con `PadInit` / `PadRead` (interfaz vieja de `libetc`), no con
    `libpad` como DW2.
  - `libcd`: DW1 busca archivos por nombre ISO9660 (`CdSearchFile`) y lee con `CdRead` /
    `CdReadSync`. DW2 lee por LBA desde una tabla. El pack de DW1 tiene que indexar por ruta.
  - `libapi`: `InitHeap3` / `malloc3` / `free3` (heap del juego).
  - `libc`: `setjmp` / `longjmp` (36 `longjmp` en `main`) y `rand` (154 llamadas). `rand`
    tiene que reproducir el LCG de PsyQ bit a bit (determinismo de combates y eventos);
    `setjmp/longjmp` en WASM requieren soporte de Emscripten (`-sSUPPORT_LONGJMP`, ver
    `web-constraints.md`).
- `libds` está linkeada (2 funciones) pero el juego no la llama.
- Las 54 "referencias por dirección" de `libgte` son 27 funciones `GsTMDfast*` / `GsTMDdiv*`
  tomadas por dirección (`%hi` + `%lo` = 2 por función): el juego arma su propia tabla de
  punteros a los dibujantes de primitivas TMD que usa `GsSortObject4J`. Son código de `libgs`
  aunque splat las ubique en el archivo `libgte.s` (límite de archivo aproximado). En el port tienen
  que existir como funciones C con esos nombres para que la tabla siga siendo válida.
