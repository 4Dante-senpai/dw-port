---
title: 'Historia 2.1: Esqueleto del build del port y lista generada de TUs'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: 'c56f8b3f50382e6980969a6d7b29c7a6bc45b650'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** No existe build del port: no hay forma de compilar el C del Juego para i686 con los
flags de AD-17, ni una lista de TUs y su overlay que no esté elegida a mano.

**Approach:** Un script genera la lista de TUs USA y su unidad (main u overlay) desde
`mk/version/us.mk` y `config/us/*.yaml`; un `CMakeLists.txt` raíz con el preset `native-headless`
compila esos TUs a objetos i686, con headers del port primero que reemplazan las macros GTE
inline por llamadas declaradas. Un script mide qué TUs cambian con `char` con o sin signo y se
verifica la signedness real contra el asm original.

## Boundaries & Constraints

**Always:** sin cambios en `src/`, `include/`, `config/`, `mk/`, `Makefile`. Flags del Juego:
`-m32 -std=gnu99 -funsigned-char -fwrapv -fno-strict-aliasing -fno-common -DDW1_PORT -DLANGUAGE_C -DVERSION_US`
y `-ffile-prefix-map`. Todo corre en la imagen `dw-port` con `DOCKER_CONTEXT=default`.

**Never:** linkear (es la Historia 2.6); implementar el GTE (2.10); linkear `asm/`; crear
`port/recomp/`; agregar presets que esta historia no usa.

**Decisiones (autónomas, kickoff Fase 5):**
- `src/main/_psstart.c` es el crt de CodeWarrior (asm de MIPS): la lista lo marca `replaced`
  y el build no lo compila; lo reemplaza `port/psyq/crt.c` en la Historia 2.6. Compilan 119/120.
- `include/gcc/` no entra al include path del port: su único header (`inline_n.h`) lo reemplaza
  el del port.
- Si el asm original muestra `char` con signo, se cambia el flag a `-fsigned-char` en esta misma
  historia y se corrige AD-17 en la memlog de la arquitectura.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Lista normal | `tu_list.py us` | TSV `path<TAB>unit<TAB>status`, 120 filas, ordenadas | N/A |
| TU sin yaml | un `.c` de `us.mk` sin subsegmento en ningún yaml | — | sale con error nombrando el archivo |
| TU en 2 yaml | un `.c` referenciado desde dos segmentos | — | sale con error nombrando ambos |
| Ruta relativa | `../vs/vs_intro` en `main.yaml` | `src/vs/vs_intro.c` → `main` | N/A |

</frozen-after-approval>

## Code Map

