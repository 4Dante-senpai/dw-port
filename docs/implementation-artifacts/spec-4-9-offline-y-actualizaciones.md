---
title: 'Historia 4.9: Offline y actualizaciones'
type: 'feature'
created: '2026-11-02'
status: 'done'
baseline_commit: '96ea87de50566db27b56f9fedb5f5f6513875530'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-8-memory-cards-en-el-navegador.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Sin red la página no abre, una versión nueva no se aplica de forma controlada y un Pack de un `format_version` anterior deja de servir y obliga a pedir el disco (FR-16, AD-11).

**Approach:** `port/web/sw.js` guarda los archivos del programa en una caché versionada por contenido y los sirve antes que la red. Una versión nueva se instala en segundo plano y se aplica con un reload automático solo en el Launcher sin tocar; si el jugador ya interactuó, queda para la próxima carga. El worker de E/S migra el Pack con `dw1-pack migrate`, el mismo C del build, sin pedir el disco.

## Boundaries & Constraints

**Always:** el service worker solo responde rutas de la lista del programa, y todo lo demás (`/__fixture/*`, `/isolated-off/`, `/seed*`) sale a la red sin pasar por `respondWith`; toda navegación a `/` con query usa la entrada `/`, y una URL con query fuera de la navegación solo responde si está en la caché tal cual; las respuestas guardadas conservan COOP/COEP y la página offline sigue `crossOriginIsolated`; la caché nueva se completa antes de activarse: si falla un archivo, la versión anterior sigue entera; una página nunca mezcla archivos de dos versiones; una versión nueva se activa solo desde el Launcher de quien tiene el Web Lock `dw1-game`, sin Juego arrancado y sin interacción del usuario en esa carga; `#play` no depende del service worker ni de la red; registrar el service worker corre al final de `launch`, después de `busy(false)`; la migración corre bajo el Web Lock, en temporal, y reemplaza con `commitPack`: el Pack anterior queda intacto hasta el final; todo Pack migrado pasa `pack_verify` contra el Manifest antes de reemplazar; un Pack de versión mayor nunca se toca; `flush` sigue en `beforeunload` y `pagehide`; `.gitlab-ci.yml` no cambia; las suites existentes conservan su comportamiento con `serviceWorkers: 'block'`.

**Never:** cambios en `src/`; subir `PACK_FORMAT_VERSION` ni cambiar el formato o los bytes del Pack vigente; pedir el disco para migrar; subir la versión de la base `dw1` sin necesidad; borrar Pack, tarjetas u opciones al actualizar; reload con Juego arrancado o tras interacción; un reload en bucle; dependencias nuevas; workbox o generadores de terceros; ocultar con reintentos un fallo intermitente de Firefox; ampliar un timeout para tapar `#play` deshabilitado.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Primera carga | sin service worker | la caché queda completa; la página toma control sin reload; «Jugar» no descarga de nuevo | falla la instalación: sigue en línea y reintenta en la próxima carga |
| Offline, Pack guardado | servidor caído y recarga | Shell, `crossOriginIsolated`, «Jugar» arranca con audio y Pack | — |
| Offline, sin Pack | servidor caído y recarga | Shell con «Pack ausente» y disco utilizable: `us.tsv` y `dw1_pack.*` en caché | — |
| Versión nueva, sin tocar | deploy y reload | se instala, se activa y recarga una sola vez; `#version` nuevo; una sola caché | — |
| Versión nueva, ya tocó | puntero, tecla o arrastre antes de `installed` | sin reload; `#update-status` avisa; se aplica en la próxima carga | — |
| Versión nueva, jugando | «Jugar» antes de `installed` | la partida sigue con la versión anterior; se aplica en la próxima carga | — |
| Instalación incompleta | un archivo nuevo responde 503 | sigue la versión anterior entera; reintenta en la próxima carga | sin aviso bloqueante |
| Otra pestaña esperando el Lock | la versión se activa | esa pestaña recarga, nunca ejecuta HTML viejo con archivos nuevos | — |
| Mismo contenido | deploy sin cambios | `sw.js` idéntico; sin instalación ni reload | — |
| Tras actualizar | Pack, tarjetas, opciones | iguales byte a byte | — |
| Pack anterior | `format_version` 0 en OPFS | migra con progreso, sin disco y también offline; bytes iguales a un Pack v1; `#play` habilitado | — |
| Migración falla | Pack v0 corrupto o sin espacio | Pack intacto; `#status` con el error; «Pack desactualizado»; `#play` deshabilitado; reintenta al recargar | sin escribir |
| Pack más nuevo | `format_version` mayor | «Pack de una versión más nueva del port»; `#play` deshabilitado; no se escribe | «Borrar datos» sigue disponible |
| Corte en el reemplazo | queda solo `us.pack.previous` | `status` lo restaura y el Pack sigue disponible | — |
| Corte tras el reemplazo | `us.pack` y `us.pack.previous`, o `us.pack.tmp` suelto | `status` borra los sobrantes | ignora `NoModificationAllowedError` |
| Sin service worker | `navigator.serviceWorker` ausente o `register` rechaza | la Shell funciona; `#update-status` avisa que no hay modo sin conexión | no bloquea «Jugar» |

