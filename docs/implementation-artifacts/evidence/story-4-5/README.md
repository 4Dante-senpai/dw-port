# Evidencia de la historia 4.5

Verificación del 2026-10-30 en la sesión en la nube, sin Docker, sin disco y sin
Firefox (ver `../story-4-4/docker-firefox-2026-10-30.md` para los errores exactos).
Los JSON contienen solo valores de `PadRead`, VBlanks y tasas.

- `input-keyboard-chromium.json`: las 18 teclas del mapeo (14 botones, cruceta con
  flechas y WASD) llegan al valor de `PadRead` con el bit esperado y se limpian al soltar.
- `input-gamepad-chromium.json`: los 14 botones del gamepad estándar simulado.
- `input-hotplug-chromium.json`: con un mando conectado y desconectado en la ventana,
  60,0 VBlank/s, 0 resincronizaciones, 0 pausas.

Comandos (Chromium): `run.py ... input` 8/8 (tres corridas seguidas tras la revisión), `shell` 10/10,
`boot` 4/4, `launcher` 2/2, `io` 6/6, `disc` 19/19; `ctest --preset native-headless`
80/80 (sin `reference_capture_unit`, que necesita `pcsx-redux`, ni los dos
`preproc_check`, ya verdes en esta sesión).

## Pendiente

- **Firefox**: toda la suite `input` (y el resto de las suites web).
- **Juego real (clase B)**: que el juego reaccione al input (por ejemplo, Start en el
  título). Sin disco el juego corre sin datos y no hay una señal de escena después de
  `runLandingScreen` para afirmarlo; hace falta el Pack real y una marca de escena o un
  estado del juego que cambie con Start. No se escribió un test que no pueda validarse.
- **Mando físico**: solo se probó un gamepad simulado con el mapeo estándar W3C.
