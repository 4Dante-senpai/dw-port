---
title: 'Historia 4.4: Pantallas de la Shell web y capacidades'
type: 'feature'
created: '2026-10-30'
status: 'in-review'
baseline_commit: '3115031f0cf037991ddb04e6a05ce0705a9c22dd'
route: 'dispatch'
review_loop_iteration: 1
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-3-seleccion-del-disco-validacion-y-pack-en-el-navegador.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** La Shell web (`dw1.html`) es un lanzador de desarrollo: sin aislamiento solo muestra un texto genérico, no detecta la falta de OPFS ni de `SharedArrayBuffer`, no impide dos pestañas, "Jugar" navega a otra URL (pierde el gesto del usuario, necesario para el audio) y el juego en wasm corre sin reloj de pared ni pausa (FR-15, FR-4, AD-8, AD-13).

**Approach:** La Shell verifica capacidades y toma el Web Lock `dw1-game` antes de cargar nada; muestra bienvenida y aviso de copia propia, disco, progreso, "Jugar", opciones, Memory cards y "Borrar datos"; "Jugar" arranca el juego en la misma página y crea el `AudioContext` dentro del gesto. El hilo del juego gana ritmo de pared en `VSync` (como `native`) con una palabra de pausa compartida que la Shell activa con "Pausa" o al ocultarse la pestaña.

## Boundaries & Constraints

**Always:** sin una capacidad (aislamiento, memoria compartida, OPFS, Web Locks) la Shell muestra un mensaje por cada capacidad faltante y no carga `dw1.js` ni crea workers; la segunda pestaña muestra un mensaje, no carga el juego ni toca OPFS (el lock también serializa construcción y borrado del Pack, deferred de 4.3) y continúa sola al liberarse el lock; la pausa retiene al hilo del juego dentro de `VSync`, el conteo de VBlanks no avanza y al reanudar el deadline se rebasa sin contar resincronización; las corridas con captura de Escena siguen sin ritmo de pared (como `native`); un solo reloj virtual, la pared nunca ordena eventos; textos de la Shell en español; IDs existentes de la Shell (`#pack-status`, `#disc`, `#play`, `#clear`, `#load-pack`, `#progress`, `#status`, `#storage-warning`, `#version`, `#drop-zone`) se conservan; tests clase A sin bytes del disco, clase B salta con 77 sin `DW1_DISC_DIR`.

**Never:** contenido de opciones, input, audio real, Memory cards (4.5–4.8: aquí solo sus secciones con el aviso de que llegan después); presentación continua (diferida en `deferred-work.md`; "Jugar" mantiene la corrida finita de 300 frames); service worker (4.9); cambios en `src/`; ASYNCIFY/JSPI; tocar el formato del Pack o el C del Pack.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Sin aislamiento | `/isolated-off/` | mensaje de aislamiento (COOP/COEP) | no se pide `dw1.js` ni workers |
| Sin `SharedArrayBuffer` | aislado, global eliminado | mensaje de memoria compartida | idem |
| Sin OPFS | `StorageManager.prototype.getDirectory` eliminado | mensaje de almacenamiento | idem |
| Segunda pestaña | lock `dw1-game` tomado por otra | mensaje "abierto en otra pestaña" | sin juego ni workers; al cerrar la primera, continúa |
| Pausa manual | juego corriendo | VBlanks quietos, botón "Reanudar" | al reanudar: 0 resincronizaciones, ~60 VBlank/s |
| Pestaña oculta | `visibilitychange` a `hidden` | igual que pausa | al volver visible reanuda salvo pausa manual activa |
| "Jugar" | Pack disponible, autoplay bloqueado sin gesto | un clic: juego en la misma página, `AudioContext` `running` | — |
| Recarga con Pack | Pack + sidecar | "Jugar" directo, sin bienvenida | — |

</frozen-after-approval>

## Code Map

