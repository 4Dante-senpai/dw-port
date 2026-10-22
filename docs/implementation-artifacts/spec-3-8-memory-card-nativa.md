---
title: 'Historia 3.8: Memory card nativa'
type: 'feature'
created: '2026-10-22'
status: 'done'
baseline_commit: '3cd2cf5d69d9eb4cbc0b73b9dd21bc856cbfecae'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/port/stubs.md'
  - '{project-root}/docs/port/reference.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Hoy `libmcrd` es un stub de la Historia 2.14 que siempre responde "tarjeta ausente", así que el Juego no puede guardar ni cargar. Además, la referencia corre con dos tarjetas vacías y formateadas mientras el port no tiene ninguna.

**Approach:** Traducir `libmcrd` del asm USA (las 11 funciones públicas y sus internas) sobre una capa de BIOS de tarjeta propia del port. Esa capa incluye eventos de tarjeta, el dispositivo `bu` y su sistema de archivos, adaptados del OpenBIOS (MIT) que usa la referencia. Debajo va un modelo de tarjeta con tiempos del reloj (AD-8). El Host guarda un `.mcd` crudo de 131.072 bytes al terminar cada comando de escritura, con temporal + `rename` (AD-13). Las Escenas `card_newgame` (crear el save en la ranura 1) y `card_continue` (cargarlo con "Continue") se comparan contra PCSX-Redux usando la misma tarjeta.

## Boundaries & Constraints

**Always:**
- Comentario `asm:` en cada función traducida. Atribución MIT al OpenBIOS de PCSX-Redux (SHA `2068784a552b598940191e8431585b7f96408b3f`, `src/mips/openbios/card/`, `sio0/card.c`) en el código adaptado. No copiar código de `src/core/` de Redux, que es GPL.
- El formato del `.mcd` y de las tarjetas nuevas sale de psx-spx. Una tarjeta nueva del port es idéntica byte a byte a la que crea Redux para un archivo inexistente. El SHA-256 de esa tarjeta puede ir al repo porque no tiene datos del disco.
- Los costos de tarjeta (por sector y por comando) son hipótesis en `port/psyq/clock_costs.h` y los calibra 3.10. Las Escenas comparan por checkpoint, así que el desfase absoluto de VBlanks no bloquea.
- `run.py` le da al port las mismas tarjetas que a Redux: dos tarjetas nuevas, salvo que la Escena indique otra.
- Las tarjetas de referencia y las generadas viven fuera del repo. En el repo solo quedan hashes.
- Siguen verdes `scene_title*`, `reference_newgame_state`/`scene_newgame_state`, `scene_title_audio_compare`, `scene_xa_op1_compare` y `scene_movie0*`. `make compare` sigue idéntico. Todo cambio en `src/` va detrás de `DW1_PORT` y se anota en `docs/port/src-changes.md`.

**Never:**
- Emular SIO0 a nivel de byte o de registro, o parchear el BIOS en RAM. Las funciones `_patch_card*`, `_copy_memcard_patch`, `_dsr_start`, `_check_dsr` y `_patch_wait_start` quedan sin efecto, documentadas.
- Persistencia web (IndexedDB) e importar/exportar tarjetas: es la Épica 4. En web la tarjeta queda ausente.
- Ventana SDL3 (3.9) y calibración de costos (3.10).
- Cambiar `state_vars.txt` global, porque obligaría a recapturar todas las referencias.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tarjeta nueva | ruta inexistente | se crea formateada, igual a la de Redux (SHA fijo) | N/A |
| Sin tarjeta | sin `--card1` | `MemCardExist`/`MemCardAccept` informan ausencia como el asm; el menú ofrece jugar sin tarjeta | N/A |
| `.mcd` con otro tamaño o ilegible | 64 bytes, 200 KB, sin permisos | tarjeta ausente y mensaje en stderr; el archivo no se toca | sin abortar |
| Encabezado VGS de 64 bytes | archivo de 131.136 bytes | se carga saltando el encabezado y se guarda crudo de 131.072 | N/A |
| Tarjeta llena | 15 bloques usados | `MemCardCreateFile` da el mismo código que el asm/OpenBIOS (el Juego muestra el error 7) | N/A |
| Corte durante la escritura | `SIGKILL` inyectado antes del `rename` o durante el temporal | al reabrir, la tarjeta es la anterior o la nueva completa | L1 sin disco |
| Dos comandos seguidos sin `MemCardSync` | como en `tickMainMenu` | igual que el asm: el segundo devuelve 0 si hay uno pendiente | N/A |

