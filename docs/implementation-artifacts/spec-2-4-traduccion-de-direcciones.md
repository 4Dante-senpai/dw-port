---
title: 'Historia 2.4: Traducción de direcciones PS1 en src/ e include/'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: '38a399f0129d8274cd47732a8896da4fc02e50d3'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** 27 macros de `src/`/`include/` usan direcciones PS1 fijas como punteros (buffers, OTs,
modelos de overlays) y `REL_BIN_OFFSETS` usa símbolos `*_START` que define el linker script PS1;
en el port apuntarían fuera de la memoria del proceso.

**Approach:** bloques `#ifdef DW1_PORT ... #else <original> #endif` que traducen cada literal con una
macro constante `PS1_RAM_ADDR()` (válida en inicializadores estáticos), y los `*_START` con un
header generado desde `config/us/overlay.ld`. El inventario reconoce las ramas de los bloques.

## Boundaries & Constraints

**Always:** cada bloque con fila en `docs/port/src-changes.md`; L0a verde (preprocesado sin
`DW1_PORT` idéntico en gcc y mwcc); los 119 TUs compilan; tests verdes en los dos presets.

**Never:** tocar los `*_bss_us.c` (residuos, AD-4); traducir `0x80000000` usado como constante.

**Decisiones (autónomas):**
- `PS1_RAM_ADDR(addr)` = `(void *)(Ps1_Ram + ((addr) & 0x1FFFFF))`, sin assert (es constante), en
  `port/psyq/include/port_game.h`, que ya se incluye a la fuerza en cada TU del Juego.
- Macros enteras (`DOOA_MMD_BUFFER`, `EVL_MMD_BUFFER`, `DOOA_SHARD_BUFFER`) se traducen a
  `(unsigned int)PS1_RAM_ADDR(...)`: el entero guarda el puntero nativo (32 bits en ambos builds).
- `*_START`: `tools/port/units/ovl_starts.py` genera `generated/dw1_overlay_starts.h` con
  `#define <ovl>_START ((char *)PS1_RAM_ADDR(<base del OVERLAY>))`.
- Estado automático del inventario: literal en la rama `#else` de un bloque `DW1_PORT` →
  `traducido: rama PS1`; en la rama `DW1_PORT` con `PS1_RAM_ADDR` → `traducido`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Macro de puntero | `GENERAL_BUFFER` en nativo | `Ps1_Ram + 0x10000` | N/A |
| Inicializador estático | `REL_BIN_OFFSETS[0]` | `Ps1_Ram + (0x80052AE0 & 0x1FFFFF)` | N/A |
| Build PS1 | preprocesado sin `DW1_PORT` | idéntico a upstream | L0a falla si no |
| Inventario | regenerar `ps1_literals.tsv` | 0 filas `pendiente` | `--no-pending` falla si quedan |

</frozen-after-approval>

## Code Map

