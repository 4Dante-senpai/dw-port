---
title: 'Historia 4.6: Remapeo y opciones persistentes'
type: 'feature'
created: '2026-10-30'
status: 'done'
baseline_commit: 'b2d949af3f5590ebf2c0c6e0e64a1f8e4f4e2189'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-4-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-4-5-teclado-y-gamepad.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** El mapeo de 4.5 está fijo en constantes de la Shell y la opción "stick como cruceta" se pierde al recargar; el jugador no puede reasignar botones ni conservar opciones (FR-9).

**Approach:** Un registro `PortSettings` (JSON, claves `snake_case`, `schema_version`) en IndexedDB guarda los controles (teclado: botón PS1 → códigos; gamepad: botón PS1 → índices estándar) y `stick_dpad`. La Shell lo carga antes de arrancar, ofrece en Opciones reasignar cada botón PS1 con la próxima tecla o botón de gamepad y "Restaurar valores por defecto", y migra versiones anteriores en orden sin perder campos.

## Boundaries & Constraints

**Always:** el muestreo de input de 4.5 lee el mapeo cargado (mismo latch y bits de cable); los valores por defecto son exactamente los de `docs/port/controls.md`; asignar una tecla o botón a un botón PS1 la quita de cualquier otro botón del mismo dispositivo; cada cambio se guarda al instante en una transacción `readwrite`; la migración es una cadena `v1 → v2` que conserva campos desconocidos; una base con `schema_version` mayor que la soportada o un registro ilegible no se sobrescribe: la Shell usa los valores por defecto y muestra un aviso; si IndexedDB falla, el juego arranca con valores por defecto y un aviso.

**Never:** volumen y opciones de video (4.7, épica 5); Memory cards (4.8); service worker (4.9); cambios en `src/` o en C (el remapeo vive en la Shell); remapeo durante la partida (las Opciones están en el lanzador).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Reasignar teclado | Cross → `KeyZ`, recargar | Z envía Cross; K ya no | — |
| Reasignar gamepad | Circle → índice 16, recargar | botón 16 envía Circle; 1 ya no | — |
| Conflicto | asignar `KeyK` a Square | K solo envía Square | — |
| Restaurar | tras reasignar, "Restaurar valores por defecto" | mapeo de `controls.md` en la base y en el juego | — |
| Opción | marcar "stick como cruceta", recargar | sigue marcada | — |
| Migración | base sembrada `schema_version: 1` con campos extra | registro v2 con asignaciones, opción y extras conservados | — |
| Versión futura | `schema_version: 99` | valores por defecto, aviso, registro intacto | no escribe |
| Registro ilegible | valor no objeto | idem | idem |

</frozen-after-approval>

## Code Map

- `port/web/dev/index.html` -- `KEYS`/`PAD_BUTTONS` fijos de 4.5 y `inputControls()` (lee `#stick-dpad`); `start()` (punto de carga antes de `launch`/`boot`); `#options` (lugar del editor).
- `port/web/tests/input.spec.cjs` -- helpers `fakePad`, `setPad`, `expectLatched`, `padRead`: reusar en la suite nueva.
- `docs/port/controls.md` -- tabla web por defecto (fuente de los valores por defecto).
- Arquitectura (AD-13): `PortSettings` con `schema_version` en IndexedDB, claves en inglés `snake_case`.
- No hubo `PortSettings` publicado antes de esta historia: el formato `v1` (teclado código → botón y `stick_dpad`) se define aquí como versión anterior para la cadena de migración y su test; se documenta como tal.

## Tasks & Acceptance