</frozen-after-approval>

## Code Map

- `port/web/dev/index.html:86` `start` -- agregar el listener de `controllerchange` antes de pedir el lock: las pestañas en espera también recargan.
- `port/web/dev/index.html:667` `launch` -- `:729` estado del Pack y migración; `:734` `version.txt` y fixture comparten `try`, offline cae sin romper; `:748` `busy(false)`, punto de registro.
- `port/web/dev/index.html:240` -- `dw1.js?poison=1` no está en la lista: sale a la red.
- `port/web/io_worker.js:6,14` `build`, `status` -- extraer de `build` el módulo, el montaje de entrada y el nodo de salida sobre el handle de `us.pack.tmp`. `:176` `commitPack` y `:205` `released` se reutilizan.
- `port/host/pack/pack.h:32`, `port/host/pack/pack.c:670` `pack_build`, `:767` `pack_verify`, `:611` `header_encode` -- modelo de temporal, `rename` y validación. `pack_open` (`:909`) no cambia.
- `tools/port/pack/dw1_pack.c` -- subcomando nuevo y texto de uso.
- `port/host/pack/tests/test_pack.c:290` -- modelo de prueba del byte de versión.
- `CMakeLists.txt:711-716,743-745,762-769` -- `configure_file`, tests web y fixture `disc_pack`.
- `tools/port/testing/deploy.sh:51,54,56`, `port/web/testing/Dockerfile:3`, `tools/port/testing/test_deploy.py:60` -- listas de archivos. `sw.js` debe entrar en las tres.
- `port/web/testing/nginx.conf` -- no cambia: ya envía COOP/COEP y `no-store`, y `try_files` sirve `sw.js`.
- `port/web/dev/serve.py:21-32` -- `--port` y fixtures; se reutiliza para el sitio de test. `/isolated-off/` no pasa por el service worker.
- `port/web/tests/run.py:18,21,41`, `port/web/tests/playwright.config.cjs:13` -- suites con disco, Packs sintéticos, `us.pack.sha256` y regex de Chromium con OPFS.
- `port/web/tests/disc_helpers.cjs` -- `disc()`, `manifest()`, `entries()`; `launch()` usa `page.route` y no sirve aquí.
- `port/web/tests/disc_hash_worker.js` -- modelo para leer hash y sidecar en OPFS.
- `port/web/tests/cards.spec.cjs` -- modelo del bucle de 20 recargas y del JSON de evidencia.
- `port/web/tests/launcher.spec.cjs:8`, `disc.spec.cjs:89`, `shell.spec.cjs:260`, `cards.spec.cjs:105` -- usan `page.route`, que no ve lo que responde un service worker: motivo de `serviceWorkers: 'block'`.
- `docs/implementation-artifacts/deferred-work.md:158-159` -- `put` de 128 KB en `pagehide` con Chromium: no quitar `beforeunload`.
- `docs/implementation-artifacts/deferred-work.md:161-163` -- `#play` deshabilitado tras recargar en Firefox: esta historia toca ese camino y lo mide.
- `docs/implementation-artifacts/deferred-work.md:165-167` -- `Target crashed` en `web_scene_title_audio`: si reaparece, capturar evidencia.
- `docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md` AD-11, AD-13 -- reglas de migración y persistencia.
- No cambiar: `src/`, `include/`, `.gitlab-ci.yml`, `port/backend/`, base `dw1`, `dwStore`.

## Tasks & Acceptance

