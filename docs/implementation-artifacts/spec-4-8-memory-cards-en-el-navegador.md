---
title: 'Historia 4.8: Memory cards en el navegador'
type: 'feature'
created: '2026-11-01'
status: 'done'
baseline_commit: '723af089d587f1a7933d95cb241f624c89f54a21'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-7-audio-en-el-navegador.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** En web la Memory card está ausente (`card_image_attach` la anula bajo `__EMSCRIPTEN__`): el jugador no puede guardar ni continuar, y la pantalla «Memory cards» es un texto de relleno (FR-10, FR-11, UJ-2, UJ-3).

**Approach:** La Shell lee de IndexedDB una entrada de 131.072 bytes por ranura y la entrega al Juego antes de `callMain`. Al terminar cada comando de escritura de `libmcrd`, el hilo del juego publica una instantánea en memoria compartida y avisa al hilo principal, que la guarda en una sola transacción `durability: 'strict'`; `pagehide` fuerza el envío de la última instantánea sin persistir. La pantalla «Memory cards» muestra dos ranuras con importar y exportar `.mcd`.

## Boundaries & Constraints

**Always:** el hilo del juego nunca toca IndexedDB y nunca espera su resultado; el tiempo virtual no depende de la persistencia; cada guardado es un único `put` del `.mcd` crudo completo, sin trocear; se persiste solo una instantánea consistente, nunca la imagen viva a mitad de comando; con varias escrituras pendientes por ranura se conserva el orden y puede omitirse una intermedia, pero nunca la última; una ranura sin entrada arranca con la tarjeta formateada de `card_image_new` y no escribe nada hasta que el Juego guarde; una entrada que no se puede leer o que no mide 131.072 bytes nunca se sobrescribe sola: la ranura queda ausente con aviso (regla de 4.6); importar sobre una ranura con datos pide confirmación; el tamaño se valida antes de escribir y el rechazo deja la ranura intacta; «Borrar datos» sigue borrando solo el Pack (FR de borrado) y no toca las tarjetas; la habilitación de `#play` no depende de IndexedDB de tarjetas; los arranques por query sin `cards=1` conservan tarjetas ausentes y sus referencias de título y audio no cambian; `.gitlab-ci.yml` no cambia.

**Never:** cambios en `src/` o en el build PS1; `.mcr`/`.mcd` con cabecera VGS o tamaños distintos de 131.072 bytes en web; gestionar saves individuales dentro de una tarjeta; importar o exportar durante la partida (la Shell de tarjetas solo existe antes de «Jugar»); service worker o migraciones de versión del port (4.9); dependencias nuevas; portar el motor de Escenas a wasm; ocultar con reintentos un fallo intermitente de Firefox.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Primera vez | sin entradas | dos tarjetas formateadas; Shell: «Vacía», exportar deshabilitado | — |
| Guardar y volver | escritura, cerrar, reabrir | entrada igual byte a byte a la última instantánea; Shell: «Con datos» | — |
| Base existente | `dw1` v1 con `settings` | sube a v2, conserva `port_settings`, crea `cards` | aviso si falla; no arranca tarjetas |
| Base más nueva | `dw1` con versión mayor | ranuras ausentes con aviso; opciones por defecto (regla de 4.6) | no escribe |
| Corte en la transacción | la página muere o la transacción aborta tras el `put` | la entrada es exactamente la anterior o la nueva | — |
| Pagehide con pendiente | aviso al hilo principal aún sin atender | la última instantánea queda guardada | si la instantánea está a mitad de copia, reintento acotado; persiste la anterior |
| Falla la escritura | `QuotaExceededError` | aviso visible; la partida sigue; reintenta en la próxima escritura | `#card-warning` |
| Importar válido | `.mcd`/`.mcr` de 131.072 bytes | ranura actualizada; la próxima partida lo usa | — |
| Importar inválido | 0, 131.071, 131.073 o 131.136 bytes | mensaje con el tamaño; ranura intacta | sin escritura |
| Importar sobre datos | el usuario cancela la confirmación | ranura intacta | — |
| Entrada corrupta | tamaño o tipo inválido en `cards` | ranura ausente en el Juego, Shell «Ilegible»; importar puede reemplazarla con confirmación | no escribe sola |
| Persist denegado | `persist()` resuelve `false` | el aviso sugiere exportar la Memory card, también al volver con Pack | no bloquea «Jugar» |