- `tools/port/metrics/ps1_literals.tsv` -- las 27 filas pendientes (ver Historia 2.2).
- `src/main/utils2.c:24,72-105` -- `shop_START`, `extern char *_START[]`, `REL_BIN_OFFSETS`.
- `config/us/overlay.ld` -- `OVERLAY 0x... : NOCROSSREFS { .BTL { btl_START = .; ...`.
- `src/endi/endi.c:37`, `src/main/script_common.c:251` -- inicializadores estáticos con `GENERAL_BUFFER`/`TEXTURE_BUFFER`.
- `src/evl/evl.c:199` -- `int32_t MAIN_D_801349E4 = EVL_MMD_BUFFER;` (entero).
- `src/main/model.c:3356` `loadMMD` -- offsets relativos (`mmdPtr + ((long *)mmdPtr)[0]`) + `GsMapModelingData`.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/include/port_game.h` -- `PS1_RAM_ADDR`.
- [x] `tools/port/units/ovl_starts.py` + test -- header generado; CMake lo produce en `generated/`.
- [x] `include/dw/main.h`, `include/dw/vs.h`, `src/main/btl.c`, `src/main/utils2.c`, `src/std/std_setup.c`, `src/std/std_hud.c`, `src/vs/vs_hud.c`, `src/dooa/dooa.c`, `src/murd/murd.c`, `src/evl/evl.c` -- bloques `DW1_PORT`.
- [x] `tools/port/metrics/ps1_literals.py` -- estados automáticos por rama + test.
- [x] `docs/port/src-changes.md` -- una fila por bloque.
- [x] `docs/research/hardware-access.md` -- resultado de `loadMMD`.
- [x] `port/psyq/tests/overlay_probe.c` -- `REL_BIN_OFFSETS`, `ENDI_FADE_CLUT_BUFFER`, `ARRAY_SECTION_START`, `MAIN_D_801349E4` con los objetos reales del Juego.

**Acceptance Criteria:**
- Given el árbol, when `preproc_check.sh` y `ps1_literals.py --check --no-pending`, then salen 0.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde (incluye `preproc_check`, que corre `ps1_literals.py --check --no-pending`)

## Implementation Notes

- 12 bloques `DW1_PORT` (27 literales + 15 `<ovl>_START`); `docs/port/src-changes.md` con una fila por bloque. L0a verde en gcc y mwcc.
- `PS1_RAM_ADDR` vive en `port_game.h` (incluido a la fuerza) y no incluye `ps1mem.h`: dentro de un TU del Juego `<stdio.h>` resuelve a los headers de PsyQ.
- Inventario: 85 filas, 0 `pendiente` (27 `traducido`, 27 `traducido: rama PS1`, 18 residuos, 13 constantes). Los estados de bloque `DW1_PORT` son automáticos; `merge()` ya no congela un `pendiente` viejo.
- `overlay_probe` verifica en la imagen real: `REL_BIN_OFFSETS[0/6/10]` (btl/mov/shop), `GENERAL_BUFFER` vía `ENDI_FADE_CLUT_BUFFER`, `TEXTURE_BUFFER` vía `ARRAY_SECTION_START`, `EVL_MMD_BUFFER` vía `MAIN_D_801349E4`, todos en la ventana.
- `loadMMD`: offsets relativos + `GsMapModelingData`; resultado en `docs/research/hardware-access.md` (densidad de `0x80xxxxxx` en los MMD sin explicar: se valida en M3).
- `isr_spins.tsv` regenerado solo por corrimiento de líneas en `utils2.c` (estados conservados).
- Revisión: chequeo de pares de literales por bloque, rango en compilación de `PS1_RAM_ADDR`, validaciones de `ovl_starts.py`; `native-headless` 11/11, `native-asan` 9/9.

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Nada verifica que la rama del port traduzca los mismos literales que la rama PS1 (error de copia pasaría) | high | Verificado: L0a ignora la rama del port y el probe no ve las macros locales. **patch**: `ps1_literals.py` falla si en un bloque `DW1_PORT` la lista de literales de la rama del port difiere de la rama PS1; test con un error de copia. |
| 2 | El AC "0 pendientes" no lo exige ningún test | medium | Verificado. **patch**: `ps1_literals.py --check --no-pending` en `preproc_check.sh`. |
| 3 | La regla nueva de `merge()` (pendiente → default) sin test | medium | Verificado. **patch**: caso en `test_merge_keeps_status`. |
| 4 | `PS1_RAM_ADDR` envuelve en silencio direcciones fuera de RAM | medium | Verificado. **patch**: chequeo en tiempo de compilación (`sizeof(char[-1])`) que conserva la expresión constante; test `ps1_ram_addr_range` (`WILL_FAIL`). |
| 5 | `ovl_starts.py` no valida rango, duplicados ni `OVERLAY` sin parsear; `sys.exit` en la función | medium | Verificado. **patch**: `starts()` devuelve errores; tests de los cuatro casos. |
| 6 | `shop_START` hecho a mano chocaría con uno generado en otra versión | low | **patch**: cada define generado va en `#ifndef`. |
| 7 | `port_branches` sobre líneas crudas; sin `#elif`, `#ifndef`, `# ifdef` | medium | Verificado. **patch**: usa las líneas sin comentarios y reconoce esas formas; tests. |
| 8 | `traducido` si la línea contiene `PS1_RAM_ADDR(` aunque el literal no sea su argumento | medium | Verificado. **patch**: exige que el literal sea el argumento; test con un segundo literal crudo. |
| 9 | `findings([])` escanea todo el árbol | low | **patch**: `files is None`. |
| 10 | El probe solo mira 3 de 16 `REL_BIN_OFFSETS` y no `BOSS_EFE_TMD` | medium | Verificado. **patch**: compara las 16 entradas contra el header generado y agrega `BOSS_EFE_TMD`. |
| 11 | Máscara `0x1FFFFF` duplicada entre `port_game.h` y `ps1mem.h` | low | **patch**: `_Static_assert(PS1_RAM_SIZE == 0x200000u)` en `ps1mem.c` con referencia cruzada. |
| 12 | `GsMapModelingData` del port descrito como hecho | low | Verificado (no existe aún). **patch**: marcado como hipótesis hasta la 2.13; citas por nombre de función. |
| 13 | Comentario y tabla dicen `config/us/overlay.ld` | low | **patch**: `config/<version>/overlay.ld`. |
| 14 | `src-changes.md` atribuía a `overlay_probe` macros que no verifica | medium | Verificado. **patch**: columna Verificación corregida por fila. |
| 15 | `shop`/`dget` comparten `0x80080800` sin documentar | low | **patch**: nota en `docs/research/overlays.md`. |
| 16 | Tipos de los globals del Juego redeclarados a mano en el probe | low | **patch**: comentario de equivalencia de ABI. |
| 17 | Macros de OT (`DOOA/MURD_ORDERING_TABLE`) sin chequeo de ventana | false | Toda dirección de `PS1_RAM_ADDR` cae en la arena, que `test_ps1mem` y el probe verifican dentro de la ventana con offset < `0xFFFFFF`. |
| 18 | `overlay_probe` no corre bajo ASan | low | Rechazado: decisión documentada en la 2.3. |
| 19 | `OVERLAY` cerrado sin reset de `base` | low | Rechazado: en `overlay.ld` cada `*_START` está dentro de su `OVERLAY`; un caso raro da error por "unparsed" o duplicado. |
| 20 | Estado inconsistente spec/sprint, STATUS.md | false | Se actualizan al cerrar. |
