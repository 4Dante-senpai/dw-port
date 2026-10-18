---
title: 'Semilla de rand del BIOS real en el port y en OpenBIOS de referencia'
type: 'feature'
created: '2026-10-18'
status: 'in-review'
baseline_commit: '0d12c3dba037b247c1d6cffe2163de8a6b81fefc'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/port/reference.md'
  - '{project-root}/docs/port/scenes.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El port y el emulador de referencia arrancan `rand` con semilla 0 (OpenBIOS), pero el
BIOS real SCPH-1001 del dueño arranca con `0x24040001` (derivado el 2026-10-18: las 16 salidas
naturales de `newgame` son consecutivas del LCG y la semilla queda 206 llamadas antes, igual que con
OpenBIOS; `docs/port/reference.md`, "Resultado"). Toda escena que dependa de `rand` diverge de una
PS1 real desde la primera llamada.

**Approach:** El port adopta `0x24040001` como semilla de arranque. El Dockerfile parchea el
`s_currentSeed` de OpenBIOS a ese mismo valor al compilarlo, así CI y cualquier fork reproducen la
aleatoriedad del hardware real sin BIOS propietario. Una única comparación local con el BIOS del dueño
(fuera de CI) valida que OpenBIOS parcheado y SCPH-1001 producen el mismo estado y los mismos `rand`.

**Decisión del dueño (2026-10-18):** opción "semilla del BIOS real + OpenBIOS parcheado", elegida para
que nada dependa del BIOS de Sony al publicar.

**Decisión del dueño (2026-10-18), validación en CI:** opción A. La rama de la historia se pushea a
GitLab para que `images` publique la imagen; el digest nuevo se fija en la rama y solo con pipeline
verde se mergea a `main` (precedente 3.1; la rama queda en el remoto).

## Boundaries & Constraints

**Always:** El BIOS de Sony y sus capturas nunca entran al repo, a la imagen ni a CI; solo hashes y
valores medidos. El parche de OpenBIOS falla el build si el patrón no aparece (no parche silencioso).
Cambiar el binario de OpenBIOS invalida las referencias de estado cacheadas.

**Never:** Tocar `src/` o `include/`. Cambiar el LCG, el conteo de llamadas o el formato de trazas.
Usar `--bios` del dueño en tests o CI. Fijar en `newgame.toml` un hash de imagen que solo se obtuvo
con el BIOS del dueño.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Arranque del port | `psyq_libc_reset()` o init estática | primeras salidas `20346, 17442, 3669, 9391, 11839` | N/A |
| Referencia CI | OpenBIOS parcheado, `newgame --state` | 16 `rand` naturales `11315, 12212, 7201, 23373, …` (= dueño) | N/A |
| Caché vieja | `build/reference/newgame` hecho con OpenBIOS semilla 0 | huella distinta, se regenera | N/A |
| Parche roto | upstream cambia la línea de `s_currentSeed` | build de imagen falla | mensaje claro |

</frozen-after-approval>

## Code Map

