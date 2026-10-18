---
title: 'Historia 3.2: Runner de Escenas y pantalla de título exacta'
type: 'feature'
created: '2026-10-17'
status: 'done'
baseline_commit: 'f4cb8559424bcc4961d95f591051a735f2dd11bc'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `run.py` solo verifica determinismo nativo; nada compara el port contra `reference_sha256`, y hoy la captura `title` del port difiere de la referencia en 10.674 píxeles (caja x 1..128, y 0..217). Causa verificada: el fondo es un `POLY_FT4` de 256 px con UV 0..255 (`renderPressStartToContinue`, `src/main/main.c:4163`); la referencia (SoftGPU de PCSX-Redux) reproduce `TITLE256.TIM` exacto en x < 128, mientras que la interpolación baricéntrica truncada de `port/backend/gpu_soft/gpu_soft.c:171` elige otros texeles. No es un problema de estado ni de ritmo.

**Approach:** Agregar `--compare` a `run.py`: compara RGB555 de la captura contra `reference_sha256`, y ante diferencia guarda una imagen de diferencias fuera del repo, imprime cantidad de píxeles y caja envolvente y sale ≠0; las excepciones `[[exceptions]]` del `.toml` (región + motivo) se descuentan y se listan. Corregir la interpolación de atributos del rasterizador para coincidir con la referencia, hasta que `title` pase sin excepciones.

## Boundaries & Constraints

**Always:** Comparación en RGB555 (cada canal del PPM `>> 3`); dimensiones distintas = fallo. Imágenes de diferencias y capturas solo en `build/` o `$DW1_REFERENCE_DIR`. Un cambio del rasterizador mantiene verdes los tests de píxeles y los SHA-256 de VRAM existentes, o actualiza cada SHA con la justificación (valor esperado recalculado de forma independiente) en el test. `scene_title` sin `--compare` conserva su comportamiento actual. Clase B salta con 77 sin disco.

**Never:** Excepciones en `title.toml` para cerrar la historia; cambios en `src/`/`include/`; tocar el capturador de referencia (`tools/port/reference/`) salvo leer el `.toml`; subir capturas o diffs al repo o a CI.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| HAPPY_PATH | `run.py title --build native-headless --compare`, Pack real | 2 corridas deterministas, RGB555 igual a la referencia; sale 0 | N/A |
| Diferencia | captura ≠ referencia | sale 1; imprime `N pixels differ, box x0..x1 y0..y1`; escribe `diff.ppm` (píxeles distintos resaltados) junto a las capturas | N/A |
| Excepción | diferencia toda dentro de una región declarada | sale 0; informe lista `exception <region>: <motivo> (N px)` | píxeles fuera de toda región: sale 1 como "Diferencia" |
| Sin hash | `--compare` y `.toml` sin `reference_sha256` | sale 1 nombrando la Escena | N/A |
| Sin referencia local | hash presente pero falta la PPM de referencia para generar el diff | sale 1 con el hash del port y el esperado; indica correr `capture.py` | N/A |
| `.toml` inválido | región fuera del frame o sin motivo | sale 1 antes de correr el juego | N/A |

**Decisiones (2026-10-17):** (1) Rasterizador en sala limpia por caja negra (opción B), empezando por el caso del título (opción C): la regla de interpolación se infiere de los píxeles de la referencia y del TIM, **sin leer el código de SoftGPU** (`build/pcsx-redux-src/src/gpu/` queda fuera de lectura; licencia del port sin cambios). La paridad general del rasterizador con escenas de prueba queda en `deferred-work.md`. (2) Spec completa sin dividir.

</frozen-after-approval>

## Code Map