</frozen-after-approval>

## Code Map

- `port/psyq/card_image.c:39-46` -- quitar la rama `__EMSCRIPTEN__` de `card_image_attach`. `CardCommit`, `card_image_new`, `card_image_data` y `card_image_commit` se reutilizan tal cual.
- `port/psyq/libmcrd.c:307` -- único llamador de `card_image_commit` (comandos 4 y 6). No cambia.
- `port/host/card.c` -- modelo nativo, no se comparte: `commit` escribe a disco; acepta VGS, web no.
- `port/host/web/main.c` -- agregar `--card-test N`. `web_prepare` corre en el hilo del juego. Un `Pack` no es necesario para esa prueba.
- `port/host/web/input.c:plat_input_vblank` -- hoy sale con captura activa antes de mirar `replay`; las escenas de tarjeta necesitan replay con captura. Sin `replay`, el título no cambia.
- `port/psyq/crt.c:89,176,219` -- `web_finish` por `vblank_limit`, fin de captura y `web_prepare`.
- `port/web/dev/index.html` -- `settingsStore` (`open('dw1', 1)`, deferred de 4.6), `boot`, `onRuntimeInitialized` (punto de carga, antes de `callMain`), listener de `pagehide`, `launch`, `select` (`persist()`), `#cards`, `#audio-warning` como modelo de aviso fuera del launcher, `showFrames` como modelo de seqlock.
- `port/web/dev/serve.py:21` -- lista blanca de `/__fixture/`; agregar los fixtures de tarjeta bajo `DW1_CARD_DIR_WEB`.
- `port/web/tests/storage.cjs:store` -- abre `dw1` con versión 1: pasa a abrir sin versión, para no fallar con `VersionError` contra v2.
- `port/web/tests/run.py:18,38` -- listas de suites con disco. `playwright.config.cjs` elige `<suite>.spec.cjs`.
- `port/web/tests/title_audio.spec.cjs`, `tools/port/scenes/pcm.py` -- modelos para el spec y el fixture nativo (`--resolved-inputs`, `final absolute VBlank`, salida fuera del repo).
- `tools/port/scenes/card_newgame.toml`, `card_continue.toml` -- inputs y `capture_vblank = 12000`.
- `tools/port/scenes/run.py:116-170,560` -- `final_card`, `prepare_cards`, `card_source`, `valid_card_export`; `tools/port/reference/capture.py:249,286` -- `--card1`, `--card-build`, `--reuse`.
- `port/web/tests/disc_real.spec.cjs:49-53` -- recarga donde falló `#play` en el pipeline 141.
- `port/web/tests/disc.spec.cjs:37,151-155` -- asertos de `#storage-warning`; conservar «denegado» en el texto.
- `tools/port/testing/deploy.sh:51` -- no cambia: no se agregan archivos servidos.
- `docs/implementation-artifacts/deferred-work.md:154` -- entrada de la 4.6 sobre la versión de `dw1`, se resuelve acá.
- No cambiar: `src/`, `include/`, `.gitlab-ci.yml`, `port/backend/`, `port/host/card.c`, SDL.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/card_image.c` -- quitar la rama web de `card_image_attach`.
- [x] `port/host/web/card.c`, `CMakeLists.txt` -- crear: dos ranuras con `_Atomic uint32_t seq` e imagen de instantánea; `web_card_address(port)`; `web_card_attach(port, blank)`, llamable desde el hilo principal antes de `callMain`, que usa `card_image_new` si `blank` y `card_image_attach` con un `CardCommit` que hace seqlock (impar, `memcpy`, par) y `MAIN_THREAD_ASYNC_EM_ASM` hacia `Module.cards.committed(port)`; `web_card_test(n)` que, en el hilo del juego, llena la ranura 1 con un patrón por iteración, llama `card_image_commit`, espera 5 ms con futex y termina con `web_finish(0)`. Exportar las funciones y sumar el fuente al objetivo web.
- [x] `port/host/web/main.c`, `port/host/web/input.c` -- `--card-test N`; aplicar el replay también con captura activa.
- [x] `port/web/dev/index.html` -- un solo `dwStore(name, mode, action, options)` con `open('dw1', 2)` y `onupgradeneeded` por `oldVersion`; `settings` lo usa sin cambiar comportamiento. Almacén `cards`, clave `0|1`, valor `ArrayBuffer` de 131.072 bytes, escrito con `{durability: 'strict'}`. `Module.cards` con `committed`, `flush` (en `pagehide`, antes de terminar el worker) y `stats()` (`seq`, `persisted` y `failures` por ranura); `?cards=1` o «Jugar» cargan y adjuntan antes de `callMain`; `?replay=<nombre>` pide ese fixture (`1` sigue siendo `replay.txt`); `?card-test=N`; `?card-hold=1` ignora los avisos para que solo `pagehide` persista (tests). `<p id="card-warning" role="status">` fuera del launcher.
- [x] `port/web/dev/index.html` -- `#cards`: dos filas `#card-slot-1|2` con estado («Vacía», «Con datos», «Ilegible»), `input type=file accept=.mcd,.mcr` y botón Exportar (`Blob` + `<a download>`, `digimon-world-card-<n>.mcd`); mensaje `#card-status`; `confirm()` antes de reemplazar datos. Unificar `persist()` en una función llamada tras construir el Pack, al volver con Pack y al importar, con el aviso «denegado» más la sugerencia de exportar.
- [x] `tools/port/scenes/cards.py`, `CMakeLists.txt` -- crear, siguiendo `pcm.py` y reutilizando `run.py`: corre `native-headless` de `card_newgame` (tarjetas nuevas) y `card_continue` (con el `.mcd` resultante), más un control de `card_continue` con tarjeta nueva que debe dar otra imagen; deja en un directorio fuera del repo `.mcd`, `<escena>.replay.txt`, SHA-256 de la captura y `fixture.json`. Test `native_cards`, `SKIP_RETURN_CODE 77`, fixture `native_cards`.
- [x] `port/web/tests/cards.spec.cjs`, `port/web/tests/storage.cjs`, `port/web/tests/run.py`, `port/web/tests/playwright.config.cjs` (si hace falta), `CMakeLists.txt` -- clase A `web_cards`, Chromium y Firefox: toda la matriz con `--card-test`; `durability === 'strict'` observado envolviendo `IDBDatabase.prototype.transaction`; corte por `tx.abort()` y por `page.close()` al ejecutarse el `put`, cada uno repetido al menos 5 veces, con la entrada igual byte a byte a la anterior o a la nueva; `pagehide` con `?card-hold=1`, repetido 5 veces; descarga exportada igual a la entrada; bucle de 20 recargas con Pack sintético que mide el tiempo hasta `#play` habilitado y registra `#instance`, `#pack-status` y `#status`, en un JSON de evidencia.
- [x] `port/web/tests/cards_real.spec.cjs`, `port/web/tests/run.py`, `port/web/dev/serve.py`, `CMakeLists.txt` -- clase B `web_cards_real`, `FIXTURES_REQUIRED "disc_pack;native_cards"`: replay de `card_newgame` con captura, entrada igual byte a byte al `.mcd` nativo y frame igual al nativo; cerrar la página, reabrir, Shell «Con datos», replay de `card_continue` y frame igual al nativo; en otro contexto, importar el `.mcd` nativo por la Shell, exportar y comparar bytes, y replay de `card_continue`; en cada recarga, afirmar `#instance`, `#pack-status`, `#status` y `#play`, en ese orden.
- [x] `port/web/tests/cards_redux.spec.cjs` o variante por variable de entorno, `port/web/tests/run.py` -- suite manual `cards_redux` (no entra en CTest, como `long`): `capture.py card_newgame --state --card-build native-headless --reuse`, importar ese `.mcd` de Redux y continuar en web con frame igual al nativo; luego exportar la ranura 1 y correr `capture.py card_continue --state --card1 <exportado>`, con estado tras `loadSavegame` igual al de la referencia con la tarjeta nativa. Sin copiar la salida de una corrida sobre otra.
- [x] `port/web/tests/disc_real.spec.cjs` -- antes de `#pack-status`, tras cada recarga, afirmar `#instance` oculto: distingue espera del Web Lock de otras causas si reaparece el fallo.
- [x] `docs/port/setup.md`, `docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md`, `docs/implementation-artifacts/deferred-work.md`, `docs/implementation-artifacts/evidence/story-4-8/` -- cómo correr las suites; aclarar en AD-13 que `schema_version` es de `PortSettings` y que las tarjetas son `.mcd` crudo versionado por la base `dw1`; resolver la entrada de la 4.6; evidencia.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_cards` y las existentes pasan en Chromium y Firefox, y las de disco se saltean.
- Given el disco, when corre `web_cards_real` en ambos navegadores, then la entrada guardada es idéntica al `.mcd` nativo, y «Continue» tras cerrar la página y tras importar da el frame nativo.
- Given PCSX-Redux, when corre la suite `cards_redux` en ambos navegadores, then el Juego carga el `.mcd` de Redux y Redux carga el `.mcd` exportado con el estado esperado tras `loadSavegame`.
- Given el bucle de recargas, when se habilita `#play`, then ninguna iteración supera 5 s; si alguna lo hace, el JSON identifica el estado alcanzado.
- Given `web_scene_title` y `web_scene_title_audio`, when corren, then siguen idénticas a nativo.

