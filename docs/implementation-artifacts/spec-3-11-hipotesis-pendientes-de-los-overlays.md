---
title: 'Historia 3.11: Hipótesis pendientes de los overlays'
type: 'chore'
created: '2026-10-26'
status: 'done'
route: 'dispatch'
baseline_commit: '661bcc433987665fd304747595c97555980115fb'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `docs/research/overlays.md` deja abierta la hipótesis (\*\*) sobre `SHOP_REL.BIN` en USA. El código de la tienda está linkeado en `main` y `shop_START` es la constante `0x80080800`, pero el archivo se sigue cargando. No se sabe si su contenido se ejecuta o si es un resto sin uso.

**Approach:** Usar Redux y la Escena `map_load`, que ya pasa contra la referencia. `newGameScene` llama `checkShopMap(0xda)`, y `loadMap` llama `checkShopMap(CURRENT_SCREEN)` justo antes de `checkArenaMap`. Con breakpoints se confirma que `SHOP_REL.BIN` se lee, desde qué función (pila de llamadas) y si se ejecuta o se lee algo en `0x80080800–0x80088610`. Se completa con un análisis estático de `main` y se cierra la hipótesis en `overlays.md` con esa evidencia.

## Boundaries & Constraints

**Always:**
- La evidencia de Redux sale de una corrida automática y reproducible con el disco USA (`capture.py` o una sonda Lua junto a `capture.lua`). Se registran el ciclo y el VBlank desde el frame 0, el `ra`, la pila hasta la función del juego que llama y el destino/tamaño leídos.
- `map_load` sigue pasando contra la referencia (`run.py map_load --compare`), con 0 VBlanks de diferencia en todos sus puntos de control, incluidos los nuevos `checkShopMap`.
- No se cambia el comportamiento del juego ni de `src/`. Una sonda del port, si hace falta, va detrás de `DW1_DEVICE_PROBE`, como las demás.
- Ni el repositorio ni los logs versionados guardan bytes del disco: solo direcciones, tamaños, hashes y conteos.
- Si la hipótesis no se confirma (`SHOP_REL.BIN` no se lee en `map_load`), se detiene la implementación y se reporta. Escribir una Escena de tienda requiere la decisión del dueño.

**Never:**
- Guionar una partida larga hasta una tienda, o crear `shop_*.toml`, sin aprobación del dueño.
- Proponer cambios a upstream, modificar `REL_BIN_OFFSETS` ni dejar de cargar `SHOP_REL.BIN` en el port.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Carga confirmada | sonda en `map_load` | 1 línea por carga de `SHOP_REL.BIN`: VBlank, `ra`, llamador (`checkShopMap` desde `newGameScene` / `loadMap`) y destino `0x80080800` | N/A |
| Región sin uso | breakpoints Exec/Read sobre `0x80080840–0x80088610` desde la carga hasta el fin de la Escena | 0 accesos, o se lista cada PC que accede | N/A |
| Trigger 0x29 activo | `checkShopMap` vuelve sin cargar | la sonda lo registra como "skip"; no se cuenta como carga | N/A |
| Sin disco | `DW1_DISC_DIR` ausente | la prueba sale con código 77 (skip clase B) | N/A |

</frozen-after-approval>

## Code Map