**Execution:**
- [x] `port/host/pack/pack.h`, `port/host/pack/pack.c`, `tools/port/pack/dw1_pack.c`, `port/host/pack/tests/test_pack.c` -- agregar `pack_migrate(in, tsv, out, err, errlen)` y `dw1-pack migrate <old.pack> <manifest.tsv> <out.pack>`. Documentar en `pack.h` el formato 0: mismo layout, nunca publicado, definido como primer formato anterior. Copiar con cabecera actualizada a `out.tmp`, correr `pack_verify` y hacer `rename`. Rechazar magic inválida, versión actual o mayor; ante un fallo no deja salida. Probar v0 igual byte a byte a v1, versión mayor, datos corruptos y salida existente intacta.
- [x] `port/web/sw.js`, `CMakeLists.txt` -- crear con `VERSION`, cache `dw1-<VERSION>` y `FILES`: `/`, `/dw1.js`, `/dw1.wasm`, `/io_worker.js`, `/audio_worklet.js`, `/dw1_pack.js`, `/dw1_pack.wasm`, `/us.tsv`. `install`: `addAll` con `cache: 'reload'`, más `/version.txt` solo si responde `ok`. `activate`: borrar otras cachés `dw1-*` y `clients.claim()`. `message` `'activate'`: `skipWaiting()`. `fetch`: decidir de forma síncrona según la matriz y Always. Salida en `build/port/web/sw.js` por `add_custom_command` que dependa de `dw1`, `dw1_pack` y los archivos copiados, en un objetivo `ALL`.
- [x] `tools/port/web/stamp_sw.py` -- crear: reescribe `VERSION` en `<dir>/sw.js` con SHA-256 de cada archivo de `FILES` (`/` es `dw1.html`), `version.txt` si existe y la fuente de `sw.js` sin el valor de `VERSION`. Idempotente.
- [x] `tools/port/testing/deploy.sh`, `port/web/testing/Dockerfile`, `tools/port/testing/test_deploy.py` -- copiar `sw.js`, estamparlo después de escribir `version.txt` y antes de `check_context.py`, y agregarlo al `COPY`; el test verifica el estampado.
- [x] `port/web/io_worker.js` -- `status` devuelve además `stale` y `newer` desde los 12 primeros bytes, con `PACK_FORMAT_VERSION` igual al de `pack.h`, y aplica la recuperación de la matriz antes de leer. Nueva petición `migrate` con el Manifest y progreso, reutilizando lo extraído de `build`, `released` y `commitPack`.
- [x] `port/web/dev/index.html` -- agregar `<p id="update-status" role="status">`. Migración en `launch` según la matriz, con `busy`. Registro al final de `launch`: tolerar `register` sin resultado; aplicar con `postMessage('activate')` un `registration.waiting` existente o una instalación nueva con `registration.active`, salvo interacción (`pointerdown`, `keydown`, `dragenter`); llamar `registration.update()` sin propagar el rechazo. `controllerchange` recarga solo si la página ya tenía controlador al abrir y no arrancó el Juego.
- [x] `port/web/tests/playwright.config.cjs`, `port/web/tests/run.py`, `port/web/tests/storage.cjs` (si hace falta), `CMakeLists.txt` -- `serviceWorkers: 'allow'` solo para suites `offline*`, `'block'` para el resto; `offline` en la regex de Chromium; para esas suites, `run.py` elige un puerto libre y lo pasa como `DW1_WEB_URL` sin levantar servidor propio. Tests `web_offline` (TIMEOUT 600) y `web_offline_real` (`SKIP_RETURN_CODE 77`, `FIXTURES_REQUIRED disc_pack`, TIMEOUT 900).
- [x] `port/web/tests/offline_helpers.cjs`, `port/web/tests/offline.spec.cjs` -- clase A, Chromium y Firefox. El helper arma una copia del sitio bajo `DW1_WEB_BUILD`, una por proyecto, con `us.tsv` del disco sintético y `sw.js` estampado; levanta `serve.py --build <copia> --port <fijo>`, lo detiene de verdad y publica versiones con `stamp_sw.py`. Cubrir toda la matriz. Offline significa `context.setOffline(true)` y servidor detenido; afirmar `crossOriginIsolated`, `#instance`, `#pack-status`, `#status`, `#play`, en ese orden, y arranque por «Jugar» sin `#audio-warning`. Pack anterior: construir un Pack por la Shell, poner `format_version` en 0 en OPFS y comparar bytes tras migrar. Probar instalación con 503, pestaña en espera y paridad de `PACK_FORMAT_VERSION` entre `pack.h` e `io_worker.js`. Bucle de 20 recargas offline con el tiempo hasta `#play` y los estados en JSON de evidencia.
- [x] `port/web/tests/offline_real.spec.cjs`, `port/web/tests/run.py` -- clase B: copia del sitio con `us.pack` y `us.pack.sha256` por enlace duro, sembrar el Pack real, instalar, apagar el servidor, recargar y «Jugar»; afirmar más de 600 VBlanks, frames pintados, audio sin aviso y sin error. Publicar una versión nueva y afirmar el SHA-256 del Pack sin cambios.
- [x] `docs/port/setup.md`, `docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md`, `docs/implementation-artifacts/deferred-work.md`, `docs/implementation-artifacts/evidence/story-4-9/` -- cómo correr las suites y que `sw.js` no debe servirse con caché HTTP larga; AD-11 y AD-13 con service worker, activación y migración; medición nueva de `#play`; evidencia.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_offline`, `testing_deploy` y las existentes pasan en Chromium y Firefox, y las de disco se saltean.
- Given el disco, when corre `web_offline_real` en ambos navegadores, then el Juego arranca offline con el Pack real y conserva su SHA-256 tras actualizar.
- Given `ctest -R pack` en `native-headless`, when corre, then `pack_migrate` pasa y `web_disc_real` sigue dando el SHA-256 canónico del Pack.
- Given el bucle de recargas offline, when se habilita `#play`, then ninguna iteración supera 5 s; si alguna lo hace, el JSON identifica el estado alcanzado.
- Given `tools/port/testing/deploy.sh`, when arma el contexto, then `sw.js` queda estampado, `check_context.py` lo acepta y la imagen lo incluye.

