---
title: 'Capturas de referencia más rápidas: pasadas en paralelo y caché en CI'
type: 'chore'
created: '2026-10-18'
status: 'in-review'
baseline_commit: '76356378345061082535757efc34952bcb158265'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/port/reference.md'
  - '{project-root}/docs/port/scenes.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** La referencia `newgame --state` tarda unos 20 minutos: PCSX-Redux corre en modo
intérprete con debugger (lo exigen los breakpoints) y `capture.py` hace dos pasadas de ~30.000 VBlanks
una detrás de otra. Además, `reference_newgame_state` recaptura siempre, y cada job de CI arranca con
`build/` vacío, así que los jobs `native-disc` y `native-asan-disc` repiten la captura en cada pipeline
aunque nada haya cambiado.

**Approach:** (1) Las dos pasadas de determinismo corren en paralelo, cada una con su directorio de
trabajo y su Xvfb. (2) `capture.py --reuse` no recaptura cuando el sello `state.sha256` coincide con la
huella actual; `reference_newgame_state` lo usa, y CI guarda `build/reference/` en la caché de GitLab.

**Decisión del dueño (2026-10-18):** hacer los puntos 1 y 2 de la propuesta como historia aparte tras
cerrar E3; descartados savestates y dynarec por riesgo para el determinismo.

## Boundaries & Constraints

**Always:** El sello solo se escribe tras verificar determinismo (dos pasadas idénticas), como hoy. Una
captura reutilizada debe ser exactamente la que produciría una captura nueva con la misma huella. La
caché vive solo en jobs clase B del runner `dw1-data` (servidor del dueño); nunca en artifacts.

**Never:** Savestates, dynarec o quitar `-interpreter`/`-debugger`. Cambiar el formato de trazas, el
contenido de la huella para escenas existentes, o la semántica de skip (77) sin disco/emulador. Reutilizar
con `--update` o sin `--state`. Paralelizar las pasadas nativas de `run.py`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pasadas paralelas | `capture.py newgame --state` | las dos corridas se solapan en el tiempo; mismas salidas y sello que hoy | N/A |
| Una pasada falla | `run-2` sale con error o timeout | error con el log de esa corrida; sin sello | se espera a la otra antes de salir |
| Caché válida | sello = huella, `run-{1,2}.state` y `run-1.ppm` presentes, `--reuse` | no lanza el emulador; imprime `cached`; exit 0 | N/A |
| Caché vieja | sello distinto (escena, BIOS, scripts) o faltan archivos | recaptura completa | N/A |
| Pin cambiado | `reference_sha256` fijado y distinto del SHA-256 de `run-1.ppm` | recaptura (y falla si no coincide) | N/A |
| Uso inválido | `--reuse` con `--update` o sin `--state` | `ValueError` | mensaje claro |
| Sin disco | `--reuse` sin `DW1_DISC_DIR` | skip 77, igual que hoy | N/A |

</frozen-after-approval>

## Code Map

