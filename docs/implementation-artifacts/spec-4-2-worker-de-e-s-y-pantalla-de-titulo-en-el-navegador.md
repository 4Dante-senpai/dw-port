---
title: 'Historia 4.2: Worker de E/S y pantalla de título en el navegador'
type: 'feature'
created: '2026-10-17'
status: 'done'
baseline_commit: '3c08be668a7a67b5f774fc98f77f5b9b71e4e0c8'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-1-el-juego-en-wasm-con-su-propio-hilo-y-presentacion.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El build `web` llega al frame 0 sin Pack: el Juego no puede leer el disco en el navegador porque OPFS solo da handles síncronos en un worker dedicado y el hilo del juego debe bloquear sin usar OPFS (AD-13).

**Approach:** Un worker de E/S (`port/web/io_worker.js`) abre el handle síncrono del Pack en OPFS antes del arranque y atiende pedidos de lectura por un buzón en memoria compartida; el hilo del juego espera con `Atomics.wait`. Con eso, la Escena `title` corre en el navegador y su captura se compara con la nativa.

## Boundaries & Constraints

**Always:** el hilo del juego nunca toca OPFS ni `navigator.storage`; la espera es `Atomics.wait` (sin bucles de espera activa); el formato del Pack y su validación (`pack_open`: magic, versión, SHA del Manifest, índice) son los mismos que en nativo (un solo C en `port/host/pack/`); tests clase A sin datos del disco, clase B saltan con 77 sin `DW1_DISC_DIR`; ningún byte del disco entra al repo ni a la evidencia (solo hashes).

**Never:** selección de disco, construcción o validación del Pack en el navegador (historia 4.3); Shell, input, audio (4.4+); `-sPROXY_TO_PTHREAD`, ASYNCIFY/JSPI; cargar el Pack entero en memoria wasm.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Título web | Pack del disco en OPFS (clase B) | SHA-256 del PPM capturado == captura `native-headless` de la Escena `title`, en Chromium y Firefox | test salta (77) sin disco |
| OPFS envenenado | `navigator.storage` del hilo del juego reemplazado por un objeto que falla | el título pasa igual y no se registra ningún acceso | cualquier acceso registrado falla el test |
| Lecturas concurrentes | Pack sintético, varios pthreads leyendo rangos distintos a la vez | cada lectura devuelve exactamente sus bytes | bytes distintos fallan con offset |
| Sin espera activa | worker que demora cada respuesta | el lector duerme en `Atomics.wait`; iteraciones de espera por pedido acotadas | conteo excedido falla |
| Pack ausente o inválido | OPFS sin Pack o con header corrupto | `pack_open` falla con mensaje y el Juego no arranca | mensaje en `#status`, sin crash del runtime |
| Lectura fuera de rango | offset/longitud más allá del archivo | error explícito al llamador | sin bytes basura |

</frozen-after-approval>

## Code Map

- `port/host/pack/pack.c:846,863,967` -- `Pack{FILE*}`, `pack_open`, `pack_read` (`fseeko`+`fread`): introducir una única costura de lectura posicional (nativo: `FILE*`; web: buzón).
- `port/backend/cdrom.c:12,35,45`, `port/psyq/libcd.c:109,186,216` -- `cdrom_open`/`cdrom_read_file`/`cdrom_read_sector` sobre el Pack; no cambian.
- `port/host/headless/main.c:20,84-105` -- CLI `--pack/--capture-*` y carga de tablas GTE: modelo para `port/host/web/main.c`.
- `port/host/web/main.c:7,19`, `port/web/dev/index.html:11-20`, `port/web/dev/serve.py:9-17` -- argv desde la URL, logs a `#status`, servidor COOP/COEP.
- `port/psyq/crt.c:47-93,102,184-194` -- marca, captura PPM (`P6`), fin por frames con `pthread_exit`, arranque del hilo en web.
- `port/host/web/present_gl.c:79` -- precedente `MAIN_THREAD_EM_ASM` para publicar datos al hilo principal.
- `CMakeLists.txt:118,303,340,384-393,407-415` -- flags web, SHA del Manifest solo nativo, fixtures `synthetic_pack`/`disc_pack`, `scene_title`, link y CTest web.
- `port/host/pack/tests/test_pack.c:81,98,265` -- layout y bytes sintéticos `(sector*31+offset*7)&255`: reutilizar para la clase A web.
- `tools/port/scenes/run.py:29-81`, `tools/port/scenes/title.toml` -- runner nativo y hash del PPM.
- `port/web/tests/{boot.spec.cjs,playwright.config.cjs,run.py}` -- Playwright 1.64.0, Firefox con Xvfb.

## Tasks & Acceptance

