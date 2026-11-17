---
title: 'Historia 6.13: Diagnóstico al morir'
type: 'feature'
created: '2026-11-17'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
baseline_commit: '3dd07ba7c21bcc5438b20e97869a4227c1ca152d'
context:
  - '{project-root}/docs/implementation-artifacts/epic-6-context.md'
  - '{project-root}/docs/port/playtest.md'
  - '{project-root}/docs/implementation-artifacts/spec-6-12-canal-de-estado-en-vivo.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Cuando una corrida del soak muere (señal, `PORT_ASSERT`, ASan/UBSan, watchdog o cuelgue), `report.json` solo guarda la cola de stderr: no hay estado del juego ni inputs recientes, y no se pueden agrupar caídas iguales entre semillas y tandas.

**Approach:** `dw1 --death-file FILE` abre el archivo al arrancar y, al morir, escribe en él el estado de la 6.12, los últimos 32 cambios de input, el overlay y los frames crudos del hilo del juego. El runner lo pasa siempre, simboliza los frames con `llvm-symbolizer-18`, calcula una firma agrupable y la publica en `summary.json` sin datos privados.

Decisiones del dueño (plan aprobado en sesión, 2026-11-17):
- Disparadores: SIGSEGV/SIGBUS/SIGILL/SIGFPE (handler propio, sin ASan), SIGABRT (`PORT_ASSERT`), callback de muerte de ASan (`__asan_set_death_callback`, cubre UBSan no recuperable), watchdog (volcado antes de `exit(3)`) y SIGQUIT (lo manda el runner al detectar cuelgue).
- Cuelgue en el runner: SIGQUIT, espera de 5 s, después SIGKILL al grupo. Un SIGQUIT que llega a otro hilo se reenvía al hilo del juego con `pthread_kill` para que el backtrace sea el del juego.
- Firma `<tipo>:<f1>|<f2>|<f3>`: solo nombres de función de los 3 primeros frames del binario, tras quitar frames del propio volcado, del watchdog, de libc y de los sanitizers. Tipos: `crash-sig<N>`, `assert`, `asan-<kind>`, `ubsan-<check>`, `watchdog`, `hang`; `exit-<N>` para una salida no cero sin volcado. Sin frames, la firma es solo `<tipo>`. Sin simbolizador, se usan direcciones y `symbolized: false`.
- `report.json` (privado) gana `death` y `signature` por corrida; `summary.json` gana solo `signatures: [{signature, count, seeds}]`.
- La sección "Contrato con el soak del homelab" de `docs/port/playtest.md` nace en esta historia y la extienden 6.14–6.16.

## Boundaries & Constraints

**Always:** todo en `port/` y `tools/`; nada en `src/`. Los digests existentes no cambian (el volcado solo escribe al morir). La invocación del soak del homelab (`run.py --soak --build native-headless-release --build native-asan --prefix tools/port/scenes/newgame.toml --vblanks 540079 --jobs 14 --seed N... --output DIR`) sigue funcionando sin cambios. `-Werror` en el código nuevo. El texto actual de `crash()` en stderr (`dw1: signal N at address P; backtrace:` + frames) se conserva para sesiones con ventana. Un solo volcado por proceso (el primero gana). `summary.json` no lleva estado, inputs, rutas ni frames: solo firmas, conteos y semillas. Tests L1 sin disco; clase B con `DW1_DISC_DIR` (skip 77 sin él).

**Never:** cambiar el comportamiento del juego, el orden de VBlanks ni los códigos de salida actuales (watchdog sigue en 3; un crash sigue muriendo por la misma señal; ASan conserva su salida). No volcar bytes del disco, del Pack ni de la RAM. No exigir `--death-file` (sin el flag todo queda como hoy, salvo los handlers que ya existían). Nada de esto en el build web.

## I/O & Edge-Case Matrix

