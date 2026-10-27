---
title: 'Historia 4.3: Selección del disco, validación y Pack en el navegador'
type: 'feature'
created: '2026-10-27'
status: 'done'
baseline_commit: '6be3021210e5f811c38e8e86f946d7f0f5b73519'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-2-worker-de-e-s-y-pantalla-de-titulo-en-el-navegador.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** En el navegador el Pack solo llega a OPFS copiándolo desde un fixture del servidor (`/__fixture/us.pack`); la jugadora no puede usar su propio `.bin`/`.cue`, que es el recorrido principal del producto (UJ-1, FR-1..FR-3).

**Approach:** La Shell acepta el `.bin` (con o sin `.cue`) por selector o arrastre y se lo pasa al worker de E/S, que corre el mismo C de `port/host/pack` (la CLI `dw1-pack` compilada a wasm) leyendo los archivos por partes y escribiendo el Pack directo a OPFS; la Shell muestra progreso, errores con el nombre del problema, "Listo para jugar", pide almacenamiento persistente y permite "Borrar datos".

## Boundaries & Constraints

**Always:** un solo C para Manifest/validación/Pack (`pack_build` y sus mensajes), sin reimplementar la validación en JS; el worker de E/S es el único que toca los handles síncronos de OPFS; lectura de la Imagen por partes (nunca el `.bin` entero en memoria JS ni wasm) y escritura del Pack en streaming; el Pack queda "disponible" solo tras un build completo (escribir `us.pack.tmp`, mover a `us.pack` y recién después escribir el sidecar `us.pack.sha256` con el SHA-256 del Pack); una falla borra `us.pack.tmp` y deja intacto un Pack anterior válido (misma semántica que `pack_build` nativo); el Manifest que usa la Shell es `tools/port/manifest/us.tsv` servido junto al build; tests clase A sin bytes del disco; clase B salta con 77 sin `DW1_DISC_DIR`; ningún byte del disco en el repo ni en la evidencia (solo hashes y tiempos).

**Never:** pantallas finales de la Shell, Web Lock y detección de capacidades (4.4); input, audio, memory cards (4.5+); service worker y migraciones (4.9); cambiar el formato del Pack o `pack.h`; ASYNCIFY/JSPI; quitar el cargador de fixture de la Historia 8.5 (sigue para el host de testing, visible solo si `/__fixture/us.pack.sha256` responde).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| `.bin` solo | disco sintético `MODE2/2352`, Manifest sintético | progreso visible, "Listo para jugar", "Jugar" habilitado | — |
| `.bin` + `.cue` (selector y arrastre) | mismo disco con su `.cue` | idem | — |
| `.cue` de otro modo | `TRACK 01 MODE1/2352` | rechazo; el mensaje nombra `MODE2/2352` | sin Pack ni `.tmp` en OPFS |
| `.iso` 2048 B/sector | imagen ISO | rechazo que explica la falta de sectores XA/video | idem |
| Un byte cambiado | byte de `\SYSTEM.CNF;1` alterado | falla nombrando ese archivo | idem |
| Exe de JP/PAL | bytes del exe sintético alterados | rechazo que nombra SLUS-01032 | idem |
| Selección inválida | sin `.bin`, o `.cue` que nombra un archivo no elegido | mensaje que lo dice | idem |
| Recarga | Pack + sidecar en OPFS | "Jugar" sin pedir el disco | — |
| Persistencia denegada | `navigator.storage.persist()` → `false` | aviso visible, el Pack igual queda | — |
| Borrar datos | Pack presente | OPFS `dw1/` vacío, vuelve a pedir el disco | — |

</frozen-after-approval>

## Code Map

