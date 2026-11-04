---
title: 'Historia 4.10: Checks del build web: ritmo, tamaño, reproducibilidad, red y datos'
type: 'feature'
created: '2026-11-04'
status: 'ready-for-dev'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-9-offline-y-actualizaciones.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** NFR-1 web, NFR-3, NFR-4, NFR-5 y NFR-7 no tienen un check automático. Hoy el ritmo de 10 minutos (`long.spec`) solo se corre a mano, y no se mide el tamaño, la reproducibilidad, la red ni los bytes del disco en los artefactos. Un merge puede empeorar cualquiera de esas garantías sin que el CI lo marque.

**Approach:** Cuatro checks en CI que no relajan ningún umbral:
- `size_check`, en la clase A: Brotli del programa.
- `repro_check`, en la clase A: segundo build en otro directorio y comparación de los `.wasm`.
- `long.spec` como test `web_long` de ctest, en la clase B, con registro de red en Playwright.
- `data_leak_check` en C, en la clase B: ventanas de 64 bytes del Pack contra los artefactos web, los objetos de `port/` y el árbol del repo.

## Boundaries & Constraints

**Always:**
- **Unidades.** 4 MB = 4.000.000 bytes y 6 MB = 6.000.000 bytes.
- **Brotli.** Calidad 11, con `zlib` de Node de la imagen.
- **Primera carga.** Es la suma del Brotli de cada archivo de `FILES` de `build/port/web/sw.js` (`/` es `dw1.html`), más `version.txt` si existe.
- **Reproducibilidad.** Se compara SHA-256 de `dw1.wasm` y `dw1_pack.wasm` entre dos directorios de build distintos, con la misma imagen y el mismo commit.
- **Red.** Se registra todo pedido de página, workers y worklet que no sea `data:` ni `blob:`. Cualquier origen distinto del de `DW1_WEB_URL` hace fallar el test.
- **Bytes del disco.**
  - `V(disco)` es el conjunto de ventanas de 64 bytes de `us.pack`, que tiene el contenido de todos los archivos del disco.
  - Una ventana del artefacto que está en `V(disco)` es una fuga si no es trivial (menos de 5 valores de byte distintos) y tiene algún byte que ninguna ventana del artefacto presente en `V(excepción)` cubre.
  - Toda coincidencia por hash se confirma comparando los bytes.
  - Excepciones:
    - para `dw1.wasm`: los objetos de `dw1_game_objs`;
    - para los objetos de `port/`: ninguna;
    - para el árbol (`git ls-files` fuera de `src/` e `include/`, sin gitlinks): los archivos versionados de `src/` e `include/`.
- **Logs y salida.** El check, sus logs y la evidencia muestran solo ruta, offset, cantidades y hashes; nunca bytes del disco.
- **Sin disco.** Sin `DW1_DISC_DIR` ni `us.pack`, los tests de clase B salen con 77. En CI se corren con `ctest_no_skip.sh`.
- **Umbrales.** Los asertos de `long.spec` siguen iguales.

**Never:**
- Cambios en `src/` o `include/`.
- Subir umbrales o timeouts para tapar un fallo, ni reintentos.
- Imprimir o guardar bytes del disco.
- Incluir un fragmento real del disco en un fixture versionado: el test de contaminación lo copia del Pack en runtime a un directorio temporal.
- Dependencias nuevas.
- Mover `web_long` o `data_leak_check` a la clase A.
- Tocar el job `testing` más allá de sumar el aserto de red que ya trae `title`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tamaño dentro | build `web` actual | `size_check` imprime los bytes de cada archivo y el total, y sale con 0 | — |
| `.wasm` grande | `--wasm-limit` menor que el Brotli de `dw1.wasm` | sale distinto de 0 y nombra el límite | test `WILL_FAIL` |
| Primera carga grande | `--first-load-limit` menor que el total | sale distinto de 0 | test `WILL_FAIL` |
| Falta un archivo | un archivo de `FILES` no existe | sale distinto de 0 y nombra el archivo | — |
| Builds iguales | dos directorios del mismo commit | `repro_check` imprime dos SHA-256 iguales por archivo | — |
| Builds distintos | difiere un `.wasm` | sale distinto de 0 y nombra el archivo | — |
| Pedido externo | página y worker piden a otro origen | el helper registra los dos pedidos y el test negativo lo afirma | — |
| Sin fugas | Pack real y artefactos actuales | `data_leak_check` sale con 0 e imprime ventanas analizadas y cubiertas | — |
| Contaminación | copia temporal de un objeto de `port/` con 64 bytes no triviales del Pack | sale distinto de 0 y nombra archivo y offset | test `WILL_FAIL` o un aserto en el script |
| Ventana de ceros | 64 bytes `00` en objetos de `port/` y en el Pack | se ignora por trivial | — |
| Sin disco | sin `DW1_DISC_DIR` | `web_long` y `web_data_leak*` salen con 77 | — |

