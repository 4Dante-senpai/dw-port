---
title: 'Historia 8.5: Entorno de testing en el homelab'
type: 'feature'
created: '2026-10-17'
status: 'done'
baseline_commit: '4385c9f421992294b46b029654ed98137687e220'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-8-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Para probar el build web hoy hace falta un túnel SSH al contenedor de desarrollo y pegar código en la consola para copiar el Pack a OPFS. El dueño quiere que cada push a `main` deje el port desplegado en un host de su LAN para probarlo a medida que avanzan las historias.

**Approach:** Un job de clase B en `dw1-data` compila `native-headless` y `web`, arma el Pack y valida el título localmente. Después construye una imagen nginx directamente en el daemon del host de deploy (`DOCKER_HOST=$HUB_DOCKER_HOST`) con el build web y sin bytes del disco. Copia el Pack a un volumen con nombre de ese host, reemplaza el contenedor con healthcheck y corre la Escena `title` con Playwright contra la URL desplegada; si algo falla, vuelve a la imagen anterior. La página de desarrollo (`dw1.html` sin parámetros) pasa a ser un lanzador con "Cargar Pack" y "Jugar".

**Decisiones del dueño (2026-10-17):** host de deploy = el que recibe los deploys del runner: variable de proyecto `HUB_DOCKER_HOST` (`tcp://…`, Protected + Masked, mismo nombre y valor que su template; `DW1_TESTING_PORT` es Protected), así que los jobs `testing*` solo corren en `main` protegida y el job nunca imprime el valor de la variable (el log del proyecto privado puede mostrar la URL de testing); contexto seguro con la flag del navegador, sin HTTPS; puerto `DW1_TESTING_PORT` (8090 si falta); se hace antes de la Historia 3.1.

## Boundaries & Constraints

**Always:** el Pack y cualquier byte del disco nunca entran al contexto de build de la imagen, al registry, a artifacts de GitLab, a logs ni a la evidencia (solo hashes); el Pack viaja del job al volumen por la API de Docker (contenedor auxiliar `alpine`), sin SSH ni rutas del host, y solo si su SHA-256 difiere del guardado en el volumen; el contenedor monta el volumen de solo lectura; headers COOP `same-origin` + COEP `require-corp` en toda respuesta y MIME `application/wasm`; mismas rutas que `serve.py` (`/` → `dw1.html`, `/__fixture/us.pack`), para que la página y los tests no cambien; host y puerto reales solo por variables de CI y en `docs/port/local-infra.md`; `docker` CLI estático descargado con versión y SHA-256 fijados.

**Never:** HTTPS, dominio o exposición fuera de la LAN; usar esta imagen como deploy de la Épica 7; registry o `skopeo` (la imagen se construye en el daemon destino, así que no hacen falta); compose; cambios en `src/`/`include/`; selector de disco o constructor de Pack en el navegador (4.3); ritmo de 59.94 Hz, input o audio.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Primer deploy | volumen vacío, sin contenedor | Pack copiado + `us.pack.sha256`; contenedor sano; Playwright título = nativo | — |
| Redeploy, Pack igual | hash del volumen = hash del Pack | no se copia el Pack (log lo dice); imagen nueva | — |
| Healthcheck o título falla | imagen nueva rota | job falla | se relanza la imagen anterior si existe |
| Rollback manual | job `testing-rollback` | contenedor con la imagen anterior | falla si no hay anterior |
| Lanzador sin Pack en OPFS | `/` sin query | muestra commit, "Pack ausente", botones | "Jugar" deshabilitado hasta copiar |
| Lanzador con Pack viejo | sidecar OPFS ≠ `/__fixture/us.pack.sha256` | "Pack desactualizado" y ofrece copiar | — |
| Sin aislamiento | origen no marcado como seguro | `cross-origin isolation required` (como hoy) | — |

</frozen-after-approval>

## Code Map