- `tools/port/reference/capture.py:185-189` -- bucle secuencial `run_once(... index ...) for index in (1, 2)`; pasar a `concurrent.futures.ThreadPoolExecutor(2)` (stdlib). `run_once` ya aísla `work-{index}`, log y trazas por índice.
- `tools/port/reference/capture.py:109` -- `xvfb-run -a` elige display mirando lock files: dos arranques simultáneos pueden elegir el mismo. Dar a cada pasada un número inicial distinto (`-n`, p. ej. `100 * index + 99`), dejando `-a` para saltar ocupados.
- `tools/port/reference/capture.py:180-187,211-213` -- `output`, limpieza previa y sello; la comprobación de `--reuse` va después de los chequeos de disco/emulador/BIOS y antes de borrar nada. Identidad: `_runner.bios_fingerprint(bios)` (igual al SHA-256 que usa hoy con `--bios`).
- `tools/port/scenes/run.py:220-230` -- `state_fingerprint`; reutilizar, no cambiar. `run.py:379-387` ya recaptura solo si el sello no coincide; no tocar.
- `tools/port/reference/test_capture.py:249-251` -- el `xvfb-run` falso hace `shift` de un solo argumento; adaptarlo a `-a -n N`. Fake emulator en `:252-277` (modos por `FAKE_MODE`).
- `CMakeLists.txt:444-447` -- `reference_newgame_state`: agregar `--reuse`.
- `.gitlab-ci.yml:51-70` -- `native-disc` y `native-asan-disc`: caché `build/reference/`, clave con `files: [.gitlab-ci.yml]` (incluye el digest de la imagen, que fija PCSX-Redux y OpenBIOS) y `when: always`. El runner usa la caché local del volumen `/cache` (`docs/port/setup.md:201`).
- `docs/port/reference.md`, `docs/port/scenes.md`, `docs/STATUS.md` -- documentar paralelo, `--reuse`, caché y tiempos medidos.

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/reference/capture.py` -- pasadas en paralelo con display inicial distinto por índice; opción `--reuse` según la matriz -- divide el tiempo de espera y evita recapturas.
- [x] `tools/port/reference/test_capture.py` -- `xvfb-run` falso compatible con `-n`; test de solapamiento (modo del fake que espera a que la otra pasada haya arrancado, con límite corto, y falla si no ocurre); tests de `--reuse`: hit sin emulador (`FAKE_MODE=fail` devuelve 0), recaptura con escena cambiada, recaptura con pin distinto, errores de uso -- cubre la matriz.
- [x] `CMakeLists.txt` -- `--reuse` en `reference_newgame_state` -- la referencia solo se regenera cuando cambia la huella.
- [x] `.gitlab-ci.yml` -- bloque de caché en los dos jobs con disco -- persistir la referencia entre pipelines.
- [x] `docs/port/reference.md`, `docs/port/scenes.md`, `docs/STATUS.md` -- comportamiento nuevo y tiempos antes/después.

**Acceptance Criteria:**
- Given la imagen actual y el disco, when corre `capture.py newgame --state` sin sello, then el sello y `run-1.state` son idénticos a los de la captura secuencial previa y el tiempo total baja de forma medible (registrar ambos en Implementation Notes).
- Given una referencia válida en `build/reference/newgame`, when corre `ctest -R reference_newgame_state`, then termina en segundos sin lanzar PCSX-Redux.
- Given la suite nativa y ASan con disco, when corren con `ctest_no_skip.sh`, then pasan sin skips.
- Given dos pipelines seguidos en `main` sin cambios en la huella, when corre `native-disc` en el segundo, then el log muestra la caché restaurada y `reference_newgame: cached`.

## Design Notes

La caché no decide validez: solo guarda archivos. La validez la decide el sello (huella de escena,
scripts, símbolos y SHA-256 de `openbios.bin`); la clave por `.gitlab-ci.yml` cubre lo que la huella no
ve (binario de PCSX-Redux vía digest de imagen). Datos derivados del juego (frames, trazas) quedan en el
volumen `/cache` del runner privado, mismo servidor que `/private` (AD-16); no son artifacts ni logs.

`when: always` guarda la caché aunque falle otro test del job: los archivos sin sello válido se
recapturan, así que una caché parcial no es peligrosa.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I -B tools/port/reference/test_capture.py` -- expected: verde.
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless --output-on-failure'` -- expected: verde.
- Suite con disco (`DW1_DISC_DIR`) nativa y ASan vía `tools/port/ci/ctest_no_skip.sh` -- expected: verde, sin skips; `time` de `reference_newgame_state` en frío y con caché.
- Dos pipelines en `main` (GitLab MCP `get_job`) -- expected: segundo `native-disc` con caché restaurada y `cached`.

## Implementation Notes

- Implementación: `ThreadPoolExecutor(max_workers=2)` y displays iniciales 199/299 con
  `xvfb-run -a -n`; el cierre del executor espera ambas pasadas incluso ante una excepción.
  El formato de trazas, la definición de `state_fingerprint` y las pasadas nativas no cambian.
- `--reuse` comprueba precondiciones, huella, los tres archivos requeridos y el pin de imagen
  antes de limpiar salidas. El fixture CTest lo usa; solo los dos jobs con disco guardan la
  referencia en caché privada con clave por `.gitlab-ci.yml` y `when: always`.
- Pruebas del capturador: 37/37 verdes dentro de este contenedor, con Lua/FFI reales y fake
  sincronizado para demostrar solapamiento. Cubren fallos de cualquiera de las dos pasadas,
  timeout, espera de la otra pasada, invalidación del sello, hit sin lanzar emulador,
  escena/BIOS/scripts/pin cambiados, archivos faltantes, pin correcto y usos inválidos.
- Medición real en este contenedor, con `DW1_DISC_DIR=/dw/game_rom/extracted`, escena
  `newgame --state --max-vblanks 40000 --timeout 1800`: secuencial original
  **1560,271 s** (26 min), paralela **871,147 s** (14 min 31 s), **44,2 % menos espera**.
  La baseline se lanzó antes de editar `capture.py`, conservando el código secuencial
  cargado en memoria y salidas separadas en `build/e3-fast-baseline/newgame/`.
  Las mediciones coexistieron parcialmente entre sí y con las suites: son tiempos bajo carga,
  no un benchmark aislado ni una garantía de reducción exacta a la mitad.
- Comparación automática final (`build/e3-fast-comparison.json`): `run-{1,2}.state`,
  `run-{1,2}.ppm` y `state.sha256` idénticos byte a byte entre ambas capturas completas.
