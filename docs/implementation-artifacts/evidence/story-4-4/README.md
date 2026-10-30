# Evidencia de la historia 4.4

Verificación del 2026-10-30 en una sesión en la nube sin Docker ni disco. Se
instalaron localmente emsdk 6.0.10 y `@playwright/test` 1.64.0; el Chromium
preinstalado reemplaza al de Playwright y **Firefox no pudo instalarse** (la
política de red bloquea `cdn.playwright.dev`). Los JSON contienen solo contadores
y tiempos; ningún byte del disco.

`verification.json` lista comandos, resultados y lo que quedó sin verificar.

- `shell-pause-chromium.json`: tasa medida antes y después de la pausa
  (59,83 y 59,79 VBlank/s frente a 60,0087), VBlanks quietos durante 2 s de pausa,
  0 resincronizaciones y 1 pausa.
- `shell-visibility-chromium.json`: pestaña oculta retiene el juego; al volver
  visible reanuda; con «Pausa» activa, volver visible no reanuda; 0
  resincronizaciones y 2 pausas.
- `shell-stall-chromium.json`: un frame atrasado 500 ms (`Module.stall`) cuenta una
  resincronización y el ritmo sigue en 59,8 VBlank/s, sin ráfaga de recuperación.
- `shell-play-chromium.json`: el contexto de control creado sin gesto queda
  `suspended`; el `AudioContext` de «Jugar» se construye con activación transitoria
  y queda `running`, sin navegación.

Los 59,8 VBlank/s de esos JSON son un artefacto de la ventana de 2 s, no un desvío del ritmo.

## Verificación en el contenedor del proyecto

Con disco, Firefox y PulseAudio, en la imagen `dw-port` (2026-10-30):

- Verificación final sobre el merge con `main` (commit `83b71a3`): `make compare` idéntico; `ctest --preset native-headless` 85/85; `ctest --preset native-asan` 83/83; `ctest --preset web` 45/45, incluidos `web_shell` y `web_scene_title` en Chromium y Firefox.
- «Jugar», con sus argumentos reales, llegó a `runLandingScreen` en 123 s en ambos navegadores,
  con `AudioContext` en `running`, `#screen` con imagen y 0 resincronizaciones.
- `pacing-100s.json`: 60,008 a 60,014 VBlank/s en ventanas de 100 s, antes y después de pausar,
  con 0 resincronizaciones. El juego sin Pack avanza a saltos de hasta 187 VBlanks, por eso una
  ventana corta oscila.
