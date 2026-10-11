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
