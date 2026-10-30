---
title: 'Historia 4.6: Remapeo y opciones persistentes'
type: 'feature'
created: '2026-10-30'
status: 'in-review'
baseline_commit: '16aa1942d88c717d106346c2b83778ecdcaceb4c'
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
- [x] `port/web/tests/input.spec.cjs` -- seguir verde con el mapeo por defecto cargado de los ajustes.
- [x] `docs/port/controls.md`, `docs/port/setup.md`, `docs/implementation-artifacts/evidence/story-4-6/` -- esquema `PortSettings` v1/v2, remapeo, cómo correr, evidencia y pendientes.

**Acceptance Criteria:**
- Given `ctest --preset web` sin disco, when corren las suites, then `web_settings` y `web_input` pasan en Chromium y Firefox.
- Given un botón reasignado y la página recargada, when el test lo pulsa con el juego corriendo, then `PadRead` refleja el botón PS1 reasignado.
- Given el Pack real, when se reasigna Start y se pulsa en el título, then el juego reacciona (pendiente sin disco).

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
