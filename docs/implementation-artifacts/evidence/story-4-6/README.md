# Evidencia de la historia 4.6

Verificación del 2026-10-30 en la sesión en la nube, sin Docker, sin disco y sin
Firefox (errores exactos en `../story-4-4/docker-firefox-2026-10-30.md`).

- `settings-keyboard-chromium.json`: registro `PortSettings` v2 tras reasignar Cross a
  `KeyZ`; tras recargar, Z llega a `PadRead` como Cross y K ya no.
- `settings-migration-chromium.json`: base sembrada con `schema_version: 1` (con un campo
  desconocido) y el registro v2 resultante: asignaciones, `stick_dpad` y el campo extra
  se conservan; el gamepad toma los valores por defecto.

Comandos (Chromium), tras la revisión: `settings` 14/14 (tres corridas), `input` 8/8
(seis corridas con salida completa), `shell` 10/10, `boot` 4/4, `launcher` 2/2, `io` 6/6,
`disc` 19/19. Antes de la revisión, una corrida de `input` dio 7/8 con la salida truncada;
no se reprodujo en las 11 corridas siguientes.

## Pendiente

- **Firefox**: suites `settings` e `input`.
- **Juego real (clase B)**: que el juego reaccione a un botón reasignado (por ejemplo,
  Start en el título); requiere el Pack real y una señal de escena posterior al título.
- **Formato v1**: nunca se publicó; la migración se probó con una base sembrada.
- **Falla intermitente de `input`** (1 corrida, sin reproducir en 11): queda para observar en CI.