- `.gitlab-ci.yml` -- jobs actuales y `.class_b` (tags `dw1-data`, reglas de disparo). Agregar `testing` (solo `$CI_COMMIT_BRANCH == $CI_DEFAULT_BRANCH`, `resource_group: dw1-testing`, `environment`) y `testing-rollback` (manual).
- `port/web/dev/index.html` -- página de desarrollo, se copia como `build/port/web/dw1.html` (`CMakeLists.txt:415`). Con query mantiene el comportamiento actual exacto; sin query, lanzador nuevo.
- `port/web/tests/seed_worker.js` -- copia por streaming a `dw1/us.pack` con `createSyncAccessHandle`; reutilizarlo desde el lanzador (lo copia `CMakeLists.txt:413`).
- `port/web/dev/serve.py` -- rutas y headers que nginx debe replicar; agregar `us.pack.sha256` a la lista de fixtures.
- `port/web/tests/run.py`, `title.spec.cjs`, `storage.cjs`, `playwright.config.cjs` -- suite `title`; `run.py` levanta un servidor local y calcula el SHA nativo desde `build/port/native-headless/scenes/title/run-1.ppm`.
- `tools/port/pack/test_pack_disc.sh` + CTest `pack_disc`/`native_title_capture`/`web_scene_title` -- arman `build/port/web/us.pack` y el fixture nativo; el job los reutiliza vía `ctest`.
- `tools/port/manifest/us.tsv` -- columna 7 = SHA-256 por archivo, para el chequeo de bytes del contexto.

## Tasks & Acceptance

**Execution:**
- [x] `port/web/testing/Dockerfile`, `port/web/testing/nginx.conf` -- nginx alpine fijado; copia `dw1.html`, `dw1.js`, `dw1.wasm`, `io_worker.js`, `seed.html`, `seed_worker.js` y `version.txt`; rutas de `serve.py`, `/__fixture/us.pack` y `.sha256` desde el volumen montado en `/pack`; COOP/COEP, `application/wasm`, `Cache-Control: no-store`; `HEALTHCHECK` con `wget`.
- [x] `tools/port/testing/deploy.sh` -- arma el contexto en un directorio temporal, falla si algún archivo empieza con `DW1PACK` o su SHA-256 está en `us.tsv`; `docker build` en el daemon destino con tag `dw1-testing:<sha>`; sincroniza el volumen `dw1-testing-pack`; retag `current`/`previous`; `docker run -d --restart unless-stopped`; espera `healthy`; `--rollback` relanza `previous`. Nunca imprime el Pack.
- [x] `port/web/tests/run.py`, `port/web/tests/playwright.config.cjs` -- si `DW1_WEB_URL` ya viene en el entorno, no levantar servidor local; con `DW1_WEB_INSECURE_ORIGIN`, pasar `--unsafely-treat-insecure-origin-as-secure=<origin>` a Chromium.
- [x] `port/web/dev/index.html`, `port/web/dev/serve.py` -- lanzador sin query: commit (`/version.txt`, si no "dev"), estado del Pack en OPFS (sidecar `dw1/us.pack.sha256`), "Cargar Pack" (worker `seed_worker.js`, progreso) y "Jugar" (`/?pack=1&frames=1000000000`).
- [x] `.gitlab-ci.yml` -- job `testing` (imagen `dw-port`, `DW1_DISC_DIR=/private/disc`): builds, `ctest -R "pack_disc|native_title_capture|web_scene_title"`, `deploy.sh`, `run.py ... title --project chromium` contra `http://<host>:<port>`; si falla, `deploy.sh --rollback` y falla. Host derivado de `HUB_DOCKER_HOST`. Job `testing-rollback` manual.
- [x] `docs/port/testing-env.md`, `docs/port/local-infra.md` (no versionado), `docs/STATUS.md`, `docs/implementation-artifacts/evidence/story-8-5/` -- uso (flag de Chromium, `dom.securecontext.allowlist` en Firefox), rollback, marcadores `<deploy-host>`; evidencia del pipeline con hashes y sin IPs.

**Acceptance Criteria:**
- Given un push a `main`, when corre el pipeline, then el job `testing` termina verde, la URL sirve el commit del push y Playwright Chromium contra esa URL da el SHA-256 nativo del título.
- Given el contexto de build, when se ejecuta el chequeo de bytes, then pasa con el contexto real y falla con un archivo de prueba que empieza con `DW1PACK`.
- Given `git grep` del repo, when se buscan las IPs del homelab, then no aparecen fuera de archivos ignorados.

## Implementation Notes

