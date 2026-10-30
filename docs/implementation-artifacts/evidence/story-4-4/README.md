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
- `shell-play-chromium.json`: el contexto de control creado sin gesto queda
  `suspended`; el `AudioContext` de «Jugar» se construye con activación transitoria
  y queda `running`, sin navegación.

## Criterios sin verificar en esta sesión

- **Firefox** (todas las suites web): requiere la imagen `dw-port` o el job de CI.
- **Clase B** (`web_scene_title`, test «one click on Jugar reaches the title»):
  requiere `DW1_DISC_DIR` y el Pack real; salta con 77.
- **`native-asan`** y **`reference_capture_unit`**: falta el runtime ASan i686 y
  `pcsx-redux` en esta sesión. El cambio en `port/psyq/crt.c` está dentro de
  `#ifdef __EMSCRIPTEN__`.

Para cerrarlos, en el contenedor del proyecto:

```sh
tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web && ctest --preset web'
tools/port/docker_run.sh sh -c 'cmake --build --preset native-headless && cmake --build --preset web && cd build/port/web && DW1_DISC_DIR=/dw/game_rom/extracted ctest -R "pack_disc|native_title_capture|web_scene_title"'
tools/port/docker_run.sh sh -c 'cmake --preset native-asan && cmake --build --preset native-asan && ctest --preset native-asan'
```
