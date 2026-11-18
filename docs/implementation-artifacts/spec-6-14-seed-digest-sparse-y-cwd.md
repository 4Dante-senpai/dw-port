---
title: 'Historia 6.14: Semilla del BIOS, digest disperso y directorio por corrida'
type: 'feature'
created: '2026-11-17'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
baseline_commit: '23ed6a330218a59f6c3b0fdf181afdf681162c5b'
context:
  - '{project-root}/docs/implementation-artifacts/epic-6-context.md'
  - '{project-root}/docs/port/playtest.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El soak del homelab siempre arranca con la misma semilla de `rand()` del BIOS, así que la variedad entre tandas depende solo de los inputs del bot. El digest hashea la pantalla en cada VBlank aunque escriba una línea cada 60, lo que frena el soak en release. Los cores de una caída caen en el directorio desde donde se lanzó el runner.

**Approach:** `dw1 --seed N` fija el estado inicial de `rand()` (sin el flag, `0x24040001` como hoy) y `run.py --rand-seed N` lo pasa a todas las corridas. `--digest-sparse` hace que el digest hashee solo en los múltiplos de `--digest-every`: sigue escribiendo una línea en cada uno, que funciona como heartbeat del detector de cuelgues. Cada proceso del runner corre con `cwd` en su directorio de trabajo.

Decisiones del dueño (plan aprobado en sesión, 2026-11-17):
- Sin `--seed` ni `--digest-sparse`, los digests y la invocación del soak no cambian.
- `--digest-sparse` queda apagado por defecto; el dueño lo activa en el CT 235. Si se pide, se aplica a todos los builds de la tanda.
- `DW1_TRACE` por corrida ya existe (`run.py`): el hueco real es el `cwd`.
- La sección "Contrato con el soak del homelab" de `docs/port/playtest.md` se extiende con lo nuevo.

## Boundaries & Constraints

**Always:** todo en `port/` y `tools/`; nada en `src/`. `--seed` aplica a la resolución del prefijo y a cada build, para que el prefijo grabado se reproduzca con la misma semilla. `reproduce.sh` repite semilla, modo de digest y directorio. `-Werror`. L1 sin disco; clase B con `DW1_DISC_DIR` (skip 77 sin él). El digest disperso es determinista y comparable entre builds que lo usen.

**Never:** cambiar el digest por defecto, el formato de la línea de `digest.txt` ni las claves actuales de `report.json`/`summary.json`. No mezclar en una tanda builds con y sin disperso. No llevar `--seed` ni `--digest-sparse` al build web.

## I/O & Edge-Case Matrix

| Escenario | Input / Estado | Salida esperada | Manejo de errores |
|---|---|---|---|
| Sin flags | invocación actual | mismos digests y `reproduce.sh` que hoy (más `cd`) | N/A |
| Semilla por defecto explícita | `--seed 0x24040001` | mismo digest que sin flag | N/A |
| Otra semilla | `--seed 1` | digest distinto, determinista | N/A |
| Semilla inválida | vacía, no numérica, > 0xFFFFFFFF, repetida | uso (exit 2) | mensaje en stderr |
| Disperso | `--digest-sparse --digest-every 60` | una línea cada 60 VBlanks con las mismas columnas de pantalla que el modo denso; cadena distinta; determinista | N/A |
| Disperso con cuelgue | el juego deja de avanzar | `hang` como hoy | N/A |
| Runner | `--rand-seed N`, `--digest-sparse` | flags en prefijo y en todos los builds; `report.json`/`summary.json` registran `rand_seed` y `digest_sparse` | `--rand-seed` inválido: error de uso de argparse |
| `cwd` | crash que deja `core` relativo | el archivo queda en el directorio de la corrida | N/A |

</frozen-after-approval>

## Code Map