- Captura paralela real: **871,147 s** (14 min 31 s), salida 0. Ambas trazas son idénticas
  byte a byte a ambas pasadas secuenciales, SHA-256
  `3439d3281483ac16c6614685cbc11c11c66fe03f410eec22551b46a1131e794b`.
  Ambos frames coinciden con las pasadas secuenciales y SCPH-1001:
  `48322f1c664daf07af2ff168782280ddd4a7bd918468988a16102b12f736e508`.
  Se conservan 9 puntos, 15 variables, 16 llamadas a `rand` y captura en VBlank 29587.
- `DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native-headless -R
  '^reference_newgame_state$' -V`: **0,13 s** según CTest (0,145 s de proceso),
  `reference_newgame: cached`; timestamps de ambos logs de emulador sin cambios.
- Los comandos se ejecutaron directamente dentro de este contenedor de toolchain, sin
  iniciar otro Docker. Logs locales (sin versionar): `build/e3-fast-unit.log`,
  `build/e3-fast-build-{native,asan}.log`, `build/e3-fast-ctest-{native,asan}.log`,
  `build/e3-fast-{native,asan}-disc.log`, `build/e3-fast-cached.log`,
  `build/e3-fast-{baseline,parallel}/{capture.log,time.txt}` y
  `build/e3-fast-comparison.json`.
- Builds `cmake --preset native-headless/native-asan` y `cmake --build --preset ...`:
  verdes. CTest completo sin disco: 49/47 pruebas, cero fallos y ocho omisiones de disco
  por preset. La selección de CI con disco y `ctest_no_skip.sh` en nativo pasa **12/12**,
  sin omisiones (415,58 s); la misma selección de clase B bajo ASan/UBSan pasa **12/12**
  sin omisiones (917,00 s), sin errores de sanitizadores. Ambas incluyen comparación
  estricta de título y estado de New Game; el fixture reutilizado tarda 0,13 s en ambos.
  Comando con disco para cada preset:
  `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh --preset PRESET
  --output-on-failure -R 'pack_disc|dw1_overlay|gte_tables_disc|scene_|reference_title|reference_newgame_state'`.
  La ejecución completa sin disco más la selección con disco cubren las 49/47 pruebas
  de cada preset; no quedan pruebas omitidas sin ejecutar.
- Validación local del YAML: únicamente `native-disc` y `native-asan-disc` declaran caché,
  heredan `dw1-data` y no declaran artifacts.
- Pendiente de validación externa: restauración de caché y `cached` en dos pipelines
  consecutivos de `main`. Esta ejecución no crea commits ni publica cambios.

## Spec Change Log

## Review Triage Log

| Capa | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| blind-hunter, edge-case-hunter, verification-gap | `--reuse` acepta trazas alteradas si existen y el sello coincide | low | Solo por edición manual; `run.py:379-387` ya usa la misma política (sello + existencia) y la caché de GitLab se restaura como zip completo. El arreglo agrega hashes por traza. Rechazado. |
| blind-hunter, edge-case-hunter | La huella no identifica el disco | low | `pack_disc` valida el disco contra `tools/port/manifest/us.tsv`; otro contenido falla antes de usar la referencia, y el disco del runner es fijo. Previo a la historia. Rechazado. |
| blind-hunter, edge-case-hunter | La huella no identifica el binario de PCSX-Redux | medium | Real: con `--reuse` en CTest, reconstruir la imagen local con otro PCSX-Redux deja reutilizar una referencia vieja. En CI la clave por `.gitlab-ci.yml` lo cubre. Incluirlo cambia la huella (excluido por el bloque congelado). Diferido. |
| blind-hunter, edge-case-hunter, verification-gap | Con `--bios`, `--reuse` exige el pin de OpenBIOS que la captura normal omite | low | Real: recaptura siempre una referencia válida del BIOS del dueño. Corrección directa: `args.bios or …` y caso en `test_reuse_owner_bios_identity`. Parche. |
| blind-hunter, edge-case-hunter | Sin exclusión mutua entre invocaciones sobre la misma salida | low | CTest ya usa `RESOURCE_LOCK newgame_capture` y `run.py` solo captura bajo ese lock; queda solo el uso manual concurrente, previo a la historia. Rechazado. |
| edge-case-hunter | Sello con bytes no decodificables rompe la reutilización | low | Real pero improbable; corrección directa comparando bytes (`read_bytes() == fingerprint.encode()`). Parche. |
| verification-gap | Nada detecta si se quita `--reuse` de `CMakeLists.txt` | low | Solo cuesta tiempo, no correctitud; el AC se verificó con `ctest -R reference_newgame_state` (0,13 s). Agregar un test de integración para un flag es complejidad nueva. Rechazado. |
| verification-gap | Nada detecta la pérdida de la caché entre pipelines | low | Mismo daño (tiempo); el AC se verifica con dos pipelines en `main`. Automatizarlo exige leer logs de GitLab desde CI. Rechazado. |
