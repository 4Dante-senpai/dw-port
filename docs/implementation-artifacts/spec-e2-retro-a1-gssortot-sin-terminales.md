---
title: 'GsSortOt sobre la OT sin paquetes terminales (acción A1 de la retro de la Épica 2)'
type: 'bugfix'
created: '2026-10-17'
status: 'done'
route: 'oneshot'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GsSortOt` (`port/psyq/libgs.c:136-146`) traduce el asm, que reenlaza el nodo dos
posiciones antes del final de la OT hija porque el `ClearOTagR` original termina en dos paquetes
(`nop2` → `sys_nop`). El `ClearOTagR` del port (`port/psyq/libgpu.c:117-122`) los omite, así que
una primitiva en el bucket 0 de la OT hija queda inalcanzable. `test_libgs.c:302-304` fija ese
resultado incorrecto. El Juego anida OTs en `std_setup`, `std_effect`, `std_hud`, `murd` y `dooa`
(hallazgo F1 de `epic-2-retro-2026-10-17.md`).

**Approach:** que `GsSortOt` reenlace el último nodo de la OT hija (el que termina en
`PS1_LINK_END`), que es el equivalente del nodo anterior a `nop2` del original. Así toda primitiva
de la OT hija sigue siendo alcanzable desde la OT padre. `ClearOTagR` no cambia. El test pasa a
comprobar la alcanzabilidad de todas las primitivas recorriendo la lista desde `ot->tag` hasta el
fin.

</frozen-after-approval>

## Implementation Notes

- Decisión: se corrige `GsSortOt` y no se restauran `nop2`/`sys_nop` en `ClearOTagR`. Restaurarlos
  haría que `DrawOTag` ejecute en cada frame un GP0 `0x80` (copia de VRAM con
  `0x80000000, 0, 0, 0x00000002`) y cuatro NOP, lo que cambia los costos de `DrawSync`, puede tocar
  el bit de máscara y cambia los 30 SHA-256 de VRAM de la 2.12. Es una pregunta de paridad contra
  la referencia: queda registrada en `deferred-work.md` para la 3.2/3.10.
- Asm verificado: `asm/us/main/psyq/libgs.s` `GsSortOt` (`t0` queda dos nodos antes del que
  termina en `0xFFFFFF`); datos `nop2`/`sys_nop` en `asm/us/main/data/psyq/libgpu.data.s:66-84`.
- Archivos: `port/psyq/libgs.c` (`GsSortOt` reenlaza el último nodo), `port/psyq/tests/test_libgs.c`
  (`count_nodes`/`reachable` acotados; el caso del bucket 0 comprueba que la primitiva enlaza a la
  OT padre y que las 256 + 16 + 1 entradas son alcanzables desde `ot.tag`), `deferred-work.md`
  (terminales `nop2`/`sys_nop`).
- Verificación: `ctest` de `native-headless` con disco (41/41, sin `reference_title`, que no
  depende del port) y `libgs` en `native-asan`. Con el `GsSortOt` anterior, el caso nuevo falla
  (`libgs:316-318`). `scene_title` sigue pasando (dos corridas con la misma captura).

## Review Triage Log

- `reachable()` sin límite ante un ciclo (Blind Hunter) — low, **patch**: límite de 4096 pasos,
  igual que `count_nodes`.
- Comprobar píxeles con `GsDrawOt` en lugar de enlaces (Blind Hunter) — low, rechazado: la
  propiedad que rompía el bug es la alcanzabilidad, que el test comprueba recorriendo la lista
  como `DrawOTag`; el recorrido y el dibujo de `DrawOTag` ya tienen sus SHA-256 de VRAM (2.12).
- La entrada de `deferred-work.md` sobre los terminales faltaba (Blind Hunter) — verdadero,
  **patch**: agregada.
