---
title: 'Historia 2.9: libc, libapi y libetc deterministas'
type: 'feature'
created: '2026-10-12'
status: 'in-review'
baseline_commit: '1933638b7d2afbd1b24eb7633cae2996bd9d8a2b'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `InitHeap3`/`malloc3`/`free3` y `PadInit`/`PadRead` son stubs que devuelven 0 (el
primer `malloc3` del Juego recibe NULL), y `rand` es el de glibc, distinto del BIOS: el Juego no
toma las mismas decisiones que en la consola.

**Approach:** traducir del asm de DW1 el heap (`libapi.s`) y `PadInit`/`PadRead` (`libetc.s`),
implementar `rand`/`srand` con el algoritmo del BIOS en un `port/psyq/libc.c` nuevo, y contar las
funciones de `libc` que define el port como implementadas en el contador de la 2.6.

## Boundaries & Constraints

**Always:** comentario `asm:` por función; toda entrada pública llama a `clock_psyq_entry()`
(salvo `rand`/`srand`, llamadas al BIOS sin reloj modelado); sin cambios en `src/`;
`native-headless` y `native-asan` verdes; L0a OK.

**Never:** input real del host (Historia de input, epics.md:675); `sprintf`/`printf` propios;
reimplementar cadenas, `memcpy`/`memset` o `setjmp`/`longjmp`.

**Decisiones (autónomas):**
- `libc.s` del Juego son saltos a la tabla A del BIOS (salvo `sprintf`), así que su semántica es
  la del BIOS. `rand` = A(2Fh): `x = x * 0x41C64E6D + 0x3039; return (x >> 16) & 0x7FFF`;
  `srand` = A(30h): `x = seed` (el Juego no lo llama; lo usan los tests). Semilla de arranque
  `0x24040001` [hipótesis: valor del BIOS según el HLE de PCSX-ReARMed; se verifica en la
  historia de epics.md:471 contra el Emulador].
- Cadenas, `mem*`, `bzero`, `printf`/`sprintf`, `exit`: siguen en la libc del host. Para
  argumentos válidos dan el mismo resultado; las ramas del BIOS con punteros NULL y el relleno de
  `strncpy` no se modelan [hipótesis, se valida contra el Emulador en la Épica 3].
  `setjmp`/`longjmp` ya están cubiertos por `port_game.h` y `test_setjmp` (2.6).
- `ResetCallback`, `VSyncCallback(s)`: ya traducidos en 2.5; no se tocan.
- `InitHeap3(addr, size)` recibe la dirección PS1 entera (`initializeHeap` pasa `0x801BFB70`):
  `PORT_ASSERT(ps1_addr_is_ram)` y `ps1_ram()`. Los bloques guardan punteros nativos (4 B en
  i686/wasm32), así que el layout de cabecera {next, unidades de 8 B} es el de la consola.
  `malloc3`/`free3`: traducción literal (redondeo a 8, asignación desde el final del bloque,
  coalescencia). Las escrituras de depuración `_r_bp`/`_r_p`/`_r_freep` de `free3` se omiten (nadie
  las lee).
- `PadInit(mode)`: `PadIdentifier = mode`, `pad_buf = 0xFFFFFFFF`, `ResetCallback()`; `PAD_init`
  y `ChangeClearPAD` no se modelan hasta la historia de input. `PadRead(id)`: `return ~pad_buf`
  (sin input = 0). Valor inicial de `pad_buf` según su sección en `config/us` (`.bss` → 0).
- `gen_stubs.py`: un símbolo `libc` definido por el port cuenta como `implemented`, no
  `host_libc`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| rand | `srand(1)`; `rand()` | 16838, luego 5758, 10113 | N/A |