**Execution:**
- [x] `port/web/dev/index.html` -- carga/guardado/migración de `PortSettings` (base `dw1`, almacén `settings`, clave `port_settings`); mapeo de input derivado de los ajustes; editor en Opciones (una fila por botón PS1 con teclas y botones actuales, "Reasignar", captura de la próxima tecla o botón de gamepad, `Escape` cancela), "Restaurar valores por defecto", `stick_dpad` persistente, avisos.
- [x] `port/web/tests/settings.spec.cjs`, `CMakeLists.txt` -- suite `settings` clase A con toda la matriz; CTest `web_settings`.
- [x] `port/web/tests/input.spec.cjs`, `port/web/tests/input_helpers.cjs` -- seguir verde con el mapeo por defecto cargado de los ajustes; helpers compartidos.
- [x] `port/web/tests/title.spec.cjs`, `CMakeLists.txt` -- clase B: Start reasignado, recarga y «Jugar» con el Pack real; `web_scene_title` pasa a 1200 s porque cada caso paga 123 s de arranque por navegador.
- [x] `docs/port/controls.md`, `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-6/` -- esquema `PortSettings` v1/v2, remapeo, cómo correr, evidencia y pendientes.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_settings` y `web_input` pasan en Chromium y Firefox.
- Given un botón reasignado y la página recargada, when el test lo pulsa con el juego corriendo, then `PadRead` refleja el botón PS1 reasignado.
- Given el Pack real, when se reasigna Start y se pulsa en el título, then el juego abre el menú y la tecla anterior ya no lo hace (`web_scene_title`, marca `runMainMenu` de 4.5).

## Design Notes

```json
{"schema_version": 2, "stick_dpad": false,
 "controls": {"keyboard": {"cross": ["KeyK"], "up": ["ArrowUp", "KeyW"]},
              "gamepad": {"cross": [0], "l2": [6]}}}
```

`v1`: `{"schema_version": 1, "stick_dpad": bool, "keyboard": {"KeyK": "cross"}}`; `v1 → v2` invierte el mapa de teclado, toma el gamepad por defecto y conserva el resto. Los índices de gamepad cuentan pulsados con `value > 0,5` (regla de gatillos de 4.5, válida para botones digitales).

## Verification

**Commands:**
- `python3 port/web/tests/run.py build/port/web settings` y `... input` -- expected: todos los casos pasan.
- `ctest --preset web` -- expected: verde; clase B saltada.

## Spec Change Log

- 2026-10-30, integración local: la rama cloud dejaba sin implementar la clase B y sin Firefox. Se agregaron el caso de Start reasignado con el Pack real, `?frames=0` y los ajustes por la presentación de 4.5. KEEP: `PortSettings`, migración y editor de la rama cloud.

## Review Triage Log

Revisión Codex (`codex-merged`) del diff contra `main` tras integrar la rama cloud.

| Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|
| `Tab` recibe `preventDefault` durante la captura | medium | patch | Bloqueaba la navegación que el comentario promete; filtrado antes de cancelar el evento. Test nuevo. |
| Ctrl+Z asigna `KeyZ` durante la captura | medium | patch | Las teclas con Ctrl, Alt o Meta siguen siendo atajos, igual que en `inputControls`. Test nuevo. |
| `controls` desconocido en v1 se pierde | low | rechazado | v1 nunca se publicó y `controls` no es campo suyo; no hay registros reales. |
| Destino v1 inválido se descarta y sobrescribe el original | medium | patch | Quitar la condición hace que la migración falle y el registro quede protegido. Test nuevo. |
| Campo extra en un mapa rompe `assign` | low | patch | Recorre solo los botones conocidos. |
| Botón sostenido en otro mando | low | rechazado | Requiere dos mandos en la captura; soltar y pulsar lo resuelve. |
| Listas con huecos pasan la validación | low | rechazado | Solo con un registro hecho a mano. |
| `onblocked` deja una apertura pendiente | false | rechazado | La versión es fija en 1, sin upgrade concurrente; el Web Lock impide otra instancia. |
| Cambios sin escritura no probados en la sesión | medium | patch | Los casos de registros protegidos arrancan el juego sin navegar y comprueban `KeyZ`. |
| Fallos de escritura sin probar | medium | patch | Dos casos con `put` que lanza: aviso, registro intacto y mapeo en memoria. |
| Conflicto de gamepad sin prueba | medium | patch | Test nuevo con persistencia y `PadRead`. |
| Umbral analógico en índices reasignados | low | patch | El caso de gamepad cubre 0,5 con `pressed` y 0,6 sin él. |
