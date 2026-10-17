# Cambios en `src/` respecto de upstream

Registro de todo cambio del port en archivos de upstream (`src/`, `include/`, `config/`, `mk/`,
`Makefile`). Cada cambio va detrás de `#ifdef DW1_PORT` y no altera el build PS1
(regla 2 y 3 de `CLAUDE.md`).

| Fecha | Archivo | Función / símbolo | Motivo | Historia | Verificación |
|---|---|---|---|---|---|
| 2026-10-11 | `include/dw/main.h` | `GENERAL_BUFFER`, `TEXTURE_BUFFER`, `BOSS_EFE_TMD_BUFFER` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1); `overlay_probe` (`ENDI_FADE_CLUT_BUFFER`, `ARRAY_SECTION_START`, `BOSS_EFE_TMD`) |
| 2026-10-11 | `include/dw/vs.h` | `VS_FINISHER_TIM`, `VS_FINISHER_MODEL`, `VS_CONFUSION_MODEL`, `VS_STUN_MODEL`, `VS_BUFF_MODEL` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/main/btl.c` | `BTL_FINISHER_TIM`, `BTL_FINISHER_MODEL`, `BTL_CONFUSION_MODEL`, `BTL_STUN_MODEL`, `BTL_BUFF_MODEL` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/std/std_setup.c` | `STD_FINISHER_TIM`, `STD_FINISHER_MODEL`, `STD_CONFUSION_MODEL`, `STD_STUN_MODEL`, `STD_BUFF_MODEL` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/std/std_hud.c` | `STD_TMD_BUFFER` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/vs/vs_hud.c` | `VS_TMD_BUFFER` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/murd/murd.c` | `MURD_ORDERING_TABLE_0` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/evl/evl.c` | `EVL_MMD_BUFFER` (entero: guarda el puntero nativo) | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1); `overlay_probe` (`MAIN_D_801349E4`) |
| 2026-10-11 | `src/dooa/dooa.c` | `DOOA_MMD_BUFFER`, `DOOA_SHARD_BUFFER` (enteros) | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/dooa/dooa.c` | `DOOA_ORDERING_TABLE_0`, `DOOA_ORDERING_TABLE_1` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1) |
| 2026-10-11 | `src/main/utils2.c` | `shop_START` | Dirección PS1 fija usada como puntero: pasa a `PS1_RAM_ADDR()` (arena, AD-4) | 2.4 | L0a; `ps1_literals.py` (literales de la rama del port = rama PS1); `overlay_probe` (`REL_BIN_OFFSETS[10]`) |
| 2026-10-11 | `src/main/utils2.c` | `btl_START` … `vs_START` (15 `extern char <ovl>_START[]`) | Símbolos del linker script PS1: pasan a `#include <dw1_overlay_starts.h>`, generado de `config/<version>/overlay.ld` (direcciones en la arena) | 2.4 | L0a; `test_ovl_starts.py`; `overlay_probe` (las 16 entradas de `REL_BIN_OFFSETS`) |
| 2026-10-11 | `src/main/efe.c` | `MAIN_D_8012342C` | `char[16]` con 16 caracteres y sin terminador: en la PS1 el `EFEDAT_CD_LOCATION` en cero que sigue cierra el string de `strcpy`; en el port pasa a `char[17]` | 2.6 | L0a; `frame0` bajo ASan (antes: `global-buffer-overflow` en `findEFEDATFile`) |
| 2026-10-16 | `src/main/main.c` | `runLandingScreen` | Una llamada `port_scene_mark("runLandingScreen")` con declaración local, para capturar el display desde el Host tras la primera entrada al título | 2.15 | L0a; `make compare`; escena `title` nativa y ASan/UBSan determinista |

## Historia 4.1: ABI directas para WASM

