---
title: 'Historia 2.3: Memoria PS1, enlaces de OT y reinicio de overlays'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: '7db469d5e0f5e08f542cf08c323422127d5452cf'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El port no tiene RAM PS1, scratchpad, forma de construir enlaces de OT de 24 bits,
ni mecanismo para que un overlay recargado vuelva a sus datos iniciales.

**Approach:** `port/psyq/ps1mem` define la arena, el scratchpad, `PS1_RAM` y `PS1_LINK` sobre una
ventana de 16 MB; `port/psyq/overlay` guarda al arrancar una copia de la sección `dw1ovl_<ovl>` de
cada overlay y la restaura en `port_overlay_loaded`. CMake compila cada TU de overlay a través de
una TU envoltorio con `#pragma clang attribute`.

## Boundaries & Constraints

**Always:** sin cambios en `src/`/`include/` (L0a verde). Código del port con `-Wall -Wextra -Werror`.
El binario nativo y los tests se linkean con `-no-pie -Wl,-Ttext-segment=0x01000000`.

**Never:** linkear el Juego (2.6); hook en `loadDynamicLibrary` (2.8); headers `libgpu.h` del port
(2.12).

**Decisiones (autónomas):**
- Ventana de OT: nativo `[0x01000000, 0x02000000)`, web `[0, 16 MB)`. Un enlace es el offset de 24
  bits dentro de la ventana; `0xFFFFFF` sigue siendo el terminador de PsyQ.
- `PS1_RAM` acepta KUSEG `0x00000000-0x001FFFFF`, KSEG0 `0x80000000-0x801FFFFF` y KSEG1
  `0xA0000000-0xA01FFFFF`; fuera de eso `PORT_ASSERT`.
- `PORT_ASSERT` aborta en debug (`NDEBUG` sin definir) e imprime en release.
- USA tiene 15 overlays con sección: `SHOP_REL` no tiene segmento propio (su código está en
  `main`), así que `port_overlay_loaded(SHOP_REL)` no restaura nada. Los símbolos
  `__start_/__stop_` se declaran `weak`.
- Nuevo preset `native-asan` (hereda `native-headless` + ASan/UBSan).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Dirección KSEG0 | `PS1_RAM(0x80010000)` | `Ps1_Ram + 0x10000` | N/A |
| Fuera de RAM | `PS1_RAM(0x80200000)` | — | `PORT_ASSERT` aborta |
| Scratchpad | índice 0x3FF / 0x400 | válido / aborta | `PORT_ASSERT` |
| Enlace | `PS1_LINK_PTR(PS1_LINK(p))` con p en la ventana | `p` | p fuera de la ventana aborta |
| Reinicio | globals, statics de archivo y locales de un overlay modificados | valores iniciales tras `port_overlay_loaded`, en -O0 y -O2 | globals de main intactos |

</frozen-after-approval>

## Code Map

- `tools/port/env_check/overlay_sections/` -- spike validado (ovl_unit.c, ovl_wrap.c, main.c): semilla del test L1.
- `include/dw/utils.h:7` -- `enum Overlay` (`BTL_REL`=1 … `EAB_REL`=16).
- `src/main/utils2.c:382` -- `loadDynamicLibrary` usa `REL_BIN_OFFSETS[lib - 1]` (el hook es la 2.8).
- `CMakeLists.txt` -- lista de TUs con su unidad (`tu_list_us.tsv` columna 2).
- `port/psyq/ps1mem.h` -- ya tiene el `_Static_assert` de punteros de 32 bits.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/port.h` -- `PORT_ASSERT`.
- [x] `port/psyq/ps1mem.{h,c}` -- arena, scratchpad, `PS1_RAM`, ventana, `PS1_LINK`/`PS1_LINK_PTR`.
- [x] `port/psyq/overlay.{h,c}` -- tabla por `enum Overlay`, `port_overlay_snapshot_all`, `port_overlay_loaded`.
- [x] `CMakeLists.txt` -- TUs envoltorio para los TUs de overlay; flags de link nativo; `overlay_probe`; tests.
- [x] `CMakePresets.json` -- `native-asan`.
- [x] `port/psyq/tests/test_ps1mem.c` -- matriz de memoria + ventana (globals, arena, stack de un pthread con stack estático).
- [x] `port/psyq/tests/test_overlay.c` + unidad sintética -- reinicio en -O0 y -O2 con `overlay.c` real.
- [x] `port/psyq/tests/overlay_probe.c` -- ejecutable con todos los objetos del Juego (sin correrlo) que imprime el tamaño de las 15 secciones vía `__start_`/`__stop_`.

**Acceptance Criteria:**
- Given `native-headless` y `native-asan`, when `ctest`, then todos los tests pasan.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --test-dir build/port/$p --output-on-failure; done'` -- expected: todo verde

## Implementation Notes