- `tools/port/scenes/run.py` -- runner actual (2 corridas, `ppm_colors`, skip 77). Agregar `--compare`, carga de excepciones, conversión RGB555, diff y caja. Reutilizar la lectura P6.
- `tools/port/scenes/title.toml` -- `reference_sha256 = "5613236…260d"`; sin `[[exceptions]]`.
- `tools/port/scenes/test_runner.py` -- L1 con `subprocess.run` simulado; extender con los casos de la matriz.
- `tools/port/reference/capture.py` -- deja `build/reference/<id>/run-1.ppm` (o `$DW1_REFERENCE_DIR`); de ahí se lee la PPM de referencia para el diff (no se modifica).
- `CMakeLists.txt:397-444` -- `scene_title` (clase B); agregar `scene_title_compare` con `--compare` (skip 77 sin disco; si falta la PPM de referencia, correr `capture.py` primero en el mismo test o en CI).
- `.gitlab-ci.yml:67` -- incluir `scene_title_compare` en `native-disc` (después de `reference_title`, que genera la PPM).
- `port/backend/gpu_soft/gpu_soft.c:158-228` -- `edge`/`top_left`/`triangle`: barycéntrico con truncamiento para color y UV. Punto a corregir. `polygon()` (≈l.236) divide el quad en dos triángulos.
- `port/backend/gpu_soft/tests/` y `port/psyq/tests/` -- tests de píxeles y SHA-256 de VRAM que el cambio puede mover (ver comando de verificación).
- Dato verificado: `TITLE256.TIM` (`disks/us/ETCNA/`, no versionado) es 8 bpp, VRAM x 768, 160×256 palabras, CLUT (0,480). Referencia = TIM exacto en x 0..127; difiere en x 128..255 (estiramiento 255/256). Port difiere del TIM en x 1..255.

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/scenes/run.py` -- `--compare`, excepciones validadas, RGB555, `diff.ppm`, informe -- AC de la historia.
- [x] `tools/port/scenes/test_runner.py` -- casos de la matriz con PPMs sintéticas (igual, diferencia con caja conocida, excepción que cubre, excepción parcial, sin hash, sin PPM, `.toml` inválido).
- [x] `port/backend/gpu_soft/gpu_soft.c` -- interpolación de atributos (UV, y color si la misma regla aplica) inferida en sala limpia de la captura de referencia y del TIM (sin leer SoftGPU); documentar la regla y su evidencia en un comentario `ref:` -- FR-4.
- [x] `port/backend/gpu_soft/tests/` -- test L1 del quad del título (256 px, UV 0..255) con el mapeo de columnas esperado calculado aparte, más los SHA existentes actualizados con justificación.
- [x] `CMakeLists.txt`, `.gitlab-ci.yml` -- `scene_title_compare` en clase B y en `native-disc`.
- [x] `docs/port/reference.md` -- reemplazar "Dato para la historia 3.2" con el resultado; `docs/port/scenes.md` (nuevo, corto) -- formato `[[exceptions]]` y uso de `--compare`.

**Acceptance Criteria:**
- Given `title.toml` con el hash de referencia y el disco, when se corre `run.py title --build native-headless --compare`, then sale 0 y la captura RGB555 es idéntica a la referencia, sin excepciones.
- Given la misma corrida en `native-asan`, when se compara, then también coincide.
- Given el build `web`, when corre `web_scene_title`, then su SHA sigue igual al de `native-headless` (ahora el de la referencia).

## Verification

**Commands:**
- `tools/port/docker_run.sh python3 -I -B tools/port/scenes/test_runner.py` -- expected: OK.
- `ctest --preset native-headless` y `ctest --preset native-asan` en la imagen -- expected: verdes.
- `DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/scenes/run.py title --build native-headless --compare` -- expected: sale 0.
- `ctest --preset web -R web_scene_title` -- expected: verde.
- Pipeline de `main` tras el merge -- expected: `native`, `native-disc` (con `scene_title_compare`), `web` y `matching` verdes.

## Implementation Notes

- Implementado por Codex (sin commits). `run.py --compare` valida `reference_sha256`, la PPM local de referencia y las excepciones antes de correr el juego; compara RGB555, escribe `diff.ppm` y la caja envolvente.
- Rasterizador: `scan_uv` en `gpu_soft.c` calcula UV por intersecciones de aristas 16.16 (división truncada), gradiente horizontal y redondeo al más cercano; inferido en sala limpia del TIM y la PPM de referencia, sin leer SoftGPU. El color interpolado no cambia. Los SHA de VRAM existentes se conservan; test nuevo `title_columns` (256×240 muestras, oráculo por columnas).
- `scene_title_compare` (clase B) depende del fixture `title_reference` (`reference_title`); `native-asan-disc` deja de excluir `reference_title`.
- Verificación del orquestador (2026-10-18): rebuild de `native-headless` tras el último cambio de `gpu_soft.c`, `gpu` OK y `run.py title --build native-headless --compare` → `RGB555 reference match (0 excepted pixels)`; ASan con el mismo SHA `56132366…260d`. Evidencia: `docs/implementation-artifacts/evidence/story-3-2.log`.

## Spec Change Log

## Review Triage Log

Iteración 0 (`codex-merged`: blind-hunter, verification-gap, edge-case-hunter):

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `scene_title` y `scene_title_compare` comparten `build/port/<preset>/scenes/title/`; con `ctest -j` se pisan (blind, edge, verification-gap) | low | Real: ambos borran y escriben `run-N.*`. Los presets y CI no usan `-j`, pero un `ctest -j` local los rompe. **patch**: `RESOURCE_LOCK` en ambos tests (una propiedad) |
| 2 | `scan_uv` recalcula gradiente e intersecciones de aristas por píxel y por canal, con divisiones de 64 bits (blind) | medium | Real: `gpu_soft.c:178-205` se llama dos veces por píxel texturado; el costo pesa en web (NFR-2). **patch**: calcular gradientes por triángulo e intersección por fila, mismo resultado (los SHA de VRAM y `title_columns` lo fijan) |
| 3 | Sin evidencia de V con muestras verticales fraccionarias (blind) | low | Real: en el título V = y exacto. Ya cubierto por la entrada diferida de paridad general del rasterizador; se rechaza aquí |
| 4 | La regla inferida se aplica a todos los triángulos texturados sin evidencia para primitivas inclinadas, UV decrecientes o recorte (blind) | maybe-false | Los SHA de VRAM existentes siguen iguales, pero no hay referencia de esas primitivas. Lo resolvería una escena de prueba en el emulador. **defer** (medium, sin verificar) |
| 5 | Los modos negativos `invalid-region`, `no-reason`, `dimensions` aceptan retorno 0 en `test_runner.py` (blind, verification-gap, edge) | medium | Real: la rama `else` (`test_runner.py:138-140`) no excluye esos modos; quitar la validación no rompe el test. **patch** |
| 6 | Un fallo de preflight o de preparación del Pack deja el `diff.ppm` anterior (blind, edge) | low | Real: el `unlink` está después de la validación y del Pack (`run.py:113-131`). Corrección directa: mover el `unlink` antes. **patch** |
| 7 | El test de solapamiento no verifica que el píxel cuente una sola vez (verification-gap) | medium | Real: ambas regiones tienen el mismo motivo y sin el `break` el test sigue pasando. **patch**: motivos distintos, conteos `[2, 0]` |
| 8 | La referencia se lee dos veces (hash y píxeles); podría cambiar en el medio (edge) | low | Requiere que alguien reescriba `build/reference` durante la corrida; improbable. Rechazado |