- `port/host/pack/pack.c:222-260,670-765` -- `image_open` (`.cue`→bin relativo al `.cue`, rechazo ISO y tamaño), `check_exe` (mensaje SLUS-01032), `pack_build` (`fopen(tmp,"wb")` secuencial, `rename`, `remove` en falla). No cambiar.
- `tools/port/pack/dw1_pack.c` -- CLI `manifest|build|verify`: compilarla también a wasm como módulo del worker (`callMain`, error por `printErr`, código de salida); agregar solo lo mínimo si hace falta (p. ej. `sha256 <file>` para el sidecar).
- `CMakeLists.txt:146-148,496-500,676-716` -- flags web globales (`-pthread`), `dw1_pack_lib` (en web incluye `io_mailbox.c`), copia de `io_worker.js`/`dw1.html`, tests `web_*`.
- `port/web/io_worker.js` -- hoy solo sirve sectores del Pack al juego; agregar el modo build (importa el módulo del Pack, monta los `File` elegidos con WORKERFS, salida a un nodo FS cuyo `write(…, position)` va al `FileSystemSyncAccessHandle` de `dw1/us.pack.tmp`, progreso por bytes leídos de la Imagen).
- `port/web/dev/index.html:52-105` -- `launch()`: estado del Pack, "Jugar" (`/?pack=1&frames=300`), cargador de fixture. Agregar selector/arrastre, progreso, mensajes, "Listo para jugar", `persist()`, "Borrar datos".
- `port/web/tests/seed_worker.js` -- precedente del protocolo de pasos/progreso y del sidecar.
- `port/host/pack/tests/test_pack.c:84-125,340-350` -- disco sintético de 32 sectores (`make_disc`), `.cue` y `.iso`: portar al test JS para la clase A.
- `port/web/tests/{run.py,storage.cjs,launcher.spec.cjs,title.spec.cjs,playwright.config.cjs}` -- runner, `errors()`, fixtures; `title.spec` hoy carga el Pack con el botón del fixture.
- `tools/port/pack/test_pack_disc.sh:26` -- SHA-256 del Pack nativo del disco real (`c01e5c36…0cf6`).
- `port/web/testing/{Dockerfile,nginx.conf}`, `tools/port/testing/test_deploy.py:60` -- lista de archivos desplegados: sumar módulo del Pack y `us.tsv`.

## Tasks & Acceptance