- `src/main/script_common.c:3063-3085` -- `checkShopMap` (trigger 0x29, `SHOP_MAPS` en `:346`) y `loadShopLibrary` (comando de script 0x23, `script_interp.c:1861`). Las dos llaman `loadDynamicLibrary(SHOP_REL, …)`.
- `src/main/utils2.c:24-27,98-115,385-397` -- `shop_START` (constante), `REL_BIN_OFFSETS[lib-1]` y `loadDynamicLibrary`, que usa `readFile` síncrono o `addFileReadRequestPath`. No se modifica.
- `src/main/main.c:3766`, `src/main/map.c:4103`, `src/main/partner.c:801` -- llamadores de `checkShopMap`. En `map_load` ocurren los dos primeros.
- `config/us/symbols.txt` -- `readFile 0x800A3410`, `loadDynamicLibrary 0x800E61E4`, `checkShopMap 0x800FF910`. `main` va de `0x80090000` en adelante (`config/us/main.yaml:53`), así que la región de SHOP no se pisa con `main`. `config/us/main.yaml:132,162,210,242`: `shop/script_menu` en `main`.
- `disks/us/SHOP_REL.BIN` (ignorado por git) -- 32.272 bytes. Cabecera `MWo1`, id `0x0b`, carga en `0x80080800`, código desde `+0x40`. Los bloques de 64 bytes no aparecen literales en `SLUS_010.32` (verificado): si es la tienda reubicada, difiere en los `jal`/`lui`.
- `tools/port/reference/capture.lua` -- patrón de breakpoints (`PCSX.addBreakpoint(addr,'Exec'|'Read'|'Write',width,…)`, retorno por `ra`, `guarded`). Se reutiliza para la sonda, sin duplicar el arranque ni la inyección de inputs.
- `tools/port/reference/capture.py:120-130` -- `config` pasado a Lua (`hardware_trace`/`hardware_adapter` es el precedente de una traza adicional por corrida).
- `tools/port/scenes/map_load.toml` -- checkpoints en orden, con sufijo `#n` para la n-ésima ocurrencia (`port/psyq/scene.c:391-395`; port por `-finstrument-functions`, Redux por breakpoint de símbolo). Entre `newGameScene` y `newGameStateMachine` va `checkShopMap`, y entre `loadMap` y `checkArenaMap` va `checkShopMap#2` (verificar el orden y el conteo en ambos lados).
- `docs/research/overlays.md:39,49-58` -- fila 11 y nota (\*\*), que se reemplazan por el resultado.

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/reference/` -- agregar a la captura de Redux una sonda opcional para `map_load`. En cada entrada a `loadDynamicLibrary` con `lib == 11` registra el VBlank, el `ra`, la pila de llamadas, el destino y el tamaño. Desde esa carga arma breakpoints Exec y Read sobre la región cargada y cuenta los accesos por PC hasta el final de la Escena. Sale en un JSONL fuera del repo -- evidencia dinámica de la carga y del uso.
- [x] `tools/port/scenes/map_load.toml` -- agregar los checkpoints `checkShopMap` y `checkShopMap#2` en su orden real y volver a capturar la referencia si hace falta. El hash solo cambia con una explicación -- tiempos de la carga en el port y en Redux, con diff 0.
- [x] `tools/port/metrics/` -- script estático sobre el asm/ELF de `main` USA que lista `jal`/`j` con destino en `0x80080800–0x80088610`, pares `lui`/`addiu`/`lw` que apuntan ahí y palabras de `.data`/`.rodata` en ese rango. Compara además los opcodes de `SHOP_REL.BIN`, sin los campos reubicados, contra `shop/script_menu` de `main` -- distingue "resto" de "usado".
- [x] `docs/research/overlays.md` -- reemplazar la hipótesis (\*\*) por el resultado. Incluye funciones llamadoras, VBlanks, conteo de accesos a la región, resultado estático, comandos para reproducir y el límite: `map_load` no entra a una tienda, y `loadShopLibrary` (script 0x23) queda cubierta solo por el análisis estático.
- [x] `docs/implementation-artifacts/evidence/story-3-11.log` -- salida de la sonda resumida (sin bytes del disco), del script estático y de `run.py map_load --compare`.

**Acceptance Criteria:**
- Given el disco USA y Redux fijado, when corre la sonda en `map_load`, then se registra al menos una carga de `SHOP_REL.BIN` con su llamador y su pila, y el conteo de ejecuciones/lecturas en la región.
- Given `map_load` con los checkpoints nuevos, when `run.py map_load --build native-headless --compare`, then sale 0 con diff 0 en cada checkpoint y el hash de frames coincide.
- Given `overlays.md`, when se lee la fila 11, then ya no figura "Hipótesis a validar" y cada afirmación remite a la evidencia o al script.

## Implementation Notes