- `port/psyq/libc.c:47-66` -- `BIOS_RAND_SEED 0x24040001u`, `rand_state`, `rand()` y `srand(seed)`; el juego no llama a `srand`, así que el host puede usarlo para la semilla antes de arrancar.
- `port/host/native_cli.c` -- parseo dentro de `if (i + 1 < argc)`; `--digest-every` (líneas ~70-74) es el modelo de validación numérica; `usage()`; `native_cli_run` instala el volcado (6.13) y arranca el juego: aplicar la semilla ahí, antes de `port_run_game`.
- `port/host/native_cli.h` -- `NativeCli` (agregar semilla y si se dio).
- `port/psyq/crt.h:19` -- `PortCapture.digest_every`; agregar `digest_sparse`.
- `port/psyq/playtest.c:84-117` -- `port_playtest_tick`: hoy hashea la pantalla y encadena en cada VBlank y escribe en `k % every == 0`. En disperso, salir antes de leer la pantalla si `k % every != 0`. `port_playtest_finish` escribe la última línea si `last % every`: en disperso no hay muestra pendiente fuera de los múltiplos, salvo el VBlank final, que debe muestrearse igual que hoy para que el runner vea el límite pedido.
- `port/host/web/main.c:118-121` -- el web parsea `--digest-every`; no se toca más allá de inicializar campos nuevos si hace falta para compilar.
- `tools/port/playtest/run.py` -- `execute()` arma el comando de cada build con `--digest-every 60`; `observe()` es el punto común del prefijo y de los builds (agrega `--death-file`, escribe `reproduce.sh`, lanza `Popen`); `main()` parsea flags y arma `report.json`/`summary.json`. `bot.resolve_prefix` (`tools/port/playtest/bot.py:36-75`) arma el comando del prefijo y lo lanza vía `launch` → `observe()`.
- `tools/port/playtest/test_playtest.py` -- tests L1 con binarios falsos y con `test_playtest_runtime`; `port/psyq/tests/test_playtest.c` prueba el digest en C.
- `tools/port/playtest/test_channel.py`, `test_death.py` -- modelos de test clase B sobre el prefijo `newgame.toml`.
- `CMakeLists.txt:476-482` -- tests clase B `playtest_channel`/`playtest_death` (modelo para el nuevo).
- `docs/port/playtest.md` -- sección "Contrato con el soak del homelab" (creada en la 6.13).

## Tasks & Acceptance

**Execution:**
- [x] `port/host/native_cli.c`, `port/host/native_cli.h` -- `--seed N` (decimal o `0x`, 0..0xFFFFFFFF, rechaza vacío/no numérico/repetido con exit 2), línea de `usage`; `srand(N)` antes de arrancar el juego solo si se dio -- semilla del BIOS.
- [x] `port/psyq/crt.h`, `port/psyq/playtest.c`, `port/host/native_cli.c` -- `--digest-sparse` (flag sin valor) en `PortCapture`; en `port_playtest_tick` no leer ni hashear la pantalla fuera de los múltiplos de `every` (salvo la muestra final de `port_playtest_finish`) -- digest disperso.
- [x] `port/psyq/tests/test_playtest.c` -- disperso: líneas en los mismos VBlanks que el denso, columna de pantalla igual, cadena distinta y repetible; denso sin cambios -- L1 del digest.
- [x] `tools/port/playtest/run.py` -- `--rand-seed N` y `--digest-sparse`; pasar `--seed` al prefijo y a cada build y `--digest-sparse` a cada build; `Popen(cwd=work)` y `cd` en `reproduce.sh`; `rand_seed` y `digest_sparse` en el nivel superior de `report.json` y `summary.json` -- runner.
- [x] `tools/port/playtest/test_playtest.py` -- L1: flags inválidos de `dw1` (`--seed`), propagación al prefijo y a todos los builds con binarios falsos, `reproduce.sh` con `cd`/semilla/disperso, archivo relativo creado por el proceso cae en el directorio de la corrida, claves nuevas de JSON, invocación del soak sin flags nuevos con `reproduce.sh` igual al de hoy salvo el `cd` -- cobertura de la matriz.
- [x] `tools/port/playtest/test_seed_sparse.py` (nuevo, clase B) + `CMakeLists.txt` (`playtest_seed_sparse`, SKIP 77, `FIXTURES_REQUIRED disc_pack`) -- `dw1` real sobre el prefijo `newgame.toml` más ~600 VBlanks: sin flag y con `--seed 0x24040001` dan el mismo digest final, `--seed 1` da otro; denso y disperso escriben líneas en los mismos VBlanks con la misma columna de pantalla; imprime VBlanks/s de ambos -- evidencia sobre el juego real.
- [x] `docs/port/playtest.md` -- `--seed`, `--rand-seed`, `--digest-sparse` y `cwd` por corrida; extender "Contrato con el soak del homelab" (flags opcionales nuevos, claves `rand_seed`/`digest_sparse`, cambio de comportamiento: `cwd` y `cd` en `reproduce.sh`) -- contrato.
- [x] Medición -- VBlanks/s de `native-headless-release` denso contra disperso sobre la misma sesión (≥ 20.000 VBlanks después del prefijo), anotada en Implementation Notes -- dato para que el dueño decida activarlo.