- `port/web/dev/index.html` -- Shell entera: rama de query (`?frames`, `?pack`, `?io-test`, captura: arranque directo de desarrollo/tests) y `launch()` (lanzador de 4.3). Reestructurar en: capacidades → lock → (query: `boot(args)`) | (Shell). `boot` reemplaza la navegación de "Jugar".
- `port/host/web/present_gl.c:46-80` -- `plat_present`; al final llamar al ritmo web (fuera de captura, `port_capture_active()`).
- `port/host/sdl/video.c:39-68` -- `pace()` nativo: modelo exacto a replicar (ancla, `CLOCK_VBLANK_NUM/DEN`, resincronización con >4 VBlanks de atraso).
- `port/host/web/io_mailbox.c` -- precedente de palabra compartida exportada (`io_mailbox_address`, `emscripten_futex_wait/wake`).
- `port/psyq/crt.c:60-68` -- `port_scene_mark`; en `__EMSCRIPTEN__` publicar el nombre en `Module.marks` (la Escena clase B detecta el título tras "Jugar"). Sin cambios para `native`.
- `CMakeLists.txt:690-730` -- fuentes y `EXPORTED_FUNCTIONS` de `dw1`; tests `web_*`.
- `port/web/tests/{run.py,playwright.config.cjs,launcher.spec.cjs,boot.spec.cjs,title.spec.cjs,storage.cjs}` -- runner, flags de navegador, test de aislamiento (`boot.spec.cjs:52-60`, hoy espera `cross-origin isolation required`), "Jugar" navega (`launcher.spec.cjs:37-39`).
- Medido: sin Pack, el juego corre 2000 frames (~2918 VBlanks) en ~12 s sin ritmo en Chromium; sirve como juego corriendo para la prueba de pausa clase A.

## Tasks & Acceptance

**Execution:**
- [x] `port/host/web/pace.c`, `port/host/web/pace.h` -- ritmo de pared del hilo del juego (modelo de `video.c`) con bloque de control exportado (`web_control_address`: pausa pedida, retenido, VBlanks, resincronizaciones, pausas); espera con `emscripten_futex_wait` sobre la palabra de pausa para despertar al pedirla -- AD-8/FR-15.
- [x] `port/host/web/present_gl.c`, `port/psyq/crt.c`, `CMakeLists.txt` -- llamar al ritmo tras presentar; `Module.marks` en web; sumar `pace.c` y exportar `_web_control_address`; CTest `web_shell`.
- [x] `port/web/dev/index.html` -- capacidades con mensaje por capacidad (`#capabilities li[data-capability=isolation|shared-memory|storage|locks]`), lock con mensaje `#instance` y espera, secciones bienvenida/aviso legal, disco, progreso, "Jugar", opciones, Memory cards, "Borrar datos"; "Jugar" crea `AudioContext` en el gesto y llama `boot` en la página; botón `#pause` y `visibilitychange`; `Module.control()` para tests.
- [x] `port/web/tests/shell.spec.cjs`, `run.py`, `playwright.config.cjs` -- suite `shell` clase A con toda la matriz; autoplay bloqueado sin gesto (Chromium `--autoplay-policy=user-gesture-required`, Firefox `media.autoplay.*`).
- [x] `port/web/tests/{boot,launcher}.spec.cjs` -- ajustar al nuevo mensaje de aislamiento y a "Jugar" en la página.
- [x] `port/web/tests/title.spec.cjs` -- clase B: un clic en "Jugar" con el Pack real llega a `runLandingScreen` con `AudioContext` `running`.
- [x] `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-4/` -- cómo correr `web_shell`; evidencia y criterios no verificados en esta sesión.
- [x] `tools/port/Dockerfile`, `port/web/tests/run.py` -- paquete `pulseaudio` en la imagen `dw-port` y un servidor con sink nulo por cada corrida de `run.py`: sin servidor de audio, Firefox deja todo `AudioContext` en `suspended`.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_shell` pasa en Chromium y Firefox y `web_boot`, `web_io`, `web_launcher`, `web_disc` siguen verdes.
- Given el juego corriendo con ritmo, when se pausa 2 s y se reanuda, then los VBlanks no cambian durante la pausa, las resincronizaciones siguen en 0 y la tasa medida tras reanudar está dentro del 10 % de 60,0087 Hz.
- Given `DW1_DISC_DIR`, when corre `web_scene_title`, then un único clic en "Jugar" llega a `runLandingScreen` con el `AudioContext` en `running`.

## Design Notes

Pausa en `pace()` (solo en `VSync`, AD-8):

```c
while ((now = now_ns()) < deadline && !atomic_load(&control.pause))
    emscripten_futex_wait(&control.pause, 0, (deadline - now) / 1e6);