</frozen-after-approval>

## Code Map

- `port/web/tests/long.spec.cjs` -- L4 de 10 minutos. Ya afirma un desvío menor a 0,1 %, 0 resincronizaciones y la memoria. Hay que sumarle el aserto de red. `run.py:19,44` ya lo trata como suite con disco.
- `port/web/tests/storage.cjs` -- helpers compartidos (`seed`, `errors`, `browserMemory`). Ahí va el registro de red.
- `port/web/tests/boot.spec.cjs:48` -- modelo de `page.on('request')`. Ahí va el test negativo de clase A.
- `port/web/tests/title.spec.cjs` -- en el job `testing` corre contra el deploy (`DW1_WEB_URL`). Ahí va el aserto de red sobre el origen del deploy.
- `CMakeLists.txt:131` -- `-ffile-prefix-map` solo del directorio de fuentes; los stubs se generan en el build. `:720-813` son los tests web. `:784-786` es el patrón de la herramienta nativa llamada desde el preset `web` (`pack_disc`). `:465-467` es el modelo de `RUN_SERIAL` (`native_wall_10min`).
- `build/port/web/CMakeFiles/dw1_game_objs.dir/{src,ovl,sjis}` -- objetos del Juego. `ovl` y `sjis` son TUs del port que incluyen C de `src/`, así que cuentan como excepción. Los demás `CMakeFiles/*.dir` son objetos de `port/`.
- `tools/port/testing/check_context.py` -- check del deploy que ya existe: rechaza el Pack y los archivos del Manifest. No cambia.
- `tools/port/ci/ctest_no_skip.sh` -- envoltorio de clase B que convierte un 77 en fallo.
- `.gitlab-ci.yml:63-67` (`web-objects`, clase A), `:23-30` (`.class_b`, `resource_group dw1-disc`), `:69-89` (`native-disc`, modelo de job con disco).
- `docs/implementation-artifacts/evidence/story-4-7/README.md:67-85` -- antecedente: un Firefox local con 2,5 CPU tuvo 1 resincronización, y con 20 CPU no tuvo ninguna.
- Medido el 2026-11-04 sobre el build `web` local: `dw1.wasm` pesa 1.557.215 B con Brotli y la primera carga 1.842.457 B.
- No cambiar: `src/`, `include/`, `port/backend/`, `port/web/dev/index.html`, `port/web/sw.js`, ni el job `testing` salvo lo indicado.

## Tasks & Acceptance