**Acceptance Criteria:**
- Given la invocación del soak del homelab sin flags nuevos, when corre en `native-headless-release` y `native-asan`, then `playtest_smoke` y las tres regresiones de la 6.10 conservan sus digests y `summary.json` conserva sus claves (más `rand_seed: null`, `digest_sparse: false`).
- Given `--digest-sparse` en el runner, when un build se cuelga, then la corrida queda `hang` dentro de `--hang-seconds` como en modo denso.
- Given `--rand-seed N`, when se ejecuta `reproduce.sh` de una corrida, then repite la misma semilla, el modo de digest y el directorio, y llega al mismo digest final.

## Implementation Notes

- `--seed` aplica `srand(N)` en `native_cli_run` antes de `port_run_game`, solo si se dio. Acepta decimal o `0x`; `0x` solo, vacío, fuera de rango o repetido salen con 2.
- Disperso: `port_playtest_tick` sale antes de leer la pantalla fuera de los múltiplos de `every` y recuerda el VBlank pendiente; `port_playtest_finish` muestrea ese último VBlank para que el runner vea el límite pedido. El modo denso queda byte a byte igual.
- El runner pasa `--seed` al prefijo y a cada build, y `--digest-sparse` solo a los builds (el prefijo usa su propio digest para el VBlank final). `rand_seed` (null) y `digest_sparse` (false) van en el nivel superior de `report.json` y `summary.json`.
- Cambio no pedido: timeout de `playtest_unit` de 30 s a 90 s; la suite tarda 9-16 s y los tests nuevos la acercan al límite con CPU cargada.
- `playtest_seed_sparse` hace seis replays completos: ~800 s en release y ~1900 s en ASan (timeout 3600).
- Medición (`native-headless-release`, prefijo `newgame.toml`, 40.000 VBlanks en total, una corrida cada uno, host remoto compartido): denso 252,5 VBlanks/s, disperso 441,6 VBlanks/s (~1,75×). Es tiempo de pared con el arranque incluido: la razón es aproximada.
- Implementador: los cinco builds verdes; L1 verde en los tres presets nativos salvo los 5 `scene_*_compare` de manifest parcial; clase B release 10/10 (regresiones con sus digests) y ASan `pack_disc`, `playtest_unit`, `playtest_seed_sparse`.
- Orquestador (2026-11-17, remoto, con los dos patches de tests de la revisión): cinco builds verdes sin warnings; L1 102/102, 100/100 y 102/102 salvo los 5 `scene_*_compare` de manifest parcial; clase B release 10/10 (995 s) y ASan 3/3 (2177 s).

## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|---|
| 1 | blind, edge | `digest_text` queda viejo entre muestras dispersas | false | `port_playtest_digest()` no tiene consumidores fuera de los tests; el canal y el volcado no lo leen | rechazo |
| 2 | blind, edge | La muestra final dispersa lee la pantalla en `finish`, no en su VBlank | low | Determinista igual; `playtest_seed_sparse` compara las filas completas (incluida la final) contra el denso en el juego real y coinciden | rechazo |
| 3 | blind, edge | `--seed` al final sin valor | false | Cae al `return usage(argv[0])` del final del bucle (`native_cli.c`) | rechazo |
| 4 | blind, edge | `NativeCli` sin inicializar | false | `native_cli_parse` empieza con `*options = (NativeCli){0}`; `PortCapture` del web usa inicializador designado | rechazo |
| 5 | blind, edge | `rand_seed_arg` acepta `0X`/`1_0` que C rechaza | false | El runner pasa `str(valor)` en decimal: C recibe siempre un decimal válido | rechazo |
| 6 | blind | Ayuda "every 60 VBlanks" | false | `execute()` fija `--digest-every 60`; el texto es exacto | rechazo |
| 7 | blind, vgap | `reproduce.sh` nunca se ejecuta en los tests | low | Gap real y barato de cerrar con un binario falso ejecutado desde otro `cwd` | patch |
| 8 | blind | Timeout de `playtest_unit` sin explicación | false | Registrado en Implementation Notes (9-16 s medidos) | rechazo |
| 9 | blind | Clase B no compara disperso entre builds | low | El modo es determinista por construcción (misma función, mismo VBlank); el soak con dos builds lo comprueba | rechazo |
| 10 | blind, edge | Nada impide mezclar modos | false | El runner aplica el flag a todos los builds de la tanda; fuera del runner no hay comparación de digests | rechazo |
| 11 | vgap, edge | Sin test de `hang` en disperso | false | El heartbeat es la misma línea cada 60 VBlanks que en denso; `test_playtest.c` verifica que las líneas caen en los mismos VBlanks y el detector del runner no cambia | rechazo |
| 12 | vgap | El prefijo no se prueba sin `--digest-sparse` con ambos flags | low | Aserción directa en el test existente | patch |
| 13 | edge | Salto sobre un múltiplo sin línea | false | Idéntico al denso: la línea se escribe solo con `k % every == 0` en ambos modos | rechazo |
| 14 | edge | Rango de `--seed` con `unsigned long` de 32 bits | false | `strtoul` fija `ERANGE` fuera de rango y `errno` se verifica | rechazo |
| 15 | edge | Rutas relativas en el comando del prefijo con `cwd=work` | false | Binario, Pack y archivos del prefijo se pasan absolutos (`out` se resuelve en `main`) | rechazo |
| 16 | edge | Rutas con caracteres raros en `cd` | low | `shlex.quote` ya se usa; rutas del runner sin espacios en la práctica | rechazo |
| 17 | edge | El test C toma el digest después de `finish` | low | Es intencional: en disperso la última muestra ocurre en `finish` | rechazo |

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'for p in native-headless native-asan native web native-headless-release; do cmake --preset $p && cmake --build --preset $p || exit 1; done'` -- expected: los cinco builds verdes con `-Werror`.
- `tools/port/remote_run.sh sh -c 'for p in native-headless native-asan native-headless-release; do env -u DW1_DISC_DIR ctest --preset $p -j12; done'` -- expected: L1 verde (salvo los `scene_*_compare` de manifest parcial del build remoto, ajenos a esta historia).
- `tools/port/remote_run.sh sh -c 'export DW1_DISC_DIR=/dw/game_rom/extracted; ctest --preset native-headless-release -R "^(pack_disc|playtest_)" --output-on-failure'` -- expected: clase B verde, `playtest_seed_sparse` incluido, digests de `main`. (`remote_run.sh` no reenvía `DW1_DISC_DIR`: exportarlo dentro.)
- `tools/port/remote_run.sh sh -c 'export DW1_DISC_DIR=/dw/game_rom/extracted; ctest --preset native-asan -R "pack_disc|playtest_unit|playtest_seed_sparse" --output-on-failure'` -- expected: verde.
