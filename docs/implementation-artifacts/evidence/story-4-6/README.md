# Evidencia de la historia 4.6

Verificación del 2026-10-30 en el contenedor del proyecto, con Chromium, Firefox y el disco real,
sobre `3fcf411`. Reemplaza a la de la sesión en la nube, que no tuvo disco ni Firefox.

- `settings-keyboard-<navegador>.json`: registro `PortSettings` v2 tras reasignar Cross a `KeyZ`;
  tras recargar, Z llega a `PadRead` como Cross y K ya no.
- `settings-migration-<navegador>.json`: base sembrada con `schema_version: 1` y un campo
  desconocido, y el registro v2 resultante: asignaciones, `stick_dpad` y el campo extra se
  conservan; el gamepad toma los valores por defecto.

Comandos, en `tools/port/remote_run.sh` con `DW1_DISC_DIR=/dw/game_rom/extracted`:
`make compare` idéntico; `ctest --preset native-headless` 85/85; `ctest --preset native-asan` 83/83;
`ctest --preset web` 47/47. `web_settings` son 38 casos, 19 por navegador; pasó también en tres
corridas seguidas. `web_scene_title` reasigna Start a `KeyZ`, recarga y pulsa «Jugar» con el Pack
real: Enter ya no sale del título y Z abre `runMainMenu`, en Chromium y Firefox.

La falla intermitente de `input` que registró la sesión en la nube venía de navegar con la escritura
de IndexedDB en curso; los tests ahora esperan el registro antes de recargar.

## Pendiente

- **Formato v1**: nunca se publicó; la migración se probó con una base sembrada.
- **Mando físico**: el remapeo de gamepad solo se probó con un mando simulado.