**Execution:**
- [ ] `tools/port/ci/size_check`, `CMakeLists.txt` -- script de Node ejecutable con `size_check <build> [--wasm-limit N] [--first-load-limit N]`, que lee `FILES` de `sw.js` según Always. Tests `web_size_check` (normal) y `web_size_check_fails_wasm` / `web_size_check_fails_first_load` (`WILL_FAIL`, límite 1000).
- [ ] `tools/port/ci/repro_check.sh` -- recibe `<build>`, configura `cmake --preset web -B build/port/web-repro` desde cero, compila `dw1 dw1_pack sw.js` y compara según Always. Si difieren, corregirlo en `CMakeLists.txt` (por ejemplo `-ffile-prefix-map=${CMAKE_BINARY_DIR}=build` o `SOURCE_DATE_EPOCH`) y documentar la causa.
- [ ] `tools/port/ci/data_leak_check.c`, `tools/port/ci/data_leak_check`, `CMakeLists.txt` -- herramienta C nativa `dw1-leak-check` (solo presets no Emscripten, `-O2`) con `--disc <us.pack> --exception <archivos...> -- <artefactos...>`. Usa un hash rodante de 64 bytes, una tabla con los hashes de las ventanas no triviales del artefacto que no están cubiertas y un solo recorrido del Pack. Sale con 1 si hay fuga y con 2 si hay error de entrada. El envoltorio `data_leak_check <web-build> <repo>` arma las tres corridas de Always y llama a `build/port/native-headless/dw1-leak-check`.
- [ ] `tools/port/ci/test_data_leak_check.sh`, `CMakeLists.txt` -- tests `web_data_leak` y `web_data_leak_contaminated` (`SKIP_RETURN_CODE 77`, `FIXTURES_REQUIRED disc_pack`). El segundo copia a un temporal un objeto de `port/`, le agrega una ventana del Pack con 16 o más valores de byte distintos y afirma la salida 1, sin imprimir la ventana. Suma un caso sintético sin disco (archivos generados) que prueba trivial, cubierto y fuga: test `data_leak_check_synthetic` en `native-headless`.
- [ ] `port/web/tests/storage.cjs`, `boot.spec.cjs`, `long.spec.cjs`, `title.spec.cjs` -- `foreignRequests(page)` con `page.context().on('request')`. Hipótesis por verificar: incluye workers y worklet; si no, sumar `page.on('worker')`. Test negativo en `boot.spec`: la página y un worker hacen `fetch` a `http://127.0.0.2:9/` y el helper registra dos pedidos. `long` y `title` afirman `[]`.
- [ ] `CMakeLists.txt` -- `web_long`: `run.py <build> long`, `SKIP_RETURN_CODE 77`, `FIXTURES_REQUIRED disc_pack`, `RUN_SERIAL TRUE`, `TIMEOUT 2400`.
- [ ] `.gitlab-ci.yml` -- `web-objects`: después de `ctest`, `tools/port/ci/repro_check.sh build/port/web`. Job nuevo `web-disc` (`extends: .class_b`, `DW1_DISC_DIR: /private/disc`, timeout de 1 h 30 min): compila `native-headless` y `web` y corre `tools/port/ci/ctest_no_skip.sh --preset web --output-on-failure -R "^(pack_disc|web_data_leak|web_data_leak_contaminated|web_long)$"`.
- [ ] `docs/port/setup.md`, `docs/implementation-artifacts/deferred-work.md`, `docs/implementation-artifacts/evidence/story-4-10/` -- cómo correr cada check y qué mide.
  - En `deferred-work`, dos pendientes para la Épica 7:
    - nginx no comprime;
    - la instalación de `sw.js` vuelve a descargar el programa en la primera visita.
  - En la evidencia: salidas de `size_check` y `repro_check`, el JSON de `long` por navegador y el resumen de `data_leak_check`.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corre, then `web_size_check*` y `web_boot` (con el test negativo de red) pasan, y `web_long` y `web_data_leak*` se saltean.
- Given `tools/port/ci/repro_check.sh build/port/web` en la imagen `dw-port`, when termina, then los SHA-256 de `dw1.wasm` y `dw1_pack.wasm` coinciden.
- Given el disco, when corre `ctest --preset web -R "pack_disc|web_long|web_data_leak"` en remoto y en serie, then `web_long` pasa en Chromium y Firefox con desvío menor a 0,1 % y red `[]`, `web_data_leak` pasa y `web_data_leak_contaminated` detecta la fuga.
- Given `ctest --preset native-headless -R data_leak`, when corre, then el caso sintético pasa.

## Implementation Notes

## Spec Change Log

## Review Triage Log

## Design Notes

**Disco = Pack.** El BIN tiene cabeceras de sector cada 2352 bytes, que cortan las ventanas. El Pack guarda el contenido contiguo de cada archivo (lo construye la fixture `disc_pack`) y sus bytes propios (cabecera e índice) no aparecen en los artefactos.

**Cobertura por byte.** El linker pega datos de varios objetos de `src/`. Dos tablas contiguas en el exe también quedan contiguas en el `.wasm`, y la ventana que cruza el límite está en el disco pero no en ningún objeto. Exigir que cada byte esté cubierto por alguna ventana de la excepción acepta ese caso. Igual detecta una fuga de 64 bytes o más que no viene de `src/`, porque su ventana interior no tiene cobertura.

**Ventanas triviales.** Los ceros, `0xFF` y los rellenos aparecen en cualquier objeto y en el disco. Sin un filtro, el criterio de los objetos de `port/` no puede pasar. El umbral de 5 valores distintos es una heurística (`ponytail:` en el código). Si la corrida real muestra coincidencias no triviales en `port/`, se informan con ruta y offset y no se sube el umbral.

**Primera carga estática.** Sumar el Brotli de `FILES` es determinista y corre en la clase A. La transferencia real depende de que el servidor comprima (nginx de testing hoy no lo hace) y de la doble descarga del service worker. Las dos cosas quedan para el deploy de la Épica 7.

## Verification

**Commands:**
- `cmake --build --preset web && ctest --preset web --output-on-failure` -- expected: verde, clase B saltada.
- `tools/port/ci/repro_check.sh build/port/web` -- expected: SHA-256 iguales.
- `cmake --build --preset native-headless && ctest --preset native-headless -R data_leak --output-on-failure` -- expected: verde.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/remote_run.sh ctest --preset web -R "pack_disc|web_long|web_data_leak" --output-on-failure` -- expected: verde, sin reintentos.
- `git diff --stat <baseline> -- src include` -- expected: vacío.