- **Corrección del spike:** los overlays sí tienen globals `const` (`static void *const endi_functions[]` en `src/endi/endi.c`) y clang da "section type conflict". El envoltorio agrega `#define const` antes del `#include`: todo global del overlay queda escribible en `dw1ovl_<ovl>`; restaurar una constante sin cambios es inocuo. Test sintético con una tabla `const`. AD-6 y su memlog actualizados.
- Ventana de OT: nativo `[0x01000000, 0x02000000)`, web `[0, 16 MB)`; el enlace es el offset de 24 bits. Precisado en AD-5.
- `overlay_probe` linkea los 119 objetos del Juego con `--unresolved-symbols=ignore-all` y `--wrap=main` (el `main` del Juego no corre). Tamaños: btl 13.096, std 26.368, fish 14.304, evl 20.836, kar 36.496, vs 14.853, mov 469.736, doo2 40.092, dooa 12.332, trn 984, dget 12, trn2 760, murd 4.668, endi 1.960, eab 2.464, shop 0. No corre bajo sanitizers (el loader exige los símbolos sin resolver).
- `dw1_psyq` pasó de OBJECT a STATIC. Presets de test en `CMakePresets.json`.
- Evidencia: `native-headless` 9/9, `native-asan` 7/7 (sin los checks estructurales, que no aplican bajo sanitizers), L0a verde; `docs/implementation-artifacts/evidence/story-2-3.log`.
- Con ASan los globals siguen en `dw1ovl_<ovl>`; ASan agrega su propia `.data` (literales instrumentados y descriptores).

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Core dump `core` (987 KB) sin trackear en la raíz, sin regla en `.gitignore` | high | Verificado (crash del primer `overlay_probe`). **patch**: borrado antes de commitear; `core`/`core.*` en `.gitignore` y en el hook pre-commit (regla 1). |
| 2 | Un objeto en `base + 0xFFFFFF` daría un enlace igual al terminador; `PS1_LINK_PTR(0xFFFFFF)` no aborta | medium | Verificado. **patch**: `ps1_link` exige `< PS1_LINK_END`, `ps1_link_ptr` también; tests. |
| 3 | No se verifica que toda la imagen quepa en la ventana | medium | Verificado. **patch**: `_end` en la ventana en `test_ps1mem` y en `overlay_probe` (con los 119 objetos: `_end = 0x125622c`). |
| 4 | Caminos de rechazo sin test (segmento inválido, debajo de la ventana, `PS1_LINK_PTR` fuera de rango) | medium | Verificado. **patch**: `0xC0010000`, `0x20010000`, `base - 1`, `NULL`, `PS1_LINK_PTR(0xFFFFFF/0x1000000)`. |
| 5 | `overlay_sections` no detecta un TU de overlay que no pasó por el envoltorio | medium | Verificado. **patch**: `check_ovl_objects.py` exige objeto envuelto y cero bytes en `.data/.bss/.sdata/.sbss` y cero COMMON por TU de overlay. |
| 6 | Falta probar independencia entre overlays y dos TUs del mismo overlay | medium | Verificado. **patch**: unidades sintéticas `ovl_unit_btl2.c` (btl) y `ovl_unit_std.c` (std); restaurar BTL no toca STD y viceversa. |
| 7 | La sonda ignora el código de salida (regex) y el 15 está fijo | low | Verificado. **patch**: la sonda devuelve error; el esperado sale de CMake (`DW1_OVERLAY_UNITS`). |
| 8 | `PS1_LINK(NULL)` pasa en web (base 0) | low | **patch**: `p != NULL` en el assert. |
| 9 | En release, `PORT_ASSERT` sigue con índices inválidos en `overlay.c` | low | **patch**: retorno temprano en `port_overlay_loaded`/`port_overlay_size`. |
| 10 | Unidad del TU list que `overlay.c` no conoce quedaría sin reinicio | low | **patch**: `FATAL_ERROR` en CMake si la unidad no está en la lista de `overlay.c`. |
| 11 | `fork()` sin chequeo de error | low | **patch**. |
| 12 | `ASAN_OPTIONS` en el preset de configure no tiene efecto | low | **patch**: solo en el test preset. |
| 13 | `port_overlay_loaded` pone el bss en cero; en la consola cargar el REL podría no limpiarlo (hipótesis) | false | `config/us/overlay.ld`: los `.bss.*` de cada TU de overlay están dentro de la sección cargada, antes del trailer `LONG(0)`; el `REL.BIN` lleva esos ceros y la recarga los repone. |
| 14 | Nadie llama todavía a `port_overlay_snapshot_all` | low | Fuera del alcance (Never: linkeo es 2.6). **patch** en `epics.md`: la 2.6 exige que `crt.c` lo llame antes de `main`. |
| 15 | `#define const` cambia prototipos de headers del TU de overlay | low | Rechazado: C no codifica `const` en el símbolo y los 119 TUs compilan; documentado en `build.md` y AD-6. |
| 16 | Espejos de RAM `0x00200000-0x007FFFFF` abortan | low | Rechazado: el assert en debug revela si el Juego los usa; hoy no hay evidencia de uso. |
| 17 | Chequeo de sanitizers por texto de flags | low | Rechazado; comentario `ponytail:` con la mejora. |
| 18 | Tests de aborto con `NDEBUG` | low | Rechazado: los tests se compilan siempre en Debug. |
| 19 | Evidencia sin preset, nombres de tests ni hash | low | **patch**: el log incluye preset, commit base y cada test. |
| 20 | STATUS.md / sprint-status / comentario de `getScratchAddr` | false/low | STATUS y sprint se actualizan al cerrar; comentario corregido. |