| Escenario | Input / Estado | Salida esperada | Manejo de errores |
|---|---|---|---|
| Crash sin ASan | SIGSEGV en el hilo del juego | stderr como hoy; archivo `reason segv`, `signal 11`, `fault_address`, estado, inputs, frames; muere por SIGSEGV | N/A |
| Crash con ASan | SEGV o heap-overflow | informe de ASan intacto; archivo `reason sanitizer`, frames del callback; código de salida de ASan | N/A |
| `PORT_ASSERT` | `abort()` | archivo `reason abort`, `signal 6`; muere por SIGABRT | N/A |
| Watchdog | 30 s virtuales sin frame | archivo `reason watchdog` antes de `exit(3)` | N/A |
| Cuelgue | runner manda SIGQUIT | archivo `reason quit` con frames del hilo del juego; el runner clasifica `hang` | sin volcado en 5 s: SIGKILL, firma `hang` sola |
| Sin flag | `dw1` sin `--death-file` | idéntico a hoy | N/A |
| Flag inválido | ruta no escribible, valor vacío o repetido | uso (exit 2) antes de arrancar | mensaje en stderr |
| Muerte doble | ASan muere después de un SIGABRT ya volcado | un solo volcado (el primero) | N/A |
| Sin simbolizador | `llvm-symbolizer-18` ausente | `symbolized: false`, firma con direcciones `0x...` | N/A |
| Archivo vacío | corrida sana o muerte sin volcado | `death` ausente en `report.json`; firma desde stderr/código | N/A |

</frozen-after-approval>

## Code Map

- `port/psyq/port.h:9-18` -- `PORT_ASSERT`: imprime y `abort()`. Los presets RelWithDebInfo usan `-O2 -g` sin `NDEBUG`, así que también aborta en `native-headless-release`.
- `port/host/native_cli.c:156-181` -- `crash()` actual (no ASan): mensaje + `backtrace_symbols_fd` + re-raise. Se mueve al módulo nuevo conservando el texto. Parsing de flags del CLI y `usage()` en el mismo archivo (ver `--state-fd` de la 6.12 como modelo de validación).
- `port/psyq/crt.c:213-224` -- `watchdog()`: corre en el hilo del juego (evento del reloj virtual). Volcar antes de `exit(3)`.
- `port/psyq/crt.c:276-298` -- `port_run_game`: crea el hilo del juego (`thread`); guardar su handle para el reenvío de SIGQUIT.
- `port/psyq/state.c` / `state.h` -- `port_state_header(FILE*)` y `port_state_record(FILE*, k, want)` de la 6.12: reutilizar para el estado del volcado (con `want = 0`).
- `port/psyq/playtest.c:53-62` -- `port_playtest_vblank()`, `port_playtest_overlay_id()`, `port_playtest_pad(mask)` (llamado desde `port_pad_latch`, `port/psyq/libetc.c:56`, en cada input publicado por cualquier fuente: archivo, canal, SDL). Ahí va el anillo de 32 cambios `(k, mask)`.
- `CMakeLists.txt:144-149` -- nativo sin PIE: las direcciones de los frames son las del ELF, se simbolizan con `llvm-symbolizer-18 --obj=dw1` sin restar base. Las de libc y del runtime de ASan (`-shared-libasan`) quedan fuera del binario y no resuelven.
- `CMakeLists.txt:463-471` -- `test_playtest_runtime` + `playtest_unit` (L1). `CMakeLists.txt:601-607` -- `test_watchdog`.
- `port/psyq/tests/test_playtest_runtime.c` -- runtime sintético de la 6.12; se extiende con modos de muerte.
- `tools/port/playtest/run.py:38-50` `classify()`, `:70-163` `observe()` (lanza el proceso, detecta cuelgue y hoy hace `killpg(SIGKILL)` en `:114-119`, arma `reproduce.sh` y el resultado), `:284-292` `report.json`/`summary.json`. `observe()` es el punto común del prefijo y de cada build: agregar el flag ahí.
- `tools/port/playtest/channel.py` -- `parse_header`/`parse_record` para leer el estado del volcado.
- `tools/port/playtest/test_playtest.py` -- tests L1 con binarios falsos (scripts Python) y con `RUNTIME`.
- `docs/port/playtest.md:73-85` -- clasificación de fallos actual; documentar volcado, firma y contrato.