**Execution:**
- [x] `port/host/pack/pack.c` (+ `pack.h` si hace falta) -- costura de lectura posicional usada por `pack_open` y `pack_read`; implementación nativa sin cambio de comportamiento.
- [x] `port/host/web/io_mailbox.c` + `port/web/io_worker.js` -- buzón en memoria compartida (pedido: offset 64 bits, longitud, destino; estado; error); exclusión entre pthreads lectores; el lector duerme con `Atomics.wait`/`memory.atomic.wait32`; el worker lee del handle síncrono (directo al SAB o vía buffer propio y copia) y notifica.
- [x] `port/web/dev/index.html` (+ `--pre-js` si hace falta) -- bootstrap asíncrono en el hilo principal: abre el worker de E/S, espera el handle de OPFS (`dw1/us.pack`), comparte la memoria wasm y recién entonces arranca el Juego; modo de test que envenena `navigator.storage` en los pthreads y registra accesos.
- [x] `port/host/web/main.c`, `port/psyq/crt.c` -- `--pack` (vía buzón), carga de tablas GTE, `--capture-mark/--capture-vblank` con el PPM publicado al hilo principal y señal explícita de fin; SHA del Manifest también en web.
- [x] `port/web/tests/` -- fixture clase B que copia el Pack nativo (`disc_pack`) a OPFS por streaming; test título (hash == captura nativa de `run.py title`) con y sin OPFS envenenado; test clase A de lecturas concurrentes y espera acotada sobre el Pack sintético (programa de test wasm o modo del mismo build).
- [x] `CMakeLists.txt` -- link web con lo anterior; CTest `web_io` (clase A) y `web_scene_title` (clase B, depende de `disc_pack` y de la captura nativa).
- [x] `.memlog.md`, `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-2/` -- decisión del buzón (layout, exclusión, errores), cómo correr los tests, evidencia con hashes.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_io` y `web_boot` pasan y `web_scene_title` se salta.
- Given `DW1_DISC_DIR` y el preset `native-headless` ya corrido, when corre `web_scene_title`, then pasa en Chromium y Firefox con el hash de la captura nativa.
- Given `native-headless` y `native-asan`, when corren sus CTest, then siguen verdes (la costura no cambia el comportamiento nativo).

## Design Notes

El Pack del disco pesa ~350 MB y la memoria wasm es fija de 64 MiB: el worker sirve rangos, nunca el archivo entero. `createSyncAccessHandle` es exclusivo: el fixture cierra su escritor antes de que el worker abra el handle. El buzón conviene en memoria wasm compartida (dirección pasada al worker), porque Emscripten no clona propiedades arbitrarias de `Module` a los pthreads.

## Verification

**Commands:**
- `tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web && ctest --preset web'` -- expected: verde, `web_scene_title` saltado.
- `tools/port/docker_run.sh sh -c 'cd build/port/web && DW1_DISC_DIR=/dw/game_rom/extracted ctest -R web_scene_title'` (tras `native-headless` con disco) -- expected: pasa en ambos navegadores.
- `ctest --preset native-headless && ctest --preset native-asan` -- expected: verdes.

## Implementation Notes

Implementación completada sin cambios en `src/` y sin commits. `pack_open` y
`pack_read` comparten una costura posicional; el validador C conserva magic,
versión, Manifest e índice. El bootstrap espera al worker OPFS antes de main;
la apertura y carga GTE ocurren en el hilo del juego. El buzón de diez u32 usa
mutex, release/acquire y futex/Atomics.wait. La captura publica el PPM y una
señal explícita de fin sin cerrar el runtime.

Verificación dentro del contenedor: web sin disco (16 pasan, 3 saltan),
native-headless (36 pasan, 4 saltan), native-asan (34 pasan, 4 saltan), título
web con sus fixtures (3 pasan; cuatro casos Playwright), y privados ASan
Pack/GTE/título/overlay (4 pasan). Chromium y Firefox completan 96 pedidos
sintéticos con 96 esperas y ningún acceso OPFS desde pthreads envenenados.
Las capturas web coinciden con nativo normal y ASan. Evidencia con hashes en
`docs/implementation-artifacts/evidence/story-4-2/`.


## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|---|
| 1 | blind/vgap | mutex nativo de `read_at` sin prueba concurrente | medium | `pack.c:853-870`: en nativo hay un solo lector (hilo del juego); el lock es especulativo. patch: eliminarlo. |
| 2 | blind/vgap | guard de `navigator.storage` verificado solo en el coordinador; `storageAccesses:0` constante | medium | `title.spec.cjs:16,19`, `io.spec.cjs:14` leen `Module.storageGuardActive` publicado por `main.c:46`; ningún conteo real. patch: contar accesos reales y control positivo por lector. |
| 3 | blind/edge | lectura corta escribe parcialmente el destino antes del error | low | El llamador recibe `-1` y `cdrom` aborta la lectura; nadie consume el buffer parcial. Rechazado. |
| 4 | vgap | lectura corta / excepción de `handle.read()` sin test | low | `io_worker.js:23-26` ya las convierte en error; requiere inyección de fallas en OPFS sin daño demostrado. Rechazado. |
| 5 | blind/edge | espera infinita si el worker muere tras `ready` | low | Sin camino demostrado que termine el worker durante la corrida; el Juego no puede seguir sin E/S de todos modos. Rechazado. |
| 6 | blind/edge | el handle exclusivo queda abierto tras `web_finish` o Pack inválido | low | Instancia única y Shell son la historia 4.4 (Web Lock `dw1-game`). Rechazado. |
| 7 | blind/edge | `--capture-vblank` sin validación; `"null"` si falta | low | Argumento de desarrollo armado por los tests. Rechazado. |
| 8 | edge | `main` falla antes del pthread sin señal de fin | low | Página de desarrollo; el test falla por timeout. Rechazado. |
| 9 | edge | lector del test sin `pack_close` en salida anticipada | low | Código de test; el proceso termina. Rechazado. |
| 10 | edge | `pthread_create` fallido publica fin con lectores vivos | low | Código de test con pool fijo. Rechazado. |
| 11 | edge | `storage.cjs` deja worker/temporizador vivos ante `onerror` | low | Fixture de test; Playwright cierra la página. Rechazado. |