## Implementation Notes

- Codex implementó en dos corridas (90 min en total) y aplicó los parches de la revisión. El contenedor local montó el disco en `/dw/game_rom/extracted`, así que esta vez corrió la clase B.
- Fuera del Code Map: `port/web/dev/serve.py` recibe fallos y puertas de prueba detrás de `DW1_OFFLINE_TEST_SERVER`, y `tools/port/web/test_stamp_sw.py` (test `web_sw_stamp`) cubre el estampado.
- Desvío del Approach: `version.txt` y la fixture pasan a una promesa aparte. `#play` ya no los espera, y `apply` sí, porque con esas lecturas pendientes `skipWaiting()` no terminaba en Chromium (8 de 12 casos). Causa interna sin demostrar.
- Desvío del Approach: `terminate()` ya no libera el handle de OPFS a tiempo en Chromium. El segundo arranque de `web_offline_real` falló al abrir el Pack después de dos recargas seguidas, y despertar la espera no alcanzó. La Shell ahora señala la salida en memoria compartida desde `beforeunload` y `pagehide`, y el worker cierra el handle antes de terminar. Sus esperas pasan a un tope de 100 ms para no perder esa señal; las peticiones siguen despertándolo por `Atomics.notify`.
- Verificación final en remoto, en serie y con disco: web 57/57 y `-R pack` nativo 3/3. Recargas offline: máximos de 86 ms en Chromium y 107 ms en Firefox.
- `mainStarted` separa «ya no se recarga» de `gameStarted`, que sigue impidiendo pedir la activación.

## Spec Change Log

## Review Triage Log

