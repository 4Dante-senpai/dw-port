# Triage de fallos del playtest (Historia 6.10)

Cada fila es un fallo del soak, del bot o de una sesión real. «Commit» queda como
`(sin commit)` hasta que el orquestador registre la corrección. Los comandos de
reproducción usan archivos privados bajo `build/`, salvo los tests de regresión, que
son sintéticos y están versionados.

| # | Síntoma | VBlank | Overlay | Reproducción | Causa | Commit | Regresión |
|---|---------|--------|---------|--------------|-------|--------|-----------|
| 1 | `port: no frame in 30 virtual seconds`, salida 3 | 34.200 (última entrada del bot: `31280 0008`) | 11 (`SHOP_REL`; antes 7 `MOV_REL`) | `tools/port/docker_run.sh sh /dw/build/port/verification-6-11/benchmark-soak/trial-0000/build-1/reproduce.sh` | El juego estaba en pausa legítima. `handlePause` (`src/main/utils2.c`) hace un bucle de `PadRead(1)` sin `VSync` hasta el siguiente Start; el bot lo pulsó otra vez recién en el VBlank 53.200. El watchdog solo mira los frames presentados y mató la pausa a los 30 s virtuales. La hipótesis anterior (OT cíclica en `DrawOTag`) era falsa: el último `DrawOTag` termina en 5.050 paquetes. El juego original no se detiene en una pausa larga. | (sin commit) | `playtest_pause_34200` (inputs sintéticos `tools/port/playtest/regress/pause_34200.inputs.txt`, 35.000 VBlanks, digest `37015fdd…98f9`). Sin la corrección del watchdog el test falla con salida 3 en el VBlank 34.200 (verificado 2026-11-16, release); con ella pasa y `watchdog_pad_poll` (L1) |
| 2 | `web_scene_title_audio`: el PCM del worklet difiere del nativo | frame estéreo 5.683.263 | `main` | `ctest --preset web -R web_scene_title_audio` (ver más abajo) | La 6.9 hizo que `plat_input_vblank` (web) cuente los VBlanks de un replay desde el frame 0 (`port_playtest_vblank`). Los inputs resueltos de la Escena (`replay.txt`, `--resolved-inputs`) son absolutos desde el arranque, así que el desfase de unos VBlanks movió una pulsación y el juego arrancó un sonido más tarde (silencio en la web desde el frame 5.683.263 y desplazamiento posterior). Nativo no cambió, por eso Redux seguía pasando. | (sin commit) | `web_scene_title_audio` (Chromium y Firefox) |
| 3 | Release: `crash: signal 8` (SIGFPE). ASan: `particle.c:144:26: runtime error: division by zero` | 45.000 (semilla 2), 66.120 (semilla 3), 151.980 (semilla 1) | 7 y 11 | `build/port/playtest/soak/20261116T002418Z/trial-0001/build-0/reproduce.sh` (privado; soak del 2026-11-16, 4 semillas × 540.079) | `tickHealingParticles` divide `512 / (i * 13 / 4)` y con `i = 0` el divisor es 0. El original no tiene chequeo y el `div` de la PS1 no traps (LO = -1). En x86 era SIGFPE. | (sin commit) | `playtest_healing_div0` (inputs sintéticos recortados de la semilla 2, 45.120 VBlanks, digest `00cda6fa…6dbe`). Sin la rama `DW1_PORT` de `particle.c` el test falla con señal 8 en el VBlank 45.000 (verificado 2026-11-16, release); con ella pasa |
| 4 | ASan: `game_menu.c:3417:21: index 61 out of bounds for type 'EvoChartEntry[61]'` | 68.820 (semilla 4, solo ASan; el release no falla porque lee memoria vecina) | 7 y 11 | `build/port/playtest/soak/20261116T002418Z/trial-0003/build-1/reproduce.sh` (privado) | El gráfico de evolución recorre `i < 0x3e` (62) sobre `EVO_CHART_BOXES[61]`. En la PS1 la entrada 62 son los primeros 8 bytes de `MAIN_D_8012472C` (siguiente símbolo, la entrada tiene `posX = 0xff6e` (-146) y `posY = 0xffbd` (-67), y el borde se dibuja en `(posX - 1, posY - 1)` = `(-147, -68)`, fuera de pantalla). | (sin commit) | `playtest_evo_chart_oob` (semilla 4 recortada, 68.880 VBlanks, digest `05312d89…133d`; falla solo bajo ASan sin el cambio) |

## Corrección 1

`PadRead` marca un flag (`port_pad_polled_take`, `port/psyq/libetc.c`) y el watchdog
(`port/psyq/crt.c`) lo trata como progreso. No cambia `src/`. El plazo de 30 s sigue
siendo el mismo para cualquier otra espera sin frames.

## Corrección 2

`--replay-absolute` (`port/host/web/main.c`, `port/host/web/input.c`) hace que el replay web cuente
desde el arranque; la Shell lo pasa con `?replay=1` (inputs de Escena). Las grabaciones de
`playtest` siguen contando desde el frame 0. La spec agrega a su JSON las muestras alrededor de la
primera diferencia y la cantidad de frames distintos.

## Corrección 3 y 4

Ambas viven en `src/` detrás de `DW1_PORT` (`docs/port/src-changes.md`; `preproc_check` sin
diferencias). Los otros cuatro fallos del soak de 2026-11-16 (3 SIGFPE del release y 3 de UBSan
con la misma causa) son la fila 3: en cada semilla el primer fallo ocultaba a los siguientes, así
que se repite el soak completo hasta que no queden fallos.

## Soak limpio

Soak del 2026-11-16 (`20261116T020359Z`) con las cuatro correcciones: 8 corridas, 0 fallos, 20 horas
virtuales, digests iguales. No hay filas abiertas del soak, pero la cobertura es parcial: solo se visitaron los overlays 7, 10 y 11. `BTL` (y los otros 12) y la Escena de combate siguen abiertos.