**Execution:**
- [x] `CMakeLists.txt` -- target web `dw1_pack` (`dw1_pack.c` + `dw1_pack_lib`, `-sENVIRONMENT=worker`, `-lworkerfs.js`, `FS`/`callMain` exportados, sin pool de pthreads) y copia de `us.tsv` al directorio del build -- mismo C que la herramienta nativa (AD-11).
- [x] `port/web/io_worker.js` -- mensaje `build {files, manifest}` → monta, corre `build /in/<.cue|.bin> /manifest/us.tsv /out/us.pack`, publica `{progress,total}`, `{error}` con el mensaje del C o `{ready, sha256}`; commit tmp→`us.pack`→sidecar; limpieza en falla; mensaje `clear` que borra `dw1/` -- el worker de E/S es el dueño de OPFS (AD-13).
- [x] `port/web/dev/index.html` -- selector `<input type=file multiple accept=.bin,.cue>` y zona de arrastre, elección de la Imagen (el `.cue` si está, si no el único `.bin`), barra de progreso, error visible, "Listo para jugar", `persist()` con aviso si se deniega, "Borrar datos"; estado al cargar: Pack disponible si existen `us.pack` y sidecar -- recorrido UJ-1.
- [x] `port/web/tests/disc.spec.cjs` + `run.py` -- suite `disc` clase A: disco sintético generado en el test, Manifest sintético producido por el mismo módulo (`dw1-pack manifest`) y servido en lugar de `/us.tsv` con `page.route`; todos los casos de la matriz en Chromium y Firefox, con OPFS revisado tras cada falla; memoria máxima por `performance.measureUserAgentSpecificMemory()` durante la validación ≤ 512 MB (Chromium).
- [x] `port/web/tests/disc_real.spec.cjs` + `run.py` -- suite `disc_real` clase B: elige el `.bin`/`.cue` de `DW1_DISC_DIR`, SHA del sidecar y del archivo en OPFS == SHA del Pack nativo, memoria ≤ 512 MB, tiempo hasta "Listo para jugar" (SM-4) escrito a JSON, recarga ofrece "Jugar", "Borrar datos" lo elimina.
- [x] `CMakeLists.txt`, `.gitlab-ci.yml` -- CTest `web_disc` (clase A) y `web_disc_real` (clase B, `SKIP_RETURN_CODE 77`, timeout amplio); sumar `web_disc_real` al job `testing`.
- [x] `port/web/testing/Dockerfile`, `tools/port/testing/test_deploy.py`, `port/web/tests/launcher.spec.cjs` -- desplegar el módulo y `us.tsv`; ajustar el test del launcher solo si cambian textos o selectores.
- [x] `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-3/` -- cómo correr las suites; evidencia con hashes, memoria máxima y SM-4.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_disc` pasa en Chromium y Firefox y `web_disc_real` se salta (77); `web_boot`, `web_io`, `web_launcher` siguen verdes.
- Given `DW1_DISC_DIR`, when corre `web_disc_real`, then el SHA-256 del Pack web es `c01e5c3680ffdf50d0342f16ec978c6d107a33529688dae6a817bd63947f0cf6` en ambos navegadores y la evidencia registra SM-4 y la memoria máxima.
- Given `native-headless` y `native-asan`, when corren sus CTest, then siguen verdes (el C del Pack no cambió de comportamiento).

## Design Notes

Reusar la CLI evita una API nueva: el worker llama `callMain(['build', img, tsv, out])` y muestra el `err` que imprime. `pack_build` escribe en orden, así que un nodo de dispositivo de Emscripten con `write(stream, buf, off, len, pos)` → `handle.write(buf.subarray(off, off+len), {at: pos})` alcanza; verificar cómo trata `fopen("wb")` (O_TRUNC) a un chrdev y que `rename`/`remove` del FS en memoria solo muevan el nodo. El `move()` real en OPFS lo hace el JS después de que `callMain` devuelve 0. Si cargar el módulo con `-pthread` dentro del worker falla, compilar sus fuentes sin pthreads es aceptable.

## Verification

**Commands:**
- `tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web && ctest --preset web'` -- expected: verde, clase B saltada.
- `tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && cmake --build --preset web && cd build/port/web && DW1_DISC_DIR=/dw/game_rom/extracted ctest -R "pack_disc|web_disc_real"'` -- expected: pasa en ambos navegadores con el SHA del Pack nativo.
- `ctest --preset native-headless && ctest --preset native-asan` -- expected: verdes.

## Implementation Notes

Implementado por Codex sin cambios en `pack.c`, `pack.h` ni `src/`. El módulo `dw1_pack` es la CLI
`dw1-pack` compilada a wasm (memoria fija de 64 MiB, sin pool de pthreads); se le agregó solo
`sha256 <file>`. El worker monta los `File` con WORKERFS (con caché de 64 KiB por lectura) y
redirige el nodo `/out/us.pack.tmp` del FS en memoria al handle síncrono de OPFS. El commit mueve
el Pack anterior a `us.pack.previous`, publica `us.pack` y luego el sidecar, y restaura el anterior
si algo falla. Evidencia: SHA web == nativo en Chromium y Firefox, memoria máxima de 67 MiB
(Chromium), SM-4 de 81 s (Chromium) y 36 s (Firefox) en el contenedor limitado a 2,5 CPU.
Firefox no expone `measureUserAgentSpecificMemory`: su memoria queda `null`.

Implementación en `port/web/`, CLI, CMake y herramientas de testing; sin cambios
en `pack.c`, `pack.h` ni `src/`. El módulo wasm comparte `dw1_pack_lib` y añade
solo el comando `sha256 <file>` a la CLI. WORKERFS usa una caché de lectura de
64 KiB por archivo; un nodo FS escribe directamente al temporal OPFS, sin
almacenar el Pack en MEMFS. El worker calcula el SHA por partes, publica el Pack
y después el sidecar. Conserva temporalmente el Pack previo para restaurarlo
si falla la publicación del sidecar. El selector y el arrastre comparten ese
recorrido; la solicitud de persistencia no bloquea «Jugar».

