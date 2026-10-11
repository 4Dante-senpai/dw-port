---
title: 'Historia 2.2: Checks L0a y los inventarios de literales y spins'
type: 'feature'
created: '2026-10-11'
status: 'done'
baseline_commit: '8a5c6cad8a1c98f3c26cd990968bf0aa3fa6594b'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Nada verifica que `src/`/`include/` sin `DW1_PORT` sigan idénticos a upstream (NFR-6
sin compilador matching), ni existen los inventarios de direcciones PS1 (AD-4) y de spins sin
PsyQ (AD-8) que las historias 2.4 y 2.15 deben vaciar.

**Approach:** `tools/port/ci/preproc_check.sh` preprocesa cada TU USA en el merge-base con
`upstream/main` y en el árbol actual, sin `DW1_PORT`, y compara; además corre los inventarios en
modo `--check`. Dos generadores (`ps1_literals.py`, `isr_spins.py`) escriben TSV versionados en
`tools/port/metrics/` conservando los estados ya asignados.

## Boundaries & Constraints

**Always:** sin cambios en `src/`/`include/`. Comparación del preprocesado sin marcas de línea y
sin líneas en blanco, para que un bloque `#ifdef DW1_PORT` agregado no cuente como diferencia.
Los TSV no contienen datos del disco (solo rutas, líneas, literales y fragmentos de C de upstream).

**Never:** traducir literales (Historia 2.4) ni agregar `port_yield` (2.15); exigir cero
`pendiente` en esta historia.

**Decisiones (autónomas):**
- Alcance de los inventarios: los TUs de `tu_list.py us` y todos los `include/**/*.h`.
- Literales: `0x80000000`–`0x801FFFFF` escritos en hex. Estado por defecto `pendiente`;
  automático `no aplica: residuo del REL` en `*_bss_us.c` (AD-4) y `no aplica: constante` para
  `0x80000000` exacto.
- Spins: loops `while`/`do` y `for` sin incremento cuyo cuerpo y condición no tienen llamadas.
  Cada fila se clasifica a mano: `port_yield` (espera un valor que escribe un callback o el
  hardware) o `no aplica: <motivo>`.
- Clave de fila: archivo + tipo + texto normalizado + ordinal; la línea es informativa pero entra
  en la comparación del `--check` (el inventario se regenera cuando cambian las líneas).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Árbol igual a upstream | repo actual | `preproc_check.sh` sale 0 | N/A |
| Cambio sin guarda | línea nueva fuera de `#ifdef DW1_PORT` en una copia | sale ≠ 0 y nombra el archivo | N/A |
| Cambio con guarda | bloque `#ifdef DW1_PORT` en una copia | sale 0 | N/A |
| Inventario viejo | TSV versionado distinto del generado | `--check` sale ≠ 0 | mensaje con el comando para regenerar |
| Estado conservado | fila existente con estado manual, regenerar | el estado se mantiene | N/A |

</frozen-after-approval>

## Code Map