## Implementation Notes

- Codex implementó en una corrida de 50 min; verificación con disco en el PC remoto. La imagen local no trae PulseAudio: se corrió con una copia de la remota, `dw-port:remote`.
- Desvío del Approach: el envío se fuerza en `beforeunload` y en `pagehide`. Con solo `pagehide`, Chromium no persistió el `put` de 128 KB (test de salida, hash nulo) y Firefox sí. Causa sin verificar. Un cierre sin `beforeunload` puede perder la última escritura no atendida en Chromium: queda el estado anterior, válido.
- `tx.commit()` explícito y conexión `dw1` abierta: sin ellos el `put` del cierre no llegaba a la base.
- `web_cards_real` compara 600 VBlanks después de `loadSavegame`, con `cards.py` reproduciendo los inputs resueltos más allá de los checkpoints. El frame del menú no probaba la carga.
- `cards_redux` exporta una tarjeta guardada en web, no la importada de Redux.
- Fuera del Code Map: `port/psyq/scene.c` (marca `MOV_playMovie` en web) y `port/psyq/tests/test_card.c` (sin ramas web: ahora corre el test real).

## Spec Change Log

## Review Triage Log

Revisión Codex (`codex-merged`) del diff contra `723af08`.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| Segunda ranura sin recorrido C → IndexedDB (gap) | medium | patch | `web_card_test` solo publica la ranura 1; un `commit` que siempre avise `0` pasaría todas las pruebas. |
| «Jugar» real sin prueba de adjunto (gap) | medium | patch | `cards.spec.cjs:97` llama `attachCards` directo y el resto usa `cards=1`: quitar `!location.search` no rompería nada. Es el camino del jugador. |
| Cinco salidas con el mismo patrón (gap, edge) | medium | patch | `cards.spec.cjs:202`: tras la primera salida, las otras cuatro pasarían sin flush. |
| `cardWarning` sugiere exportar desde la partida (blind, edge) | medium | patch | El exportador está en el launcher oculto y usa bytes previos a la partida. Exportar en partida es un Never del spec: se quita la sugerencia. |
| `card-hold` no aísla `pagehide` (edge, claim) | medium | rechazado | Es cierto y la corrección sería editar este spec. En Chromium `pagehide` solo no persiste un `put` de 128 KB (medido); en Firefox sí. Queda en Implementation Notes. |
| `snapshot()` abandona tras 4 intentos (blind, edge) | low | rechazado | La matriz lo pide: reintento acotado y persiste la anterior. El estado queda válido. |
| Sin flush en `visibilitychange` (blind) | low | rechazado | `committed` persiste en cuanto llega el aviso, también oculto; solo perdería un aviso sin atender en el instante de matar el proceso, y queda el estado anterior. |
| `attachCards` sin plazo ante lectura estancada (blind, edge) | low | rechazado | Sin caso alcanzable mostrado; el plazo agrega rama y recuperación. |
| Desborde de `seq` uint32 (blind, edge) | false | rechazado | Requiere 2^31 comandos de escritura. |
| `cards.py` no verifica `native-headless` (blind) | false | rechazado | Mismo contrato que `pcm.py`: sin binario falla de forma visible, y CI lo construye antes. |
| Seqlock sin copia previa ni escritor que termina (blind) | low | rechazado | El caso sin copia no persiste nada y deja la entrada anterior, lo que la matriz admite. |
| `cards_redux.py` fija `build/port/web` (edge) | low | rechazado | Suite manual, siempre con ese directorio. |