**Decisiones del dueño (2026-10-22):**
1. **Escena de save (opción A):** `card_newgame` crea el save en la ranura 1 desde New Game (`MemCardCreateFile` + `MemCardWriteFile` de los 0x2000 bytes de `SAVE_FILE`, `main_menu.c:3558-3636`) y `card_continue` lo carga con "Continue". El guardado en partida por `openSaveMachine` queda fuera de esta historia.
2. **`movie0_skip` en el mismo VBlank (opción A):** con las tarjetas presentes como en Redux, el port sale con Start en el VBlank 122, igual que la referencia (antes 123). Se quita `skip_vblank_delta` de `movie0_skip.toml` y el test exige igualdad exacta. La Historia 3.10 conserva la calibración del llenado de `ClearImage` tras `CdlPause` y de la fase de `PadRead`, porque esos costos siguen sin calibrar.

</frozen-after-approval>

## Code Map

- `port/psyq/libmcrd.c` -- stubs que se reemplazan. Hoy guarda solo el comando pendiente (patrón de DW2-PC).
- `asm/us/main/psyq/libmcrd.s` -- fuente de la traducción (66 `glabel`). Públicas: `MemCardInit/Start/Exist/Accept/ReadFile/WriteFile/CreateFile/DeleteFile/Format/GetDirentry/Sync` y también `MemCardOpen/ReadData/WriteData/Callback`. Internas: `_card_*_back`, `_card_read_file`/`_card_write_file`, `_rslt_to_code`, `_callback_handler` (corre por `VSyncCallbacks`), `_card_open/_start/_stop/_close`, `_clr/_get/_chk_card_event[_x]`, `UserFunc*`. Desde `:2544` hay trampolines del BIOS (`_card_info`, `_card_load`, `_card_write`, `_new_card`, `InitCARD2`…) y parches (`_patch_card`…); estos van a la capa de BIOS del port.
- `asm/us/main/psyq/libapi.s` -- trampolines del BIOS que usa `libmcrd`: `open/close/read/write/lseek/firstfile/nextfile/erase/format`, `_bu_init`, `OpenEvent/CloseEvent/EnableEvent/TestEvent`, `EnterCriticalSection`/`ExitCriticalSection`. Ninguno está implementado en `port/psyq` (`libapi.c` solo tiene heap y RCnt).
- `build/pcsx-redux-src/src/mips/openbios/card/{backupunit.c,device.c}`, `sio0/card.c`, `kernel/events.c` -- base MIT del dispositivo `bu`, el directorio y los eventos `SwCARD`/`HwCARD`. Reemplazar el driver SIO0 por el modelo de sectores con eventos del reloj.
- `src/main/main_menu.c` -- consumidor. `updateMemoryCardState` (`:3287`) hace polling de `MemCardExist`. `tickMainMenu` (`:3319`): New Game → `0xA` (pos 0 = ranura 1, pos 2 = sin tarjeta) → `0xC` Accept → `0xD` direntries → `0xE` slot → `0xF` confirmar → `0x10` Create → `0x11` Write + `loadSavegame`. Continue → `0x14…0x19` (`ReadFile :3751`). `loadSavegame` (`:5016`) da los símbolos para comparar el estado cargado (`MONEY`, `SAVED_STATE`, `INVENTORY`, `YEAR/DAY/HOUR/MINUTE`, `MERIT`, `TAMER_ENTITY`, `PARTNER_ENTITY`, `MAIN_STATE`).
- `src/main/main.c:3419` -- `MemCardInit(1)`/`MemCardStart()`. Ya tiene filas en `src-changes.md:65-66`.
- `port/psyq/clock.{h,c}`, `port/psyq/clock_costs.h` -- eventos de fin de sector/comando. Agregar las constantes de tarjeta como hipótesis.
- `port/host/headless/main.c:42-82` -- CLI. Agregar `--card1`/`--card2` con la ruta del `.mcd`. Los callbacks Host↔`libmcrd` (cargar imagen, commit al terminar una escritura) siguen la forma de `port/host/plat.h`.
- `tools/port/reference/capture.py:134`, `capture.lua` -- hoy Redux crea `work/card{1,2}.mcd` nuevas. Agregar una opción para copiar una tarjeta dada a `card1.mcd` antes de correr y exportar su SHA-256/copia al terminar.
- `tools/port/scenes/run.py` (`validate_scene :113`, `state_variables :186`, `state_fingerprint :236`, `:529`), `newgame.toml` (base de inputs), `test_runner.py` -- agregar las claves de Escena `card1` (nueva o salida de otra Escena) y `state_vars` (archivo propio por Escena, que entra en el fingerprint), y la comparación del SHA-256 final de `card1.mcd` entre port y referencia.
- `CMakeLists.txt:478-513` -- patrón de tests clase B (`reference_*`/`scene_*`, skip 77, timeout). `.gitlab-ci.yml`: los nuevos tests clase B y los L1.
- `docs/port/stubs.md:14-24,51-53`, `tools/port/psyq/gen_stubs.py`, `psyq_functions.tsv` -- sacar `libmcrd` y los BIOS implementados.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/bios_card.{h,c}` (o similar) -- eventos de tarjeta (`OpenEvent/CloseEvent/EnableEvent/TestEvent`, `DeliverEvent` interno, secciones críticas), `InitCARD/StartCARD/StopCARD`, `_bu_init`, `_card_info/_card_load/_card_write/_card_clear/_new_card` y `open/close/read/write/lseek/firstfile/nextfile/erase/format` sobre `bu00:`/`bu10:`, adaptados de OpenBIOS. El fin de sector es un evento del reloj.
- [x] `port/psyq/card_image.{h,c}` -- imagen de 128 KB por puerto, formato nuevo de psx-spx, carga (incluye el encabezado VGS) y commit atómico vía Host.
- [x] `port/psyq/libmcrd.c` -- traducción completa del asm. Los parches quedan sin efecto, documentados.
- [x] `port/psyq/clock_costs.h` -- costos de tarjeta como hipótesis, con referencia a 3.10.
- [x] `port/host/headless/main.c` + host de tarjeta nativo -- `--card1/--card2`; commit al terminar cada comando de escritura de `libmcrd` (temporal en el mismo directorio, `fsync`, `rename`). Punto de corte inyectable por variable de entorno, solo para tests.
- [x] `port/psyq/tests/test_card*.c` y `CMakeLists.txt` -- L1 sin disco (nativo, ASan y web donde aplique): formato nuevo == SHA de Redux; Create/Write/Read/Delete/GetDirentry/Format contra valores esperados; tarjeta llena; archivos inválidos; ida y vuelta de 0x2000 bytes. Test de `SIGKILL` en nativo con un proceso hijo.
- [x] `tools/port/reference/capture.{py,lua}` -- tarjeta de entrada y exportación del `.mcd` final.
- [x] `tools/port/scenes/{run.py,test_runner.py,card_newgame.toml,card_continue.toml,card_state_vars.txt}` -- Escenas, claves nuevas y tests del runner.
- [x] `CMakeLists.txt`, `.gitlab-ci.yml` -- `reference_card_newgame`, `scene_card_newgame_compare`, `reference_card_continue`, `scene_card_continue_compare` (clase B, lock, skip 77).
- [x] `docs/port/memcard.md`, `docs/port/stubs.md`, `docs/port/reference.md`, `gen_stubs.py` -- modelo, atribución, hipótesis de costo y métricas.

**Acceptance Criteria:**
- Given `card_newgame` con tarjetas nuevas en ambos lados, when el Juego crea el save en la ranura 1, then el estado coincide en cada checkpoint y el `card1.mcd` final del port es idéntico byte a byte al de Redux, con 131.072 bytes.
- Given el `card1.mcd` que escribió el port en `card_newgame`, when `card_continue` lo carga con "Continue" en el port y en Redux, then Redux lo muestra y lo carga, y las variables de `card_state_vars.txt` después de `loadSavegame` coinciden entre ambos lados y con las del momento del guardado.
- Given un `SIGKILL` inyectado durante el commit, when se reabre la tarjeta, then es la anterior o la nueva, nunca una mezcla.
- Given el reemplazo de los stubs, when corren `title`, `newgame`, `title_audio`, `xa_op1` y `movie0`, then siguen pasando contra sus referencias.

## Implementation Notes

## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|---|---|
| 1 | blind/edge | `transfer`/`data_back` reintentan sin fin con offset, buffer o tamaño inválidos | low | rechazado | El asm (`_card_read_data`, `libmcrd.s`) repite `lseek`/`read` en un bucle cerrado ante error; el port es igual de fiel. El Juego solo pasa offset 0 y 0x80/0x2000. |
| 2 | edge | transferencia más larga que el archivo queda pendiente | low | rechazado | Mismo comportamiento del asm; inalcanzable desde `main_menu.c` (tamaños fijos de 1 bloque). |
| 3 | blind/edge | `MemCardStart` después de `MemCardStop` no reactiva el dispositivo | low | rechazado | `MemCardStop` no existe en el exe USA ni lo llama el Juego; solo lo usan los tests como desmontaje. |
| 4 | blind/edge | `_card_start` repetido agota los 32 eventos | low | rechazado | El Juego llama `MemCardStart` una vez (`main.c:3420`); el asm también abre los ocho eventos en cada llamada. |
| 5 | blind/edge | `fopen` de una FIFO bloquea el arranque | low | rechazado | Requiere pasar una FIFO a `--card1`; el arreglo reestructura la apertura. |
| 6 | blind/edge | `--card1` y `--card2` con el mismo archivo se pisan | low | rechazado | Error de configuración de la CLI de depuración; resolver alias agrega lógica nueva. La 3.9 y la Épica 4 eligen las rutas. |
| 7 | blind/edge | un commit fallido deja la imagen en memoria modificada | low | rechazado | El Juego recibe error; un commit posterior persiste lo que el Juego escribió, como una escritura parcial en hardware. El `.mcd` en disco nunca queda mezclado. |
| 8 | blind/edge | `repair_dirs` no rechaza ciclos ni bloques compartidos | low | rechazado | Los recorridos están acotados por `count ≤ 15` e índices validados (`bios_card.c:209-277`, `absolute :364`), sin acceso fuera de límites; solo afecta tarjetas ya corruptas. La prueba de terminación entra en la fila 18. |
| 9 | blind/edge | `bios_bu_io` con tamaño no múltiplo de 128 | false | rechazado | Su único llamador es `transfer`, que rechaza `bytes & 127` (`libmcrd.c:473`); `read`/`write` del BIOS no se exportan al Juego. |
| 10 | blind/edge | `run.py` reutiliza la referencia de estado sin comprobar la tarjeta de referencia del preset | medium | patch | `run.py:622` mira solo el sello de estado; si falta `final_card('reference', id, build)` la comparación falla después. Arreglo: incluir `valid_card_export` en la condición de recaptura. |
| 11 | blind/edge | `MemCardOpen` durante un comando pendiente | low | rechazado | El Juego no llama `MemCardOpen`. |
| 12 | edge | borrar un archivo con descriptor abierto | low | rechazado | El Juego no lo hace; borra desde el menú sin archivos abiertos. |
| 13 | edge | callback de fin que espera sincrónicamente | false | rechazado | DW1 no registra `MemCardCallback` (sin usos en `src/`). |
| 14 | edge | `prepare_cards` borra una entrada que coincide con su salida | low | rechazado | Solo si una Escena se nombra a sí misma en `card1`; el arreglo agrega una guarda nueva. |
| 15 | edge | la tarjeta se exporta antes de que pasen todos los checks | low | rechazado | Si `card_newgame` falla, su propio test queda rojo; `card_continue` no oculta ese fallo. |
| 16 | verification-gap | sin test de commit fallido hacia `MemCardSync`/callback | medium | patch | Agregar a `test_card.c` un commit que falle y verificar el resultado 2 en Sync y en el callback. |
| 17 | verification-gap | `--card2` sin verificación observable | medium | patch | Prueba del ejecutable con dos rutas que confirme la persistencia independiente de la ranura 2. |
| 18 | verification-gap | directorios corruptos sin prueba | low | patch | Imágenes sintéticas con índices fuera de rango, ciclos y cadenas truncadas en `test_card.c`, con terminación acotada bajo ASan. |

## Design Notes

- **Commit por comando, no por sector:** el `.mcd` se reemplaza cuando `libmcrd` termina un comando de escritura (lo informa `MemCardSync`). Así un corte deja el archivo anterior o el nuevo, y la web (AD-13) usa el mismo punto para mandar la copia a IndexedDB.
- **OpenBIOS como base del BIOS:** la referencia corre OpenBIOS. Copiar su lógica de directorio y asignación de bloques da la misma tarjeta byte a byte, y eso es lo que permite comparar los hashes.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless'` -- expected: verde. Igual con `native-asan` y `web`.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh` (nativo y ASan, vía `tools/port/remote_run.sh`) -- expected: clase B verde sin omisiones, incluidas las cuatro de tarjeta.
- `make compare` en `dw-build` -- expected: `Compiled binaries match with target binaries`.