| rand largo | `srand(s)` para 3 semillas; 10.000 valores | primeros valores y suma iguales a los dorados del test | N/A |
| Heap | `InitHeap3(0x80100000, 0x1000)`; `malloc3(0x10)` | devuelve la dirección PS1 `0x80100FE8` | N/A |
| Heap tamaño | `malloc3(0)`; pedido mayor que el heap | NULL | N/A |
| Heap free | secuencia malloc/free/malloc de prueba | mismas direcciones PS1 que el algoritmo del asm, coalescencia incluida | N/A |
| Heap dir. inválida | `InitHeap3` con dirección fuera de la RAM PS1 | abort en debug | N/A |
| Pad | `PadInit(0)`; `PadRead(0)` | 0, y `ResetCallback` quedó hecho | N/A |

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libapi.s:745` `InitHeap3`, `:777` `malloc3`, `:834` `free3` -- fuente del heap (`_freep`, `_r_*`); cuidado con los delay slots del redondeo.
- `asm/us/main/psyq/libetc.s:12` `PadInit`, `:36` `PadRead`; `config/us/symbols.txt:5052` `pad_buf`/`PadIdentifier`.
- `asm/us/main/psyq/libc.s` -- `rand` A(2Fh); el resto son saltos al BIOS.
- `src/main/main.c:3533` `initializeHeap`, `:3389` `PadInit(0)`, `:4040` `PadRead(0)`; `src/main/model.c` usa `malloc3`/`free3`.
- `port/psyq/libapi.c`, `port/psyq/libetc.c` -- estilo y `psyq_lib*_reset`; agregar heap y pad aquí.
- `port/psyq/ps1mem.h` -- `ps1_addr_is_ram`, `ps1_ram`, `Ps1_Ram`.
- `port/psyq/include/__rts_info_t__.h` -- ya anuncia que `InitHeap3` mapea con `PS1_RAM`.
- `port/psyq/psyq.h` -- prototipos y resets.
- `tools/port/psyq/gen_stubs.py` -- regla `host_libc` vs `implemented`.
- `CMakeLists.txt:128` -- fuentes de `dw1_psyq`; tests como `test_clock` (`:164`).

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/libc.c` -- `rand`, `srand`, `psyq_libc_reset` (semilla de arranque).
- [x] `port/psyq/libapi.c` -- `InitHeap3`, `malloc3`, `free3`; reset del heap.
- [x] `port/psyq/libetc.c` -- `PadInit`, `PadRead`; `pad_buf`/`PadIdentifier` en el reset.
- [x] `port/psyq/psyq.h` -- prototipos.
- [x] `tools/port/psyq/gen_stubs.py` -- `libc` del port cuenta como `implemented`.
- [x] `port/psyq/tests/test_libc_heap_pad.c` -- matriz completa (valores dorados de `rand`
  generados aparte con un script de una línea y escritos como constantes; direcciones PS1 del heap
  calculadas a mano desde el asm y escritas como constantes; abort en hijo con `RLIMIT_CORE=0`).
- [x] `CMakeLists.txt` -- `libc.c` en `dw1_psyq`; test `libc_heap_pad`.

**Acceptance Criteria:**
- Given los dos presets sin `DW1_DISC_DIR`, when `ctest`, then todo pasa, `frame0` incluido.
- Given el build, when corre `gen_stubs.py`, then imprime `implemented=18 host_libc=12 stubs=169`
  (antes 12/13/174) y ningún stub de `InitHeap3`/`malloc3`/`free3`/`PadInit`/`PadRead`.
- Given `DW1_DISC_DIR`, when `ctest -R 'pack_disc|dw1_overlay'`, then siguen pasando.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p; done'` -- expected: verde
- ídem con `DW1_DISC_DIR` montado -- expected: `pack_disc` y `dw1_overlay` pasan
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK

## Implementation Notes

- Heap traducido con cabeceras nativas de 8 B, lista circular, centinela de tamaño cero,
  asignación desde el final y coalescencia de ambos vecinos. Se comprueban los invariantes de
  dirección, alineación y extensión de la arena en debug.
- `rand` usa aritmética unsigned de 32 bits; `srand` y `psyq_libc_reset` permiten reproducir
  secuencias sin avanzar el reloj. La semilla de arranque sigue siendo la hipótesis aprobada.
- Pad sin entrada del host: estado inicial `.bss`, inicialización sin botones y activación de
  callbacks mediante `ResetCallback`, que conserva su implementación anterior.
- El test nuevo cubre tres secuencias de 10.000 valores, direcciones constantes del heap,
  redondeo, agotamiento, coalescencia, aliases de RAM, aborts sin core dump y reloj/callbacks.
  `psyq_prototypes` incluye ahora los headers upstream de heap y números aleatorios.

- Verificación en este contenedor: ambos builds y suites completas sin `DW1_DISC_DIR` verdes
  (22 tests ejecutados + 2 skips en headless; 20 + 2 skips en ASan). `pack_disc`/`dw1_overlay`
  pasan con disco en ambos presets. L0a: 120 TUs, gcc/mwcc, sin diferencias. Contadores exactos
  `implemented=18 host_libc=12 stubs=169`; sin stubs de heap/pad ni cambios en `src/`/`include/`.
  Evidencia: `docs/implementation-artifacts/evidence/story-2-9.log`. Sin commits.

## Spec Change Log

## Review Triage Log