Revisión Codex (`codex-merged`) del diff contra `96ea87d`.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| «Jugar» entre `activate` y `controllerchange` (blind, gap, edge) | medium | patch | `gameStarted` se pone en `boot` e impide el reload: HTML viejo cargaría `dw1.js` nuevo. Contradice «nunca mezcla». Deshabilitar `#play` no sirve: `skipWaiting` se colgó en Chromium y `#play` no puede depender del service worker. |
| Actualización de prueba solo cambia `version.txt` (blind, gap) | medium | patch | `offline_helpers.cjs:20`: los ejecutables son iguales entre versiones; servir código viejo desde la caché nueva pasaría. |
| `/index.html` no abre offline (blind, edge) | low | patch | nginx y `serve.py` la sirven y el service worker no; corrección directa de una condición. `/dw1.html` no se agrega: no es una dirección de la Shell. |
| Pack nuevo con sidecar viejo tras un corte (blind, gap, edge) | low | rechazado | El Pack ya está completo y `commitPack` lo movió entero. El SHA solo se compara en el host de testing; `#play` usa su presencia, y el Juego valida cabecera e índice. Recalcular en `status` agregaría minutos al camino de `#play`. |
| Primera instalación reclama una Shell anterior (blind, edge) | low | rechazado | Exige publicar durante los segundos de la primera visita, y la guarda pediría comparar versiones. Se corrige solo en la próxima carga. |
| `/version.txt` sin plazo (blind, edge) | low | rechazado | Los archivos obligatorios de `addAll` tampoco tienen plazo y el navegador corta `waitUntil`; un servidor colgado no es un caso cotidiano. |
| Instalación sin verificar contenido contra el hash (blind, edge) | low | rechazado | `deploy.sh` reemplaza un solo contenedor: durante el cambio las peticiones fallan y `addAll` aborta. Verificar por archivo agrega una tabla de hashes. |
| `pack_migrate` con entrada igual a `out.tmp` (blind, edge) | low | rechazado | La web usa `/in/us.pack` y `/out/us.pack`; solo se alcanza a mano en la herramienta nativa. |

Auditoría propia (paso 3):

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| «Jugar» clase A no prueba el arranque | medium | patch | `offline.spec.cjs` espera `Module.audioContext`, asignado antes de bajar `dw1.js`. |
| Fila «Primera carga» sin prueba de no redescarga | medium | patch | Ninguna prueba arranca sin servidor tras la primera carga. |
| Fila «Pack anterior» sin aserto de progreso | low | patch | Falta observar `#progress` durante la migración. |
| `ownsLock` nunca es falso en `apply` | low | patch | `registerOffline` solo corre desde `launch`, que ya tiene el Lock. |

## Design Notes

**Sin pasos manuales en el caso común.** FR-16 pide aplicar la versión nueva al recargar. Un service worker clásico necesita dos recargas: la primera instala y la segunda activa. Esperar la instalación antes de habilitar `#play` lo haría depender de la red. Por eso `#play` no espera nada, y el reload automático ocurre solo si la página sigue intacta. La interacción protege una construcción de Pack, una importación de tarjeta o un remapeo en curso.

**Sin mezcla de versiones.** La página pide archivos después de cargarse (`dw1.js` al «Jugar», workers, worklet). Activar otra versión con el Juego arrancado o en otra pestaña que espera el Lock mezclaría HTML viejo con archivos nuevos. El Web Lock garantiza que quien activa es el único Launcher en uso; `controllerchange` recarga a los demás.

**`VERSION` por contenido.** El navegador solo detecta una versión si cambian los bytes de `sw.js`. Con un hash, dos builds iguales no fuerzan una descarga, y un deploy solo-commit cambia `version.txt` y así mantiene `#version` veraz.

**Formato 0.** Hoy no hay ningún formato anterior publicado y AD-11 exige tener lista la migración. Se define el 0 con el mismo layout, igual que `MIGRATIONS[1]` en `PortSettings` (nunca publicado). Subir a 2 sin cambio real alteraría el SHA-256 canónico de todos los Packs. Una migración futura agrega un caso sin cambiar el recorrido.

**`serviceWorkers: 'block'` fuera de `offline*`.** Según la documentación de Playwright, `page.route` no intercepta lo que responde un service worker, y varias suites dependen de eso. Bloquearlo conserva sus condiciones. El camino real queda cubierto por `web_offline` y `web_offline_real`. Hipótesis por verificar: `block` en Firefox deja `register` sin resultado o `navigator.serviceWorker` ausente; la Shell tolera ambos casos.

**Offline real.** `setOffline` puede no alcanzar al service worker en todos los navegadores, y un archivo fuera de la lista saldría por red sin fallar. Detener el servidor lo descubre. Por eso el sitio de test es una copia propia con puerto fijo: el origen no cambia al «publicar», y no se pisa el directorio de build con `ctest -j`.

## Verification

**Commands:**
- `cmake --build --preset web && ctest --preset web --output-on-failure` -- expected: verde; clase B saltada sin disco.
- `ctest --preset native-headless --output-on-failure -R pack` -- expected: verde, con `pack_migrate`.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/remote_run.sh ctest --preset web -R 'web_offline_real|web_disc_real|web_cards_real|web_scene_title' --output-on-failure` -- expected: verde, sin reintentos.
- `git diff --stat cfd2197 -- src include .gitlab-ci.yml` -- expected: vacío.