## Design Notes

**Instantánea con seqlock, no copia en cola.** Copiar a `malloc` por cada commit y encolar haría que `pagehide` no vea el último commit si el aviso sigue sin atender. Con la instantánea, `flush` lee siempre el último estado consistente, y un aviso perdido no pierde datos.

**La atomicidad es por comando de `libmcrd`.** Un save de DW1 puede ser varios comandos, por ejemplo crear el archivo y luego escribirlo. Un corte entre ellos deja el mismo estado que apagar la consola en ese punto: tarjeta legible, anterior o nueva respecto del comando. Nativo se comporta igual.

**`#play` y recargas.** Hipótesis sin verificar para el fallo del pipeline 141: `#pack-status` queda en su valor inicial si el Web Lock de la página anterior todavía no se liberó. `start()` espera ese lock antes de `launch()`. Esta historia no agrega IndexedDB al camino de habilitación de `#play`, y el bucle de recargas y el aserto de `#instance` lo miden. Si aparece, se investiga la causa, no se amplía el timeout.

**Redux fuera de CI.** El job `testing` tiene tope de 2 h, ya superó 1 h con dos pasadas de CTest, y su `timeout` vive en `.gitlab-ci.yml`, que no se toca por el costo de invalidar la caché. Dos capturas de Redux suman alrededor de 30 min, así que `cards_redux` queda como suite manual con evidencia, igual que `long`. CI cubre `web_cards` y `web_cards_real`, que sí entran solos por la primera pasada de `ctest --preset web`.

## Verification

**Commands:**
- `cmake --build --preset web && ctest --preset web --output-on-failure` -- expected: verde; clase B saltada sin disco.
- `DW1_DISC_DIR=... ctest --preset web -R 'native_cards|web_cards_real'` -- expected: bytes y frames iguales a nativo en ambos navegadores.
- `DW1_DISC_DIR=... python3 port/web/tests/run.py build/port/web cards_redux` -- expected: Redux y web cargan las tarjetas del otro.
- `DW1_DISC_DIR=... ctest --preset web -R 'web_scene_title|web_disc_real'` -- expected: verde, sin reintentos.
- `ctest --preset native-headless --output-on-failure` -- expected: verde.