- `tools/port/units/tu_list.py` -- lista de TUs USA (reusar `tu_list()`).
- `tools/gen_text.py` -- headers de texto necesarios para preprocesar.
- `mk/platform/psx.mk` -- include path upstream: `include/$(TOOLCHAIN)`, PsyQ, `include`, generated.
- `src/mov/mov.c:155` `MOV_waitForFlip` -- spin conocido (`env->flipPending`).
- `src/main/main.c:3722,3770` -- do/while sobre `FADE_OUT_CURRENT` (llaman funciones en el cuerpo).
- `include/dw/main.h:8-10`, `src/main/btl.c:23-27`, `src/std/std_setup.c:27-31`, `include/dw/vs.h`, `src/dooa/dooa.c`, `src/murd/murd.c`, `src/evl/evl.c`, `src/std/std_hud.c`, `src/vs/vs_hud.c`, `src/main/utils2.c:24` -- literales a traducir en 2.4.

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/metrics/inventory.py` -- archivos, quitar comentarios/strings preservando líneas, merge de estados, `--check`.
- [x] `tools/port/metrics/ps1_literals.py` + `ps1_literals.tsv` -- generador y TSV inicial.
- [x] `tools/port/metrics/isr_spins.py` + `isr_spins.tsv` -- generador y TSV con cada fila clasificada.
- [x] `tools/port/ci/preproc_check.sh` -- L0a.
- [x] `tools/port/ci/test_preproc_check.sh` -- casos de la matriz sobre copias temporales.
- [x] `tools/port/metrics/test_inventory.py` -- merge que conserva estados y `--check` que detecta diferencias.
- [x] `CMakeLists.txt` -- registrar los tests en CTest.
- [x] `docs/port/build.md` -- sección L0a.

**Acceptance Criteria:**
- Given el repo actual, when `preproc_check.sh`, then sale 0 con 120 TUs comparados.
- Given `isr_spins.tsv`, then contiene `MOV_waitForFlip` con estado `port_yield` y ninguna fila `pendiente`.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: exit 0
- `DOCKER_CONTEXT=default tools/port/docker_run.sh ctest --test-dir build/port/native-headless` -- expected: todos pasan

## Implementation Notes

- Inventario de spins: 67 loops sin llamadas; 1 `port_yield` (`MOV_waitForFlip`, `src/mov/mov.c:163`), 65 que modifican su propia condición, 1 `do {} while (0)` vacío. El primer borrador no veía llamadas por tabla (`BTL_jtbl_...[op]()`), que hacían aparecer los intérpretes EFE como spins; se agregó `INDEXED_CALL` con su test.
- Límite conocido (comentario `ponytail:` en `isr_spins.py`): no detecta loops que solo llaman código del Juego que nunca llega a PsyQ; esos aparecerían como cuelgue en L2.
- Literales: 58 filas; 27 `pendiente` (macros de buffers y OTs en `include/dw/main.h`, `include/dw/vs.h`, `btl.c`, `std_setup.c`, `std_hud.c`, `vs_hud.c`, `dooa.c`, `murd.c`, `evl.c`, `utils2.c`), 18 residuos de `*_bss_us.c`, 13 constantes `0x80000000`. Las direcciones de overlays (`btl_START[]` …) son símbolos del linker, no literales: las cubre la Historia 2.4.
- `preproc_check.sh` falla también si el preprocesador falla (sin comparar salida parcial). Corre en ~14 s.
- CTest: `tu_list`, `inventory` (14 tests), `preproc_check`, `test_preproc_check` (6 casos: igual, sin guarda, con guarda, rama mwcc, rama `#else`, TU borrado) — 4/4.
- Revisión: la clave de los spins pasó a incluir la función (`<loop>@<función>`); el TSV se regeneró y reclasificó con los mismos criterios.

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | L0a solo preprocesa la config gcc; el build matching usa mwcc (`__MWERKS__`, `include/mwcc`) | high | Verificado: `Makefile` `TOOLCHAIN ?= mwcc`; `include/dw/types.h` define tipos solo bajo `__MWERKS__`. **patch**: segunda pasada mwcc por TU + test que edita `types.h` en esa rama. |
| 2 | La lista de TUs puede fallar o venir vacía y el check pasa con 0 TUs | medium | Verificado (sustitución en `for` ignora el error). **patch**: lista a archivo con `set -e`, mínimo de 100 TUs. |
| 3 | Un TU borrado de la lista no se detecta | medium | Verificado. **patch**: compara la lista de TUs del árbol con la de la base; test que borra `sjis.c` de `us.mk`. |
| 4 | `--no-pending` no se usa ni se testea | medium | Verificado. **patch**: `isr_spins.py --check --no-pending` en L0a + test. |
| 5 | Estados por defecto de `ps1_literals` sin test | medium | Verificado (el merge los tapa). **patch**: test sobre `findings()` sin merge. |
| 6 | Regenerar pierde estados manuales en silencio | medium | Verificado. **patch**: `merge()` devuelve los estados perdidos y el script los imprime; test. |
| 7 | Ordinal: insertar un loop idéntico antes mueve estados | medium | Verificado para `while (1)` repetidos. **patch**: la clave de spins incluye la función envolvente (`while@MOV_waitForFlip`); test con K&R. |
| 8 | `read_text`/`write_text` sin encoding con TSV no ASCII | low | Verificado. **patch**: `utf-8` explícito. |
| 9 | `splitlines()` parte en `\x85`/`\x0c` con latin-1 | low | Verificado. **patch**: `split("\n")`. |
| 10 | Comilla sin cerrar (`#if 0` con apóstrofo) corre las líneas | low | Verificado. **patch** en `strip_c` + test. |
| 11 | Literales con sufijo `U`/`L` no se detectan | low | Verificado (ninguno hoy). **patch**: regex con sufijo + test. |
| 12 | Texto de condición tomado con `index()` puede elegir otra aparición | low | Verificado. **patch**: offset conocido `cs`. |
| 13 | Spins en headers no se buscan | low | Verificado. **patch**: se escanean también `include/**/*.h` (0 hallazgos nuevos). |
| 14 | Motivo "modifica su propia condición" falso para `while (1)`/`for (;;)` | low | Verificado. **patch**: motivo propio. |
| 15 | `config/`, `mk/` y `.c` no USA quedan fuera de L0a | medium | Verificado. **patch**: `git diff --quiet` sobre `config mk Makefile` y rechazo de `.c` cambiados que no son TU USA. `asm/` se genera del disco y no está versionado. |
| 16 | Sin ref `upstream/main` el error es críptico | low | **patch**: mensaje que pide `git fetch upstream`; documentado. |
| 17 | `test_preproc_check.sh` sin bit ejecutable | low | **patch**: `chmod +x`. |
| 18 | 40 literales `0x1F80xxxx` (scratchpad) fuera del inventario | false | `grep -riE "0x1f80[0-9a-f]{4}" src include` da 0: el Juego usa `getScratchAddr` (header PsyQ del port, AD-4). |
| 19 | Macros-función ocultan spins; loops con `goto`; `for` con incremento | low | Rechazado como parche; documentado como límite `ponytail:` en `isr_spins.py` (se manifestaría como cuelgue en L2). |
| 20 | La línea entra en `--check` y genera churn | low | Rechazado: decisión del spec (el inventario se regenera cuando cambian las líneas, y avisa estados perdidos). |
| 21 | Fila malformada en el TSV da traceback | low | Rechazado: archivo generado; el traceback ya señala el problema. |
| 22 | Sin timeout ni etiquetas en CTest; L0a no conectado a CI | low | Rechazado aquí; el CI es la Historia 8.2. |
| 23 | Include de PsyQ fijo en `mw_lib41` | low | Rechazado: es el `PSYQ_INCLUDE` de `us.mk`, la única versión del port. |
| 24 | STATUS.md / sprint-status / cita `mov.c:155` | false | Se actualizan al cerrar; `:155` es el inicio de la función y `:163` el loop. |