- `mk/version/us.mk` -- fuente de los `.c` USA (`<UNIT>_C_SRC := ...`, con `\` de continuación).
- `config/us/<seg>.yaml` -- `options.src_path` + subsegmentos `[addr, kind, name]`; `name` relativo a `src_path` (`../vs/vs_intro`). `bss.yaml` no tiene `src_path` y se ignora.
- `include/gcc/inline_n.h` -- macros GTE con asm MIPS; las usadas por el Juego son 11: `gte_ldv0`, `gte_ldv3`, `gte_rtps`, `gte_rtpt`, `gte_stsxy`, `gte_stsxy3`, `gte_stszotz`, `gte_ldlzc`, `gte_stlzc`, `gte_SetRotMatrix`, `gte_SetTransMatrix`.
- `tools/port/metrics/wasm_compile_check.sh` -- patrón de flags e includes previo (97/120 en wasm).
- `tools/port/docker_run.sh` -- corre comandos en la imagen con el repo en `/dw`.
- `asm/us/**/*.s` y `asm/us/**/matchings` -- asm original para verificar `lb`/`lbu` en lecturas de `char`.

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/units/tu_list.py` -- generar la lista (stdout o `-o`) -- AD-1.
- [x] `tools/port/units/test_tu_list.py` -- test con unittest: `vs_intro`, `vs_combat`, `vs_select`, `shop/script_menu` → `main`; ningún `*_bss_{eu,jp*}`; 120 filas; `_psstart` `replaced`; error ante TU sin yaml (yaml sintético en tmp).
- [x] `CMakeLists.txt`, `CMakePresets.json` -- preset `native-headless` (Ninja, `build/port/native-headless`), target `dw1_game_objs` (OBJECT) con la lista generada en configure, y `dw1_psyq` con `port/psyq/ps1mem.c`.
- [x] `port/psyq/ps1mem.h`, `port/psyq/ps1mem.c` -- `_Static_assert(sizeof(void *) == 4)`.
- [x] `port/psyq/include/inline_n.h` -- las 11 macros como llamadas a `port_gte_*`.
- [x] `port/backend/gte.h` -- prototipos `port_gte_*`.
- [x] `tools/port/metrics/char_sign_diff.py` -- compilar cada TU con `-funsigned-char` y `-fsigned-char`, comparar `.text`/`.data` (objcopy sin debug) y listar los que difieren.
- [x] `docs/port/char-signedness.md` -- informe + evidencia del asm original.
- [x] `docs/port/build.md` -- cómo compilar y testear.

**Acceptance Criteria:**
- Given la imagen `dw-port`, when `cmake --preset native-headless && cmake --build --preset native-headless`, then 119 TUs compilan a objetos ELF i386 sin errores y `ps1mem.c` compila.
- Given `tu_list.py`, when corre su test, then pasa.
- Given `char_sign_diff.sh`, when corre, then el informe lista los TUs que difieren y la signedness verificada en el asm.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I tools/port/units/test_tu_list.py` -- expected: OK
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset native-headless && cmake --build --preset native-headless'` -- expected: exit 0, 119 objetos + ps1mem
- `DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I tools/port/metrics/char_sign_diff.py` -- expected: lista de TUs

## Implementation Notes

- `char` resultó **con signo**: `isAsciiEncoded` carga con `lb` + `sra 7` (`asm/us/main/sjis.s`, `0x800F18A4`). Se aplicó la decisión prevista: `-fsigned-char` en `CMakeLists.txt`, AD-17 enmendado en el spine y su memlog, y `wasm_compile_check.sh` alineado. 14/119 TUs cambian de código con el flag (`docs/port/char-signedness.md`).
- `.gitignore` de upstream ignora todo directorio `build/`: la herramienta vive en `tools/port/units/` en lugar de `tools/port/build/`.
- Los headers de texto (`text/**/*.yaml`) se generan en CMake con `tools/gen_text.py`, como `mk/text.mk`; sin datos del disco.
- `char_sign_diff` quedó en Python (lee `compile_commands.json`, activado en el preset) en vez de shell.
- Evidencia: 9 tests OK; build limpio 144/144 pasos (119 objetos del Juego + `ps1mem.c` + 24 headers); `ps1mem.h` sin `-m32` falla con el `_Static_assert`.

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | Nada fija `-fsigned-char`; `char_sign_diff` sale 0 si falta el flag | medium | Verificado: revertir el flag no rompía nada. **patch**: `port/psyq/include/port_game.h` incluido a la fuerza con `_Static_assert((char)-1 < 0)` (probado: falla con `-funsigned-char`); el script falla si `checked == 0`. |
| 2 | `CMAKE_CONFIGURE_DEPENDS` sin `tu_list.py`; globs sin `CONFIGURE_DEPENDS` | medium | Verificado. **patch**: `CONFIGURE_DEPENDS` en ambos globs y `tu_list.py` en la lista. |
| 3 | Tests fuera de CTest (L1 se define con CTest) | low | Verificado. **patch**: `enable_testing()` + `add_test(tu_list)`; `ctest` 1/1. |
| 4 | Evidencia del signo con una sola función | medium | Verificado. **patch**: segunda evidencia independiente, `convertValueToDigits` con `lb` en `0x800E5628`, en `docs/port/char-signedness.md`. |
| 5 | `relative_to(cwd)` falla fuera de la raíz; `mkdtemp` sin borrar | low | Verificado. **patch**: raíz desde `__file__`, `TemporaryDirectory`; probado desde `/tmp`. |
| 6 | Regex de `c_sources` ignora `+=`/`?=` | low | Verificado. **patch**: `[:?+]?=`; el test sintético usa `+=`. |
| 7 | `test_no_other_versions` con `"_jp" in f` es demasiado amplio | low | Verificado. **patch**: regex por sufijo `_(eu|jp\w*)\.c`. |
| 8 | AC de `epics.md` dice 120 TUs y `char_sign_diff.sh` | low | Verificado. **patch** en `epics.md` (119 `build`, `.py`). |
| 9 | Spec dice `.sh`, tareas sin marcar, notas vacías | false | El diff revisado era anterior a la actualización del spec; ya está `.py`, tareas `[x]` y notas escritas. El bloque congelado no se edita (prevé el cambio de flag). |
| 10 | `inline_n.h`: un `gte_*` nuevo podría compilar como declaración implícita bajo `-w` | false | El revisor de verificación probó en `dw-port` que clang 18 da error con `-w`. |
| 11 | Falta test de cobertura de `gte_*` usados vs definidos | false | Mismo motivo que 10: el build falla si falta uno. |
| 12 | Compara el objeto completo y no `.text`/`.data` | false | Es más estricto (cualquier sección); no oculta diferencias. Documentado en el script. |
| 13 | Informe pegado a mano en el `.md` | low | Rechazado: el comando que lo regenera está en el `.md` y en `build.md`. |
| 14 | Sin evidencia automática de "119 objetos ELF i386" | low | Rechazado: el build falla si un TU falla, y `port_game.h` asserta punteros de 32 bits en cada TU. |
| 15 | `SOURCE_DATE_EPOCH` ausente | low | **defer** a la Historia 4.10 (NFR-7); ningún TU usa `__DATE__`/`__TIME__`. |
| 16 | Arquitectura editada después de aprobada | false | El dueño aprobó AD-17 con el supuesto marcado para verificar; se enmendó la regla en su lugar con evento en la memlog, como pide el workflow de arquitectura. Se informa al dueño. |
| 17 | Métrica 97/120 de wasm no remedida con el flag nuevo | false | La signedness no cambia si un TU compila; el conteo mide errores de compilación. |
| 18 | yaml vacío o sin `name`; subsegmentos en forma dict; CRLF | low | Rechazado: los yaml de `config/us` no usan subsegmentos dict (solo segmentos) ni CRLF; un yaml roto ya da traceback en configure. |
| 19 | Tests sin caso sintético `replaced`, sin yaml sin `src_path`, sin `-o` | low | Rechazado: `replaced` y `bss.yaml` (sin `src_path`) están cubiertos por el test con la config real. |
| 20 | `compile_commands.json` ausente da traceback crudo | low | Rechazado: uso documentado tras configurar el preset. |