Todos estos bloques usan `DW1_PORT && !DW1_ABI_AUDIT`; la macro de auditoría
permite reproducir las declaraciones PS1 en el inventario. Las llamadas indirectas
se conservan y usan `EMULATE_FUNCTION_POINTER_CASTS`, sin adaptadores en el juego.
Las macros de aridad eliminan solo argumentos que la definición no recibe.

`handlePraiseScold(partnerState)` conserva a0: en el asm de
`partnerTickPraiseScold`, la rama de estado 2 llega a `jal handlePraiseScold`
(0x800DDE34) sin modificar a0 desde la entrada. `tickCameraMoveTo` devuelve
`checkCameraMovement`: el asm termina con un salto directo a esa función
(0x800D88F4), conservando su v0 de retorno.

| Archivo | Bloque / símbolo | Motivo | Verificación |
|---|---|---|---|
| `include/dw/world_object.h` | `removeObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_main.c` | `removeObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_main.c` | `addObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_main.c` | `BTL_initializeFinisherChargeup` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_main.c` | `BTL_addBuffRingsEffect` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_main.c` | `BTL_addAuraProjectile` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_main.c` | `removeBuffModelObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/btl/battle_setup.c` | `playSound` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/dget/dget.c` | `playSound` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/dget/dget.c` | `showTextboxReady` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/dooa/dooa.c` | `setEFEFlashOffset` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/dooa/dooa.c` | `waitForDeathMapLoading` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/dooa/dooa.c` | `fadeoutCLUT1` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/dooa/dooa.c` | `DOOA_storeDigimonY` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/evl/evl.c` | `fadeoutCLUT1` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/evl/evl.c` | `fadeoutCLUT2` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/battle_ui.c` | `BTL_tickBattleEndText` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/btl.c` | `initializeBuffModelObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/evl.c` | `loadMapSounds2` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/evl.c` | `isSoundLoaded` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/evl.c` | `loadVLALL` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/fish.c` | `triggerSeadramonCutscene` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/game_menu.c` | `activateMedalTexture` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/inventory.c` | `playSound` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/main.c` | `checkFishingMap` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/main.c` | `BTL_addBuffDiskEffect` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/main.c` | `MemCardInit` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/main.c` | `MemCardStart` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/main.c` | `initializeMusic` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/main.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/map.c` | `checkFishingMap` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/map.c` | `tickCameraMoveTo` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/map.c` | `tickCameraMoveTo` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/map.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | `playSound` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | `handleSpecialEvolutions` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | `handlePraiseScold` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | `loadMapSounds2` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | `isSoundLoaded` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | `loadMapSounds` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/partner.c` | llamada a `handlePraiseScold` | Pasar `partnerState` consumido, verificado en asm | L0a, `make compare`, enlace WASM |
| `src/main/script_interp.c` | `port_unverified_active_anim` | Trap si existe el NPC: segundo argumento original no determinado | L0a, `make compare`, enlace WASM fatal |
| `src/main/script_interp.c` | `scriptSetDigimon` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/script_interp.c` | `setupMap` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/tamer.c` | `playSound` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/tamer.c` | `getEntityScreenPos` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/tamer.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/main/ui.c` | `playSound` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/murd/murd.c` | `waitForDeathMapLoading` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/murd/murd.c` | `fadeoutCLUT1` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/std/std_main.c` | `STD_addWinScene` | Ajustar aridad a la definición; solo elimina argumentos no consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/std/std_setup.c` | `readFile` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_brain.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_def.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_hp.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_hud.c` | `worldPosToScreenPos` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_mp.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_off.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_reward.c` | `createMenuBox` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn/trn_speed.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn2/trn2_hud.c` | `worldPosToScreenPos` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/trn2/trn2_mp.c` | `checkCameraMovement` (retorno) | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/vs/vs_hud.c` | `addObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/vs/vs_hud.c` | `removeObject` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
| `src/vs/vs_scene.c` | `readFile` | Corregir declaración/retorno de llamada directa para wasm-ld, conservando parámetros consumidos | L0a, `make compare`, enlace WASM fatal |