No tocar: `src/`, el formato de `digest.txt`, los valores actuales de `status` (`crash`, `assert`, `sanitizer`, `hang`, `usage`) ni las claves actuales de `report.json`/`summary.json`.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/playtest.c`, `port/psyq/playtest.h` -- anillo de los últimos 32 cambios de máscara `(port_playtest_vblank(), mask)` en `port_playtest_pad` y un accesor de solo lectura, del más viejo al más nuevo -- inputs recientes para el volcado.
- [x] `port/psyq/death.c`, `port/psyq/death.h` (nuevo, en `dw1_runtime`; vacío/no-op con `__EMSCRIPTEN__`) -- `port_death_install(fd)`, `port_death_dump(reason, sig, addr)` idempotente (flag atómico), handlers de SIGSEGV/SIGBUS/SIGILL/SIGFPE (sin ASan; conservan el texto de `crash()` y re-raise), SIGABRT y SIGQUIT (instalados solo con `--death-file`), reenvío de SIGQUIT al hilo del juego con `pthread_kill`, `__asan_set_death_callback` con ASan; precargar `backtrace()` al instalar -- volcado best-effort desde contexto de señal.
- [x] `port/psyq/crt.c`, `port/psyq/crt.h` -- guardar el handle del hilo del juego; llamar al volcado de watchdog antes de `exit(3)` -- reenvío y watchdog.
- [x] `port/host/native_cli.c` -- `--death-file FILE` (abre `O_WRONLY|O_CREAT|O_TRUNC`, 0600, rechaza vacío/repetido/no escribible con exit 2), línea en `usage`, instala el módulo; quitar `crash()` local -- entrada del CLI.
- [x] `CMakeLists.txt` -- `death.c` en `dw1_runtime` con los flags del port; `test_playtest_runtime` con lo que necesite; test clase B `playtest_death` (SKIP 77, `FIXTURES_REQUIRED disc_pack`, timeout holgado) -- build y tests.
- [x] `port/psyq/tests/test_playtest_runtime.c` -- modos de muerte por variable de entorno (p. ej. `DEATH_MODE=segv|assert|watchdog|spin` y `DEATH_FILE`): fallo de segmento, `PORT_ASSERT(0)`, sin frames, bucle con nombre reconocible -- L1 de los handlers reales.
- [x] `tools/port/playtest/run.py` -- `--death-file work/death.txt` en `observe()` (también en `reproduce.sh`); `UBSAN_OPTIONS` con `report_error_type=1:print_stacktrace=1` como `ASAN_OPTIONS`; cuelgue: SIGQUIT, 5 s (constante de módulo), SIGKILL al grupo; parsear el volcado; simbolizar con una llamada a `llvm-symbolizer-18 --obj=<binario>` por volcado (dirección − 1); firma; `death` y `signature` por corrida en `report.json`; `signatures` agregadas en `summary.json` -- runner.
- [x] `tools/port/playtest/test_playtest.py` -- L1: cada fila de la matriz con `RUNTIME` (crash con y sin ASan según el build, assert, watchdog, quit con frame del bucle si hay simbolizador, sin flag, flags inválidos) y con binarios falsos (firma por tipo, filtro de frames, `symbolized: false`, SIGQUIT ignorado termina en SIGKILL tras la gracia parcheada, agregación `{signature, count, seeds}`, `summary.json` sin `death`/frames/rutas) -- cobertura de la matriz.
- [x] `tools/port/playtest/test_death.py` (nuevo, clase B) -- `dw1` real sobre el prefijo `newgame.toml`, SIGQUIT tras pasar el prefijo: volcado `reason quit`, estado parseable con pantalla distinta de 0, al menos un frame simbolizado del binario, firma `hang:...` vía las funciones de `run.py` -- evidencia sobre el juego real.
- [x] `docs/port/playtest.md` -- formato del volcado, tipos y regla de firma, SIGQUIT antes de SIGKILL, y sección nueva "Contrato con el soak del homelab" (invocación vigente, flags nuevos opcionales, claves nuevas de JSON, cambios de comportamiento) -- contrato pedido por el dueño.

**Acceptance Criteria:**
- Given cada disparador de la matriz, when el proceso muere con `--death-file`, then el archivo empieza con `dw1-death 1` y trae `reason`, `vblank`, el encabezado y un registro de estado parseables con `channel.py`, a lo sumo 32 líneas `input` y al menos una `frame`, y el código de salida es el de hoy.
- Given la invocación del soak del homelab sin cambios, when corre en `native-headless-release` y `native-asan`, then termina, los digests de `playtest_smoke` y de las tres regresiones de la 6.10 no cambian y `summary.json` conserva sus claves actuales más `signatures`.
- Given dos semillas que mueren en la misma función, when el runner arma `summary.json`, then hay una sola entrada con `count: 2` y ambas semillas.
- Given un cuelgue, when el runner lo detecta, then manda SIGQUIT, espera hasta 5 s, mata con SIGKILL si sigue vivo, y la corrida queda `hang` con firma `hang:...` (o `hang` sin volcado).

## Implementation Notes

- `death.c` vive en `dw1_runtime` (no-op con Emscripten); `native_cli.c` abre el archivo y llama a `port_death_install`. `test_card_cli.c` necesita un stub porque incluye `native_cli.c`.
- Línea extra `pc` en el volcado: el pc exacto que interrumpió la señal. Sin ella, la búsqueda con dirección − 1 resolvía el frame del fallo a la función anterior en -O2. El runner no le resta 1.
- SIGQUIT: tras el volcado el proceso sale con `_exit(131)`, sin core. Si `pthread_kill` al hilo del juego falla, vuelca desde el hilo actual.
- El FILE del volcado usa un buffer estático (`setvbuf`), así el handler no reserva memoria; stdio sigue sin ser async-signal-safe estricto (best-effort).
- `count` en `signatures` cuenta corridas (dos builds de una semilla suman 2); `seeds` es el conjunto ordenado.
- Hipótesis abierta: UBSan no recuperable pasando por el callback de muerte de ASan no se disparó con un error UBSan real; si no pasa, la firma cae a `ubsan-*` sin frames.
- Sin pila alternativa de señales: un desbordamiento de pila no deja volcado.
- Implementador: los cinco builds verdes; L1 verde (salvo los 5 `scene_*_compare` de manifest parcial del build remoto); clase B release `pack_disc`, `playtest_smoke`, `playtest_channel`, `playtest_death` y las tres regresiones (mismos digests); ASan `pack_disc` + `playtest_death`.
- Verificación del orquestador tras los parches de revisión (remoto): los cinco builds verdes; L1 sin disco en `native-headless`, `native-asan` y `native-headless-release` verde salvo los 5 `scene_*_compare` de manifest parcial (`playtest_unit`: 36 tests, sin skips, con simbolizador); clase B release `^(pack_disc|playtest_)` 9/9 (`playtest_death` 303 s, `playtest_smoke` 618 s, `playtest_channel` y las tres regresiones con sus digests); ASan `playtest_unit`, `pack_disc` y `playtest_death` 3/3 (579 s).

## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia | Ruta |
|---|---|---|---|---|---|
| 1 | blind, edge | `fprintf` sobre el FILE de `fdopen` reserva su buffer en la primera escritura, dentro del handler | medium | glibc asigna el buffer en forma perezosa; un crash dentro de `malloc` puede trabar el volcado. Se arregla con `setvbuf` y un buffer estático al instalar | patch |
| 2 | blind, edge | `pthread_kill` a un hilo del juego que ya terminó | low | Ventana mínima (el proceso sale tras `dw1_game_main`), pero se corrige directo: si `pthread_kill` falla, volcar desde el hilo actual | patch |
| 3 | blind, edge | Tras un SIGQUIT que termina dentro de la gracia, no se hace `killpg` del grupo | low | Antes siempre había `killpg`; corrección directa: `killpg` tolerante a `ProcessLookupError` siempre | patch |
| 4 | blind, edge, vgap | `parse_death` lanza `ValueError` ante un volcado truncado y aborta el soak | medium | `k, mask = rest.split()` con `input 5` lanza; el volcado es best-effort por diseño | patch |
| 5 | blind, edge | `symbolize` sin timeout | low | Un simbolizador colgado bloquearía el runner; `timeout` es corrección directa | patch |
| 6 | blind, edge | Frame `0x0` da `-0x1` y bloques desalineados | false | `backtrace()` no devuelve 0 en x86 y la salida de `llvm-symbolizer` tiene un bloque por dirección | rechazo |
| 7 | blind, edge | `runtime error:` recuperable previo convierte otras muertes en `ubsan-*` | false | `native-asan` compila con `-fno-sanitize-recover=undefined`: el primer `runtime error:` termina el proceso | rechazo |
| 8 | blind, edge | Exit 3 que no es watchdog | false | El único `exit(3)` del port es `crt.c:222` (watchdog); uso es 2 | rechazo |
| 9 | blind, edge | Frames del handler sin archivo entran a la firma | low | Con `-g` todas las funciones del binario resuelven archivo; agregar nombres genéricos (`crash`, `quit`) al filtro arriesga ocultar funciones del juego | rechazo |
| 10 | blind, edge, vgap | Build solo UBSan sin callback | false | No hay preset solo UBSan; `native-asan` combina ASan+UBSan y registra el callback | rechazo |
| 11 | blind, edge | `argv[++i]` de `--death-file` sin verificar límites | false | La rama está dentro del bloque `if (i + 1 < argc)` (`native_cli.c:69`) | rechazo |
| 12 | blind, edge | fd filtrado ante un `usage()` posterior o `fdopen` fallido | low | El proceso sale enseguida con 2; sin daño | rechazo |
| 13 | blind | `signatures: []` en corridas verdes sin documentar | low | La doc dice que `summary.json` gana la clave; vacía es la lectura natural | rechazo |
| 14 | blind | `UBSAN_OPTIONS` descarta opciones del llamador | false | Se concatenan las del entorno antes de las nuevas | rechazo |
| 15 | blind | `death.txt` viejo entre trials | false | `observe()` lo borra antes de cada lanzamiento y cada build tiene su directorio | rechazo |
| 16 | vgap | El anillo no se prueba pasados 32 cambios | medium | Gap verificado por la capa: el probe genera solo 3 cambios | patch |
| 17 | vgap | Aserciones simbolizadas se saltean en silencio sin simbolizador | medium | Gap verificado: todo bajo `if death['symbolized']` | patch |
| 18 | vgap | Sin aserción de `UBSAN_OPTIONS` ni del mapeo bus/ill/fpe | low | Gap verificado; aserción directa sobre `reproduce.sh` y un modo `fpe` del probe | patch |
| 19 | edge | `ProcessLookupError` si el proceso salió entre `poll` y `kill` | false | Sin `wait` el hijo queda zombi y `os.kill` no falla | rechazo |
| 20 | edge | Carrera de `quit_forward`/`game` | low | Escritura única antes de que el runner pueda mandar SIGQUIT (requiere VBlanks observados) | rechazo |
| 21 | edge | SIGABRT desde otro hilo da frames de ese hilo | low | Son los frames correctos del assert | rechazo |
| 22 | edge | Entradas del anillo con k=0 antes del origen | low | Igual que el resto del playtest (VBlank 0 antes del frame 0); documentado por el formato | rechazo |
| 23 | edge | Anillo roto por lectura concurrente | low | El volcado corre en el hilo del juego (SIGQUIT reenviado, watchdog, crash del juego) | rechazo |
| 24 | edge | `seeds` descarta `None` | false | El runner siempre asigna semilla (`[1]` por defecto) | rechazo |
| 25 | edge | Sin simbolizar, las firmas no agrupan | low | Permitido por la intención (`symbolized: false`) | rechazo |

## Design Notes

Volcado (texto, una clave por línea; `fault_address` y `signal` en 0 si no aplican):

```
dw1-death 1
reason segv|bus|ill|fpe|abort|sanitizer|watchdog|quit
signal 11
fault_address 0x00000000
vblank 1234
# dw1-state 1 k overlay screen ...
1234 3 ...
input 1200 0010
frame 0x0804a1b2
```

`report.json`, por corrida con volcado: `death: {reason, signal, fault_address, vblank, state: {campo: valor}, inputs: [[k, mask]], frames: [{address, function, file, line}], symbolized}` y `signature`. `summary.json`: `signatures: [{signature: "asan-heap-buffer-overflow:fnA|fnB|fnC", count: 2, seeds: [3, 9]}]`, ordenadas por firma.

Tipo: `asan-<kind>` desde `ERROR: AddressSanitizer: <kind>` en stderr; `ubsan-<check>` desde `SUMMARY: UndefinedBehaviorSanitizer: <check>` (`ubsan-undefined-behavior` si falta); `assert` si stderr tiene `PORT_ASSERT` o `reason abort`; `watchdog` con `reason watchdog` o salida 3; `hang` si el runner detectó cuelgue; `crash-sig<N>` para señales; `exit-<N>` para otras salidas no cero. Filtro de frames: sin resolver (`??`, fuera del binario), funciones `port_death_*`, el handler de señal y `watchdog`.

Seguridad en señal: `snprintf` + `write(2)` sobre el fd abierto al instalar y `backtrace`/`backtrace_symbols_fd` precargados; no es async-signal-safe estricto, pero el proceso ya está muriendo y el volcado es best-effort. Que UBSan no recuperable pase por el callback de muerte de ASan con `-shared-libasan` es una hipótesis: el test L1 en `native-asan` lo confirma o lo deja registrado.

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'for p in native-headless native-asan native web native-headless-release; do cmake --preset $p && cmake --build --preset $p || exit 1; done'` -- expected: los cinco builds verdes con `-Werror`.
- `tools/port/remote_run.sh sh -c 'env -u DW1_DISC_DIR ctest --preset native-headless -j8; env -u DW1_DISC_DIR ctest --preset native-asan -j8'` -- expected: L1 verde (salvo los `scene_*_compare` de manifest parcial del build remoto, ajenos a esta historia).
- `tools/port/remote_run.sh ctest --preset native-headless-release -R '^(pack_disc|playtest_)' --output-on-failure` -- expected: clase B verde, `playtest_death` incluido, digests de `main`.
- `tools/port/remote_run.sh ctest --preset native-asan -R 'pack_disc|playtest_death' --output-on-failure` -- expected: verde.