La suite sintética final cubre 26 casos en ambos navegadores, incluidos errores
inyectados al escribir el Pack y el sidecar. El Manifest sintético lo genera el
mismo módulo C. El caso de memoria mantiene vivo el worker antes del constructor
para evitar una muestra que solo mida la Shell. La suite real compara el SHA
nativo, el sidecar y una lectura independiente del archivo comprometido, y
registra SM-4. Firefox registra memoria nula porque no expone la API solicitada.

Verificación final: web sin disco, 38 pruebas pasan y 4 se saltan; suites
nativas sin disco con fixtures aislados, 58/56 pasan y 25 se saltan en cada
preset; CTest `web_disc`, `pack_disc` y `web_disc_real` con disco, 3/3 pasan.
Chromium: máximo medido 69.853.927 bytes, SM-4 81.228 ms; Firefox: SM-4 36.194 ms.
En ambos navegadores, archivo y sidecar coinciden con el SHA nativo requerido.

Evidencia y comandos en `evidence/story-4-3/`; ver allí también la explicación de
los fixtures nativos privados obsoletos encontrados en este contenedor y su
aislamiento temporal para las suites completas sin disco. Los Packs almacenados
se restauraron al terminar esas suites. No se hicieron commits ni capturas de
pantalla.

## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|---|
| 1 | blind/edge | cierre de pestaña entre los `move` del commit deja `us.pack.previous` o Pack nuevo con sidecar viejo | low | `io_worker.js` `commitPack`: el Pack es determinista para un Manifest, así que sidecar viejo y Pack nuevo coinciden; sin `us.pack` la Shell pide el disco otra vez. Recuperación agrega lógica para un caso raro. Rechazado. |
| 2 | blind/edge | dos pestañas construyen o borran a la vez sobre los mismos nombres OPFS | medium | Real; el Web Lock `dw1-game` es de la historia 4.4 (excluido por la intención). defer: el lock de 4.4 debe cubrir también build y borrado. |
| 3 | blind/edge | al recargar no se verifica el SHA del Pack contra el sidecar | low | Rehashear 350 MB en cada carga; `pack_open` del juego ya valida cabecera, Manifest e índice. Rechazado. |
| 4 | blind/edge | falla de limpieza de `us.pack.previous` tras un commit correcto se informa como error | low | `removeEntry` de un archivo propio recién movido; sin camino demostrado. Rechazado. |
| 5 | blind | progreso al 100 % y quieto durante el SHA-256 final | low | Lo ve toda jugadora en cada build (el SHA relee ~350 MB). patch: contar también la relectura del SHA en el progreso. |
| 6 | blind | sin cancelar una construcción larga | low | Funcionalidad no pedida. Rechazado. |
| 7 | blind/vgap | rollback del commit sin prueba que distinga Pack anterior de reemplazo, ni fallas de `move` | medium | `disc.spec.cjs:74-102` usa el mismo disco dos veces y solo inyecta fallas en `write`. patch: segundo disco válido distinto y fallas en `tmp.move` y en el `move` del sidecar, con recarga. |
| 8 | vgap | ninguna prueba fija que «Jugar» no espere a `persist()` | low | Los tests resuelven `persist` al instante. patch: promesa pendiente y rechazo. |
| 9 | edge | selección con miles de archivos extra | low | Uso improbable; el directorio real trae `readme.html` y se ignora sin daño. Rechazado. |
| 10 | edge | aviso de persistencia tardío tras «Borrar datos» | low | Carrera improbable y cosmética. Rechazado. |
| 11 | edge | worker colgado deja la Shell deshabilitada | maybe-false | Sin camino demostrado de cuelgue del módulo; requeriría inyectar fallas de carga. Rechazado (sería low). |
| 12 | edge | `measureUserAgentSpecificMemory` rechaza durante la pausa y el test vence | false | El test falla por timeout, que es la falla correcta en ese caso. |
| 13 | edge | `TIMEOUT 1800` de `web_disc_real` menor que dos navegadores secuenciales con 1500 s cada uno | low | Corrección directa. patch: subir a 3300. |
| 14 | edge | memoria no medida en Firefox | false | El criterio de la épica la mide con `measureUserAgentSpecificMemory` en Chromium; Firefox no expone la API. |