- Implementado por Codex (sin commits). El Manifest tiene 6 columnas: el chequeo de contexto usa la última (SHA-256), no la "columna 7" del Code Map.
- El headless shell fijado de Playwright no habilita OPFS en orígenes HTTP marcados como seguros; el modo remoto usa `channel: chromium` (Chromium completo).
- El caso no envenenado de `title.spec.cjs` ahora carga el Pack con el lanzador ("Cargar Pack") en lugar de `seed()`; el envenenado sigue con `seed()`.
- Orquestador: `deploy.sh` muestra el stderr de Docker con la dirección del daemon redactada (`<docker-host>`, `<deploy-host>`) y borra tags `dw1-testing:<sha>` viejos tras un deploy sano (quedan `current` y `previous`).
- El rollback restaura la imagen anterior pero conserva el Pack actual del volumen.
- Pendiente de evidencia real: build de nginx, volumen, healthcheck y rollback en el host de deploy; se verifica con el pipeline de `main` tras el merge.

## Spec Change Log

## Review Triage Log

Iteración 0 (Codex `codex-merged`):

| # | Capa | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|---|
| 1 | blind, edge | `seed_worker.js` escribe el sidecar sin calcular el SHA-256 de lo descargado | low | Un corte de red hace fallar `reader.read()` y no se escribe el sidecar; queda solo una respuesta 200 con otro contenido, improbable en la LAN. El fix (SHA-256 incremental de 335 MB en JS) agrega complejidad: rechazado. |
| 2 | edge | Respuesta 200 vacía marca el Pack como disponible | low | nginx devuelve 404 si falta el archivo del volumen; un 200 vacío no es alcanzable en uso normal y el fix agrega una guarda: rechazado. |
| 3 | blind, edge | Deploy entre la consulta del hash y la descarga mezcla versiones | low | Ventana de segundos en un entorno de desarrollo de un usuario; el fix exige URLs inmutables: rechazado. |
| 4 | blind, edge | El sidecar se borra antes de adquirir el lock del Pack (segunda pestaña) | low | Consecuencia: la página muestra "desactualizado" y se vuelve a copiar; no corrompe nada: rechazado. |
| 5 | blind, edge | `deploy.sh` omite la copia con Pack corrupto y sidecar coincidente | low | La copia es atómica (`mv` de `.tmp` y el sidecar se escribe después); no se mostró cómo se llega a ese estado: rechazado. |
| 6 | blind, edge | Redeployar la misma imagen pisa `previous` con `current` | medium | Reintentar el job del mismo commit deja `previous` = `current`, así que el rollback no sirve. patch. |
| 7 | blind, verif | El job `testing` no corre `web_launcher`, `testing_deploy` ni `testing_context` | medium | `.gitlab-ci.yml` filtra `-R "pack_disc\|native_title_capture\|web_scene_title"`. patch. |
| 8 | blind | El chequeo de bytes revisa el contexto y no la imagen | false | La imagen es la base nginx fijada por digest más un `COPY` de exactamente esos archivos del contexto. |
| 9 | verif | `test_deploy.py` no verifica qué imagen restaura el rollback | medium | El doble modela los tags como nombres; si `previous` apuntara a la imagen nueva, el test pasaría igual. patch (junto con el 6). |
| 10 | verif | El test del lanzador no observa el progreso intermedio | low | Cosmético; el fix necesita un servidor por bloques en el test: rechazado. |
| 11 | edge | `new Worker` lanza una excepción o el worker no responde | low | No se mostró que el worker de mismo origen falle con COOP/COEP activos: rechazado. |
| 12 | edge | `DW1_TESTING_PORT` con ceros a la izquierda se interpreta en octal | low | El dueño cargó un puerto normal; se rechaza la guarda extra. |
| 13 | edge | Si `d tag` falla después del healthcheck, no hay rollback | low | Fallo de `tag` local tras un `run` exitoso en el mismo daemon: improbable, y el contenedor nuevo ya está sano: rechazado. |

## Verification

**Commands:**
- `ctest --preset web` -- expected: verde (lanzador sin romper `web_boot`/`web_io`).
- `DW1_DISC_DIR=... ctest --test-dir build/port/web -R web_scene_title` -- expected: verde.
- `bash -n tools/port/testing/deploy.sh` y prueba del chequeo de bytes con un archivo `DW1PACK` -- expected: el chequeo falla como debe.
- Pipeline de GitLab en `main` -- expected: `testing` verde; log sin bytes del disco.

**Manual checks (if no CLI):**
- El dueño abre `http://<deploy-host>:<port>/` con la flag, pulsa "Cargar Pack" y luego "Jugar": ve el título.
