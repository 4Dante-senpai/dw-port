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
| `src/main/script_interp.c` | `port_script_animation_residue` | Reconstruir el byte de a1 que consume `setActiveAnim` tras `scriptNPCStartAnimation` | Asm de las llamadas, `startAnimation`, `setupModelMatrix` y `ScaleMatrix`; sonda de referencia en `setActiveAnim` |
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

## Historia 3.3: búsqueda de Jijimon con slots vacíos

`src/shop/script_menu.c`: las tres búsquedas de `ENTITY_TABLE[i]->type == 0x75`
comprueban NULL solo bajo `DW1_PORT`. New Game deja los slots 3..9 en NULL; el original
lee RAM en la dirección 0 y el nativo disparaba UBSan en `setupNewGameDialogueBox`.
La sonda del emulador fijado con OpenBIOS observó `Entity.type` en RAM[0] = 0 al primer
`VSync` (`build/reference/null-probe/log.txt`). El build PS1 conserva la expresión original.
La referencia sigue siendo provisional respecto del BIOS real del dueño.

`NAMING_CTRL_BOXES` agrega dos shorts solo bajo `DW1_PORT` para representar la lectura
contigua de `NAMING_ROLLOVER_CHARS[0..1]` que hace `renderNamingBox` en el original.
ASan detectó la lectura fuera del array de 10 elementos al entrar al selector de
nombres. Se verificaron los valores `0x8000, 0x8000` y la contigüidad en
`asm/us/main/shop/script_menu.data.s` (`0x801307A0..A2`), y la lectura en
`asm/us/shop/script_menu.s:renderNamingBox`. No cambia el array del build PS1.

## Historia 3.3: áreas de modelos y escritura en RAM baja

`src/main/main.c` conserva bajo `DW1_PORT` una única área de `0xe000` bytes para
`TAMER_MODEL_BUFFER` y el área que comienza a `+0x7800`. `MAIN_D_801A8B98` apunta a
esa segunda parte; `src/vs/vs_hud.c` adapta su declaración solo para el port.
`BOYS.MMD` ocupa 57036 bytes, más que los `0x7800` del primer array. En el original,
`config/us/symbols.txt` fija las direcciones `0x801A1398`, `0x801A8B98` y el siguiente
objeto `MAP_COLLISION_DATA` en `0x801AF398`: ambos espacios forman un bloque contiguo.
El layout de datos de `asm/us/main/data/` y las referencias a ambos buffers se
conservan; los arrays del build PS1 mantienen su tamaño original.

`src/main/overworld.c:clearMapDigimon` pone a cero `isOnMap` de la entidad sobre la
arena PS1, después de borrar el slot. El asm original almacena NULL en
`ENTITY_TABLE[i+2]` (`800B64C4`), lo vuelve a cargar (`800B64C8`) y escribe el byte
`0x34` a través de ese cero (`800B64D0`). El cambio traduce esa escritura a la RAM
baja del port; no accede al objeto que ocupaba antes el slot ni omite el efecto.
La expresión original permanece en la rama PS1.

`renderMapOverlays` omite primero `currentFrame == -1` bajo `DW1_PORT`.
El asm (`800B4480..44B8`) lee antes `animSprites[-1]` y luego salta incondicionalmente
ese objeto cuando el frame es -1. La comprobación previa evita el acceso
fuera del array detectado por UBSan sin cambiar qué objetos se dibujan. Se adelanta
también el wrap a cero de `currentFrame >= 8`: los dos caminos del asm
(`800B46A8..46C8`, `800B471C..4738`) descartan esa lectura de índice 8 y siempre
escriben cero. Las expresiones originales se conservan intactas en PS1.

`script_interp.c` reconstruye el segundo argumento de `setActiveAnim`, que el
original no prepara después de `scriptNPCStartAnimation` (`80105538..5544` y
`801054C4..54D0`). Sin NPC coincidente, a1 conserva la animación; con offset de
animación cero, `startAnimation` deja a1=0 (`800C1A48`). En el resto de casos,
la última llamada a `setupModelMatrix` pasa la escala del último hueso a
`ScaleMatrix` (`800C19E0`), cuya implementación (`8009B0C0..B1F4`) no cambia a1.
El port usa el byte bajo de la dirección original dentro del heap PS1, traduciendo
el puntero de la arena, y mantiene un trap para almacenamiento fuera de ella.
Las dos llamadas y la declaración PS1 se conservan bajo la rama sin `DW1_PORT`.
Una sonda en `setActiveAnim` del original observó a0=12, a1=0x801D9548
(byte 72, igual al primer caso nativo), seguido de 0x801D9EB8 y 0x801D86B8
(byte 184): `build/reference/animation-check/run-1.log`.


`mov.c:MOV_playMovie` inicializa a cero el `DRAWENV` local solo en el port.
El asm reserva ese objeto en sp+0x40 y lo pasa a `PutDrawEnv` en `800108A4`
sin inicializarlo. En ASan, sus bytes indeterminados activaban `isbg` con un
rectángulo que alcanzaba toda la VRAM, incluyendo las texturas del título.
Un watchpoint en VRAM[0][768] localizó la primera escritura en `PutDrawEnv`
desde esa llamada (`build/title-watch.log`). La inicialización evita ejecutar
comandos GPU derivados de memoria indeterminada; la rama PS1 conserva la
declaración original. `scene_title` y `scene_title_compare` pasan también en
ASan, con cero píxeles RGB555 distintos respecto de la referencia.