- `port/psyq/libc.c:7-10,28-32` -- `BIOS_RAND_SEED 0u` y comentarios "provisional OpenBIOS"; cambiar a `0x24040001u` y citar la derivación.
- `port/psyq/tests/test_libc_heap_pad.c:31-56` -- golden de `0x24040001` ya existe (`4256336683u`); los checks de init estática/reset esperan `0, 21468` y pasan a `20346, 17442`.
- `tools/port/Dockerfile:96-115` -- etapa `pcsx-redux`; OpenBIOS sale de `src/mips/openbios` del SHA fijo. `kernel/libcmisc.c:175` es `static uint32_t s_currentSeed;` (`.bss`, en cero). Parchear entre checkout y `make -C src/mips/openbios`.
- `tools/port/scenes/run.py:212-219` -- `state_fingerprint(scene, bios_identity="openbios")`: la identidad literal no cambia con el binario de OpenBIOS; usar el SHA-256 de `openbios.bin` para que la caché local se regenere. Mantener coherencia con `capture.py:212-213` y con `run.py:371`.
- `tools/port/reference/capture.py:18,153,212` -- `OPENBIOS`, selección de BIOS e identidad del sello.
- `tools/port/reference/test_capture.py:284,387,416-457` -- tests de huella con `OPENBIOS` parcheado; ajustarlos a la identidad nueva.
- `CMakeLists.txt:444-450` -- `reference_newgame_state` y `scene_newgame_state` (clase B, disco).
- `docs/port/reference.md` ("Estado y semilla de rand", "Resultado"), `docs/port/scenes.md:105-107` (identidad del BIOS), `docs/STATUS.md` (bloqueos), `deferred-work.md` (entrada de 3.1).
- Evidencia local del dueño (no versionada): `build/reference-owner/newgame/run-1.state`, frame `48322f1c…e508`; comando en `reference.md`.

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/libc.c` -- semilla `0x24040001u`, comentario con fuente (SCPH-1001, 206 llamadas, `reference.md`) -- paridad con hardware real.
- [x] `port/psyq/tests/test_libc_heap_pad.c` -- init estática y reset esperan `20346, 17442` -- fija la semilla nueva.
- [x] `tools/port/Dockerfile` -- `sed` que reemplaza la línea de `s_currentSeed` por `= 0x24040001` y `grep -q` que falla si no quedó; comentario con la razón -- referencia CI sin BIOS propietario.
- [x] `tools/port/scenes/run.py`, `tools/port/reference/capture.py`, `tools/port/reference/test_capture.py` -- identidad del BIOS por defecto = SHA-256 de `openbios.bin` (fallback `openbios` si falta el archivo) -- invalida caché vieja.
- [x] Imagen local `dw-port` reconstruida (`DOCKER_CONTEXT=default tools/port/docker_build.sh`) y referencias `newgame` regeneradas -- base de la verificación.
- [x] `docs/port/reference.md`, `docs/port/scenes.md`, `docs/STATUS.md`, `docs/implementation-artifacts/deferred-work.md` -- semilla adoptada, parche, identidad, resultado de la comparación con el dueño; cerrar la decisión pendiente.
- [x] `.gitlab-ci.yml` -- digest de `dw-port` nuevo, fijado en la rama tras el job `images` (decisión A): `e79c381a…6f45` (job 387).

**Acceptance Criteria:**
- Given la imagen nueva, when se ejecuta `pcsx-redux` con su `openbios.bin` sobre `newgame --state`, then las dos corridas son idénticas y los 16 `rand` y las 15 variables en los 9 puntos coinciden con `build/reference-owner/newgame/run-1.state` (ignorando la columna de VBlank).
- Given la misma captura, when se compara el frame con `48322f1c…e508`, then el resultado (igual o distinto, y por qué) queda registrado en `reference.md`.
- Given la imagen nueva, when corre `reference_title`, then el hash sigue en `56132366…260d`.
- Given referencias regeneradas, when corren `scene_newgame_state` nativo y ASan y la suite con disco, then pasan sin skips.
- Given el build PS1, when corre `make compare`, then sigue idéntico (sin cambios en `src/`).

## Design Notes

Bit 31 de la semilla no afecta las salidas (`(s >> 16) & 0x7FFF` solo depende de los bits bajos), así
que `0x24040001` y `0xA4040001` son equivalentes; se elige `0x24040001`, que además es la palabra
MIPS `li a0, 1`, consistente con un valor residual en la RAM del BIOS real.

Para no invalidar la capa cacheada de PCSX-Redux (build en frío de horas), el parche va en un `RUN`
nuevo posterior a esa capa que modifica `libcmisc.c`, recompila solo OpenBIOS y reemplaza
`/out/share/pcsx-redux/openbios.bin`. Dentro del contenedor del implementador no hay Docker: la
reconstrucción de la imagen, la regeneración de referencias y la comparación con el BIOS del dueño
las ejecuta el orquestador en el host después de la implementación.

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && ctest --preset native-headless --output-on-failure'` -- expected: verde.
- `DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I -B tools/port/reference/test_capture.py` -- expected: verde.
- Suite con disco (`DW1_DISC_DIR`) nativa y ASan con `-R "reference_|scene_"` -- expected: verde, sin skips.
- Comparación con dueño: script de `reference.md` + diff de trazas sin columna VBlank -- expected: sin diferencias.
- `make compare` en `dw-build` -- expected: OK.

## Implementation Notes

- Implementación (Codex, contenedor): `libc.c`, test de `rand`, parche en capa propia del Dockerfile,
  identidad del sello por SHA-256 de `openbios.bin` (`run.py:bios_fingerprint`, `capture.py`),
  `test_capture.py` (caché invalidada al cambiar el binario) y `test_host.py` (`20346`).
- Matriz "Parche roto": cubierta por la ejecución del fragmento `sed`/`grep` sobre la fuente fijada
  (pasa) y sobre una declaración alterada (falla con el mensaje), `build/e3-openbios-patch.log`; el
  build real de la imagen aplicó el parche.
- Host: imagen reconstruida (`openbios.bin` `4ddea141…2189`); `title` `56132366…260d`; New Game
  idéntico a SCPH-1001 en estado, 16 `rand` y frame `48322f1c…e508`; nativo 49/49 y ASan 47/47 con
  disco sin omisiones; `make compare` OK.

## Spec Change Log

## Review Triage Log

| Capa | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| blind-hunter, verification-gap | `.gitlab-ci.yml:14` conserva el digest viejo | false | Tarea explícita pendiente del spec (decisión A): se fija tras `images` en la rama y antes del merge. |
| blind-hunter, verification-gap | El frame de New Game no se exige en `scene_newgame_state` | medium | Real pero previo: `run.py --state` solo informa el diff por diseño (`scenes.md:114`); el port difería en 403 px con semilla 0 y ahora en 402 (caja 87..217, 46..121). Diferido. |
| blind-hunter | Falta evidencia versionada de la validación | low | Real; corrección directa: `evidence/story-e3/` con logs de referencia, CTest y comparación con el dueño. Parche. |
| blind-hunter | Falta un chequeo reproducible de la derivación de 206 llamadas | low | Real pero improbable de afectar el uso diario; requiere script/test nuevo. Rechazado. |
| blind-hunter | `version.json` no identifica el parche | low | Real; el SHA-256 de `openbios.bin` ya está en `reference.md` y en el sello; agregar campos a un archivo que lee PCSX-Redux sin verificar su efecto suma riesgo. Rechazado. |
| edge-case-hunter | El BIOS puede cambiar entre la captura y el hash del sello | low | Requiere modificar el BIOS durante una corrida de 20 min; el guard agrega complejidad. Rechazado. |