if (atomic_load(&control.pause)) { hold(); deadline = now_ns(); remainder = 0; }
```

El lock usa `ifAvailable` para decidir el mensaje y después una petición en espera, así la recarga (el documento viejo aún no liberó) no falla. El mensaje de audio de FR-4 se verifica con autoplay bloqueado: un `AudioContext` creado sin gesto queda `suspended` (control negativo del test).

## Verification

**Commands:**
- `cmake --preset web && cmake --build --preset web && ctest --preset web` -- expected: verde; clase B saltada.
- `python3 port/web/tests/run.py build/port/web shell --project chromium` -- expected: todos los casos pasan.
- `ctest --preset native-headless` -- expected: verde (`crt.c` solo cambia en `__EMSCRIPTEN__`).

## Review Triage Log

Revisión Codex (`codex-merged`) sobre `12268fe`, con verificación local en el contenedor del proyecto.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| «Jugar» termina tras 300 frames y no llega al título; `title.spec.cjs` lo oculta con `DW1_PLAY_FRAMES` | high | intent_gap | Sin la sobrescritura, la prueba de un clic no llega a `runLandingScreen` en 360 s (Chromium). El Intent congelado exige la corrida de 300 frames y el criterio exige llegar al título: se contradicen. |
| Falta presentación continua | false | rechazado | El Intent la excluye; ya está en `deferred-work.md`. Queda atada a la decisión del hallazgo anterior. |
| `locks.request` puede rechazar sin aviso | low | patch | La promesa nunca resolvía; se agregó `.catch(reject)` a las dos esperas. |
| Falla la descarga de `dw1.js` | medium | patch | Quedaba un canvas vacío sin mensaje; `script.onerror` lo informa, con test. |
| «Pausa» sigue visible al terminar la corrida | low | patch | `Module.onFinished` la oculta; el test ya no necesita un clic extra. |
| Falta `lang="es"` | low | patch | Corrección directa. |
| Chromium sin política de autoplay explícita | low | patch | `--autoplay-policy=document-user-activation-required`; `user-gesture-required` no bloquea un `AudioContext` del mismo origen: el control negativo quedó `running`. |
| `released()` sin prueba determinista | medium | patch | Test nuevo con un worker que retiene `us.pack`, lo libera y agota el plazo. |
| Falta chequeo de WebGL2 y `OffscreenCanvas` | low | rechazado | Improbable en navegadores con las cuatro capacidades, y agrega rama y mensaje nuevos. |
| `getDirectory` lanza de forma síncrona | false | rechazado | Devuelve una promesa rechazada, que `capabilities()` ya maneja. |
| Worker de E/S falla antes de `ready` | false | rechazado | `onerror` ya rechaza y el `catch` lo informa. |
| `AudioContext` lanza o `resume()` rechaza | low | rechazado | Improbable; el audio real llega en la 4.7. |
| Ráfaga breve tras resincronizar | low | rechazado | `pace.c` rebasa `deadline` a `now`; una cota fina chocaría con los saltos periódicos de 187 VBlanks. |