- Codex implementó la sonda `tools/port/reference/shop.lua` (activada con `capture.py map_load --shop-probe`), los checkpoints `checkShopMap` y `checkShopMap#2`, y `tools/port/metrics/shop_overlay.py`. No se modificó `src/`.
- Tras la review, `scan()` respeta el delay slot y aparece un candidato nuevo: `DOOA_REINCARNATION_SEQ + 0x24` en `initializeReincarnatedPartner`, que es de DOOA y no de la tienda.
- Verificación final en remoto: 12/12 tests de CTest con disco, entre ellos `reference_map_load` y `scene_map_load_compare`. `make compare` no se corrió porque no hay cambios en `src/`.

## Spec Change Log

## Review Triage Log

| # | Origen | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|---|
| 1 | blind-hunter, edge-case | `scan()` borra los registros en el salto antes de procesar su delay slot (`shop_overlay.py:54`) | medium | La línea 54 limpia los registros al ver la rama. El `addiu` del delay slot llega sin el `lui`, y `lui a0; jal; addiu a0` es un patrón habitual de CodeWarrior. El "cero candidatos" de `overlays.md` puede ser incompleto. | patch |
| 2 | edge-case | `mfc2`/`cfc2` (op 0x12) no invalidan `rt` (`shop_overlay.py:50`) | low | 0x12 está en la lista de exclusión, así que puede quedar un valor obsoleto. Solo produce falsos positivos y el resultado no tuvo ninguno. La corrección es directa. | patch |
| 3 | blind-hunter, edge-case | `normalize()` no corta ante una instrucción SPECIAL que escribe el registro del `lui` (`shop_overlay.py:70-76`) | low | `addu v0,…` no detiene el enmascarado y oculta constantes. Solo puede inflar coincidencias, y la conclusión ("no confirma copia") no cambia. La corrección es directa. | patch |
| 4 | blind-hunter, edge-case | SHOP no se valida por identidad o estructura; con un archivo malformado `max()` falla | low | Un archivo vacío o sin `jr ra` falla con excepción ruidosa. El informe emite `shop_sha256` y el tamaño. Uso cotidiano improbable y el arreglo agrega guardas. | rechazado |
| 5 | blind-hunter, edge-case | `loadShopLibrary` se verifica por pertenencia de opcodes, no por argumentos efectivos | low | La función es mínima (`script_common.c:3082-3085`) y el build matching garantiza su asm. El arreglo requiere análisis de flujo. | rechazado |
| 6 | blind-hunter, edge-case, verification-gap | `capture.py` acepta `read_file` sin `cd_read`, y la validación de las dos trazas no tiene test | medium | `capture.py:327-335` solo exige `read_file` y los dos resúmenes; una traza sin transferencia pasa. Ningún test ejerce esa ruta. | patch |
| 7 | blind-hunter, verification-gap | El doble de `addBreakpoint` ignora `width`; reducir el rango a 4 bytes no rompe el test | low | `test_capture.py:1078-1083` descarta `width`. La corrida real de Redux sí contó lecturas en PCs del rango. Arreglo directo: registrar y comprobar dirección y ancho. | patch |
| 8 | verification-gap | `audit()` completo no tiene test | low | Script de investigación de una sola corrida, con salida registrada en la evidencia. Probarlo exige refactorizar la lectura de `config/` y el asm. | rechazado |
| 9 | edge-case | El rango vigilado empieza en `0x80080840` y deja fuera la cabecera | false | La fila "Región sin uso" de la matriz congelada fija `0x80080840–0x80088610`. La cabecera `MWo1` no es código. | rechazado |

## Verification

**Commands:**
- `tools/port/remote_run.sh python3 tools/port/scenes/run.py map_load --build native-headless --compare` -- expected: salida 0, todos los checkpoints con diff 0.
- `python3 tools/port/reference/capture.py map_load <flag de la sonda>` -- expected: JSONL con cargas de `lib 11` y conteo de accesos.
- `ctest --preset native-headless -R 'map_load|capture|scene'` -- expected: sin regresiones.
