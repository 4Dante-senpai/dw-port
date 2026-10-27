# Calibración del reloj — historia 3.10 completada

Los quince puntos de las cuatro escenas estrictas tienen **diferencia cero**
con las referencias nuevas. Las tablas antes/después y por fuente incluyen
ambas películas y el mapa con las entradas sostenidas. Newgame, audio y
tarjetas pasan con los guiones nuevos. La verificación remota final completó
las cuatro suites, serialmente y sin omisiones, y la prueba de pared de diez
minutos. Las constantes sin medición por operación conservan `[hypothesis]`.

## Período medido en Redux

El ejecutable final, reconstruido con los parches declarados en el Dockerfile,
tenía SHA-256 `56659c5fd50de2f1c1365cd50b9e553bd92100cefd09cbac06d349a202d63a2d`.
La imagen actual incorpora ese parche de dithering y su ejecutable instalado
es `59e9d967691373faf6591c59bd52d5a8ec0c08bebf7122b78e224688e0124561`;
se usa tal cual, sin reconstruir Redux.
El BIOS sembrado conserva SHA-256
`4ddea14132380f1aa6c303e45b417e1ead628d5c8c78ad678dcf2f9404832189`.
`tools/port/reference/measure_clock.lua` ejecuta un programa sintético, sin
BIOS ni disco, que configura GP1(08h) en NTSC 320×240 sin entrelazado. Descarta
el campo inicial parcial y suma 256 intervalos de `GPU::Vsync` en ciclos de
`PCSX.getCPUCycles()`. Dos procesos nuevos produjeron lo mismo:

| Corrida | Intervalos | Ciclos totales | Ciclos por VBlank |
|---|---:|---:|---:|
| 1 | 256 | 144485888 | 564398 |
| 2 | 256 | 144485888 | 564398 |

Las muestras individuales alternan 564396/564400 ciclos por la granularidad
de observación al terminar instrucciones. El promedio sobre 256 intervalos
es exacto: `33868800 / 564398 = 60,0087172527 Hz`. Es una medición del
emulador fijado, no una afirmación sobre la consola física.
`clock.h` usa la razón reducida `282199/16934400` segundos y SDL usa esa
misma razón. El acumulador evita multiplicar todos los VBlanks por un
numerador grande; el test cubre 24 horas y el límite de `uint32_t`.

Reproducción desde la raíz (repetir con un directorio portable nuevo):

```sh
mkdir -p /tmp/dw1-clock-probe
xvfb-run -a pcsx-redux -no-ui -portable /tmp/dw1-clock-probe \
  -interpreter -softgpu -debugger -noupdate -no-webserver -no-gdb \
  -dofile "$PWD/tools/port/reference/measure_clock.lua" -run
```

Evidencia final: `build/calibration/clock-probe-final-{1,2}.log`. Ambos
procesos nuevos conservan exactamente el período medido antes de reconstruir.
No se incorporan datos del disco.

## Desglose de Redux y cierre del mapa

`hardware.lua` observa escrituras reales de registros CD/GPU/MDEC, comandos,
parámetros y DMA 0–3; registra IRQ/ACK del CD y el intervalo entre la última
lectura DMA ocupada y la primera terminada. No suma intervalos de dispositivos
que se superponen. `hardware.py` valida y resume el JSONL. La captura se ejecuta
dos veces, en serie; la reutilización exige igualdad de las trazas temporales
y de hardware. La captura detallada `build/calibration/map-irq/` conserva:

- `hardware.jsonl`: SHA-256 `d5e4da1b412e5aa53cdfaee5ff7e1ad9f562c81faab28b9e87bb0ff170c349f7`; dos corridas idénticas, 139946833 bytes cada una.
- `timing.jsonl`: SHA-256 `da700284745f558bd44e59c692b40769bb04fee44c6e302d8fde10625c596209`; dos corridas idénticas.
- `state`: SHA-256 `44be862f461ab33636d3099cb3fdd689b9fa6b248981e78d5b28129fb156ace4`; dos corridas idénticas.

Los ámbitos exclusivos atribuyen VLC a CPU y las consultas `VSync(-1)`
a PsyQ. Las ocupaciones DMA se consultan aparte, sin doble contabilización.

El breakpoint real de `loadMap` entra en el ciclo absoluto 5737917881.
`checkArenaMap` entra en 5806864029 y el retorno a su llamador ocurre en
5806864132: **103 ciclos después, en el mismo VBlank**. El candidato queda
confirmado; la ventana de un VBlank incluye ese retorno. No es una hipótesis.

Mediciones por operación que justifican la forma del modelo:

| Operación | Observación Redux | Modelo |
|---|---|---|
| DMA GPU, 128 palabras | terminado antes de 109 ciclos | 7 ciclos por bloque de 16 palabras |
| DMA GPU, 2048 palabras | ocupado a 796; terminado a 2765 | 896 ciclos |
| ClearImage directo → DrawSync | retorno de DrawSync a 245 ciclos | sin costo por píxel; espera DMA |
| DMA MDEC, 2112 palabras | ocupado a 8744; terminado a 18790 | 2 ciclos por byte de salida, 16896 |
| CD | comandos/IRQ y fuente fijada de Redux | FIFO 797 µs; inicio 20 ms; seek 1 ms + 3 µs/LBA; cambio de velocidad 650 ms |
| Sectores CD | programación entera en `cdrom.cc` | 13333/6666 µs a 1×/2× |
| Pause CD | estado del transporte | 70/35 ms leyendo; 200 µs inactivo |
| Tarjeta, 74 transacciones SIO0 | lectura 188714..189025 ciclos; escritura 184720..184743 | alternancia de puertos por VBlank; fase de terminación según operación |
| VLC | ramas del algoritmo nativo y fuente fijada de Redux | BIAS=2 por instrucción, +1 por acceso RAM; contrastado con los puntos estrictos de inicio y fin |

Los costos de CPU de comandos CD, retorno de tarjeta y otras constantes sin
medición suficiente conservan `[hypothesis]`. Ninguna constante depende de
la Escena. La geometría de directorios ISO se usa solo con el hash del
manifest USA verificado; contiene metadatos, no contenido del disco.


La captura de comandos completa se resume automáticamente en
`build/calibration/map-irq/hardware-summary.json`. Por tramo, los conteos
CD usan el byte de comando hexadecimal; DMA informa palabras, no tiempo
sumado. Así se distingue CPU de espera y de ocupación de cada dispositivo.

| Fin del tramo | Comandos CD (hex: cantidad) | DMA GPU palabras | DMA MDEC entrada/salida palabras | Decodificaciones MDEC |
|---|---|---:|---:|---:|
| MOV_playMovie | 01:2, 0a:2, 0c:2, 09:14, 02:14, 0e:8, 06:14 | 22784 | 0 / 0 | 0 |
| runLandingScreen | 02:4, 06:3, 09:4, 0e:3, 1b:1 | 1900800 | 60544 / 1900800 | 45 |
| runMainMenu | 02:3, 06:3, 09:3 | 31328 | 0 / 0 | 0 |
| newGameScene | 02:1, 06:1, 09:1 | 22208 | 0 / 0 | 0 |
| newGameStateMachine | 02:3, 06:3, 09:3 | 4160 | 0 / 0 | 0 |
| setupNameSelectorBox | — | 2848 | 0 / 0 | 0 |
| setupNameSelectorBox#2 | — | 2480 | 0 / 0 | 0 |
| unloadNewGameScene | — | 2608 | 0 / 0 | 0 |
| loadMap | 01:1, 0a:1, 0c:1, 02:13, 0e:5, 06:12, 09:13, 1b:1 | 137953312 | 19305664 / 137913600 | 3265 |
| checkArenaMap | 02:2, 06:2, 09:2 | 49152 | 0 / 0 | 0 |

### CPU e interrupciones: medición por operación

La captura `build/calibration/map-irq/` repite las trazas de IRQ de CD:
898 ciclos de ACK, 863 de COMPLETE, 2273 por sector y 3634 en el último
sector, que además emite Pause. El despacho BIOS añade 1706 ciclos. Una
búsqueda ISO sin trabajo concurrente difiere 2607 ciclos sin este servicio:
2604 corresponden al ACK y 3 al jitter observado en `CdIntToPos` (133/136).
El reloj conserva la fecha del siguiente sector y añade las interrupciones
al trabajo de CPU pendiente, sin reducir la velocidad del transporte.

`post-movie-probe` mide `ResetGraph(3)` en 212790 ciclos. Se restan los
64 de entrada y 59 de `ResetCallback`: quedan 212667 de CPU para el mismo
reinicio de cola/buffers sin reset de hardware. El modo 0 conserva su
medición independiente.

`file-probe` mide `SsVabOpenHeadSticky` en 7929/8477 ciclos para cabeceras
con 18/27 muestras, versión 7, 128 slots y dos programas activos. El asm
cuenta 18 ciclos por slot, 2 adicionales por programa activo, 4096 para el
recorrido fijo de 256 posiciones y 62/60 por muestra par/impar, más 301
fijos y la entrada de 64. Predice ambas mediciones exactamente. No se
aplica el modelo a la rama antigua de 64 slots sin medición equivalente.

El probe histórico `reset-vab-native` alcanzó los diez puntos del mapa
con diferencia cero. La aceptación vigente requiere repetirlo con los
guiones de entrada sostenida; no se aplica un offset al latch del pad.

## Antes/después por punto de control

El baseline es `d79a22a49dda0c77b10478298c4b6c26555b3d0c`, instrumentado
para observar costos exclusivos y usar el mismo origen: primer retorno de
`VSync`, incluidas las consultas `VSync(-1)` de `PutDrawEnv`. Conserva el
modelo anterior y su tasa 59,94 Hz. La instrumentación y los resultados están
en `build/calibration/baseline-observation.patch` y
`build/calibration/baseline-measured/`; no se modificó el baseline publicado.
La columna «Antes» se midió con los guiones anteriores al cambio a Start
sostenido; el después y Redux usan las pulsaciones sostenidas autorizadas. En los tramos interactivos, la
diferencia incluye ese cambio de entrada y no mide solo el costo del reloj.

| Escena / punto | Redux | Antes | Diferencia antes | Actual | Diferencia actual |
|---|---:|---:|---:|---:|---:|
| title: runLandingScreen | 7820 | 7508 | -312 | 7820 | 0 |
| movie0_skip: MOV_playMovie | 482 | 196 | -286 | 482 | 0 |
| movie0_skip: runLandingScreen | 642 | 353 | -289 | 642 | 0 |
| movie0: MOV_playMovie | 482 | 196 | -286 | 482 | 0 |
| movie0: runLandingScreen | 7820 | 7508 | -312 | 7820 | 0 |
| map_load: MOV_playMovie | 482 | 196 | -286 | 482 | 0 |
| map_load: runLandingScreen | 642 | 353 | -289 | 642 | 0 |
| map_load: runMainMenu | 848 | 518 | -330 | 848 | 0 |
| map_load: newGameScene | 1138 | 808 | -330 | 1138 | 0 |
| map_load: newGameStateMachine | 1319 | 945 | -374 | 1319 | 0 |
| map_load: setupNameSelectorBox | 1595 | 1221 | -374 | 1595 | 0 |
| map_load: setupNameSelectorBox#2 | 1976 | 1464 | -512 | 1976 | 0 |
| map_load: unloadNewGameScene | 2501 | 1848 | -653 | 2501 | 0 |
| map_load: loadMap | 9413 | 8631 | -782 | 9413 | 0 |
| map_load: checkArenaMap | 9535 | 8735 | -800 | 9535 | 0 |

Desglose **por tramo**, en ciclos, en el orden CD / GPU / MDEC / tarjeta /
PsyQ / espera / CPU / SPU. Redux usa ámbitos exclusivos de API con CPU
residual; VLC se atribuye a CPU. Las ocupaciones DMA se registran aparte,
sin sumar trabajo de dispositivos concurrentes. El port atribuye por el
ámbito activo del reloj, por lo que los costos de servicio de IRQ durante
una llamada bloqueante pueden quedar en el dispositivo que inició la espera.
Las columnas suman el tiempo transcurrido; la atribución se complementa con
la tabla de comandos y DMA para identificar cada operación.

| Escena / fin del tramo | Redux | Antes | Actual |
|---|---|---|---|
| title: runLandingScreen | 656563853 / 46184846 / 10865654 / 83925606 / 168501107 / 1317723959 / 2129628223 / 106520 | 3536052307 / 690880 / 0 / 0 / 12625728 / 690323468 / 2506306 / 568064 | 73827226 / 0 / 0 / 79050700 / 10491306 / 0 / 4250120102 / 4624 |
| movie0_skip: MOV_playMovie | 51171860 / 4495 / 0 / 83925606 / 133012457 / 0 / 3479201 / 0 | 108605504 / 0 / 0 / 0 / 12224 / 0 / 2506306 / 0 | 61556739 / 0 / 0 / 79050700 / 10496 / 0 / 130918297 / 0 |
| movie0_skip: runLandingScreen | 26747412 / 574651 / 134100 / 0 / 22239306 / 19529690 / 20913890 / 106520 | 82470611 / 690880 / 0 / 0 / 203712 / 4816261 / 0 / 568064 | 12284405 / 0 / 0 / 0 / 138794 / 0 / 77861673 / 4509 |
| movie0: MOV_playMovie | 51171860 / 4495 / 0 / 83925606 / 133012457 / 0 / 3479201 / 0 | 108605504 / 0 / 0 / 0 / 12224 / 0 / 2506306 / 0 | 61556739 / 0 / 0 / 79050700 / 10496 / 0 / 130918297 / 0 |
| movie0: runLandingScreen | 605391993 / 46180351 / 10865654 / 0 / 35488650 / 1317723959 / 2126149022 / 106520 | 3427446803 / 690880 / 0 / 0 / 12613504 / 690323468 / 0 / 568064 | 12270487 / 0 / 0 / 0 / 10480810 / 0 / 4119201805 / 4624 |
| map_load: MOV_playMovie | 51171860 / 4495 / 0 / 83925606 / 133012457 / 0 / 3479201 / 0 | 108605504 / 0 / 0 / 0 / 12224 / 0 / 2506306 / 0 | 61556739 / 0 / 0 / 79050700 / 10496 / 0 / 130918297 / 0 |
| map_load: runLandingScreen | 26747412 / 574651 / 134100 / 0 / 22239306 / 19529690 / 20913890 / 106520 | 82470611 / 690880 / 0 / 0 / 203712 / 4816261 / 0 / 568064 | 12284405 / 0 / 0 / 0 / 138794 / 0 / 77861673 / 4509 |
| map_load: runMainMenu | 15018470 / 72739 / 0 / 0 / 43941855 / 49482200 / 7647645 / 112083 | 36803904 / 13290968 / 0 / 0 / 944896 / 41298102 / 0 / 568064 | 1597345 / 234858 / 0 / 0 / 896255 / 0 / 113546496 / 4624 |
| map_load: newGameScene | 5688467 / 249988 / 0 / 13066 / 17629237 / 120672507 / 19424027 / 0 | 11816384 / 45929441 / 0 / 0 / 3941232 / 102085674 / 0 / 0 | 423845 / 23293 / 0 / 0 / 3705536 / 0 / 159523324 / 0 |
| map_load: newGameStateMachine | 10902553 / 18896 / 0 / 0 / 30768378 / 48389158 / 12071705 / 0 | 19394624 / 3870526 / 0 / 0 / 1573414 / 52572850 / 0 / 0 | 2216710 / 1511 / 0 / 0 / 1873268 / 0 / 98064662 / 0 |
| map_load: setupNameSelectorBox | 0 / 186104 / 0 / 0 / 259120 / 129421636 / 25907001 / 0 | 0 / 7484669 / 0 / 0 / 3891469 / 144576139 / 0 / 0 | 0 / 0 / 0 / 0 / 3853708 / 0 / 151920143 / 0 |
| map_load: setupNameSelectorBox#2 | 0 / 188140 / 0 / 0 / 337664 / 173276294 / 41242563 / 0 | 0 / 5963460 / 0 / 0 / 4945868 / 126396480 / 0 / 0 | 0 / 0 / 0 / 0 / 9371742 / 0 / 205672889 / 0 |
| map_load: unloadNewGameScene | 0 / 241522 / 0 / 0 / 471388 / 241068397 / 54513534 / 0 | 0 / 12257381 / 0 / 0 / 6715146 / 198004234 / 0 / 0 | 0 / 0 / 0 / 0 / 11216898 / 0 / 285082757 / 0 |
| map_load: loadMap | 670195646 / 41496722 / 9751275 / 0 / 160890607 / 963654300 / 2055255662 / 124976 | 3210563603 / 724160 / 0 / 0 / 12190656 / 608957402 / 0 / 682240 | 15222770 / 3050 / 0 / 0 / 10676423 / 0 / 3875456454 / 4624 |
| map_load: checkArenaMap | 16167677 / 25330 / 0 / 0 / 48239430 / 0 / 4480762 / 32949 | 56843904 / 194560 / 0 / 0 / 757632 / 0 / 0 / 950912 | 1716595 / 20524 / 0 / 0 / 888596 / 0 / 66327845 / 0 |

Las filas se generan con `python3 build/calibration/final_tables.py` a partir
de las capturas de aceptación nuevas y del baseline instrumentado. El
generador exige que existan todos los puntos y que Redux y port coincidan
en cada VBlank; no extrapola los costos de otra Escena.


## Comparación y prueba de pared

`run.py --compare` y el camino de audio comparan desde frame 0; título,
películas, mapa y puntos compartidos exigen diferencia cero. Un punto ausente
falla. El diagnóstico incluye el primer punto distinto y los costos del tramo.
Se retiró `skip_vblank_delta`. El latch del pad no se movió.

`native --vblanks N` mide desde el primer retorno de VSync, igual que el ancla
de pared SDL. `native_wall_10min` exige ≥600 s, error <1 VBlank y cero
resincronizaciones. La ejecución aislada del orquestador sobre el árbol actual pasa:
**36006 VBlanks, 600,012922230 s y 0 resincronizaciones**. El tiempo
equivale a 36006,004416 VBlanks a la tasa medida (error absoluto
0,004416 < 1). Evidencia sincronizada: `build/port/native/Testing/Temporary/LastTest.log`,
preservada en `build/calibration/orchestrator-current-tree/native-LastTest.log`.
La medición histórica anterior al cambio de ancla no se usa para esta aceptación.

## Verificación vigente

Las capturas Redux se ejecutan una por vez, con límites de 7200 s por
corrida (14400 s para películas); CTest permite ambas corridas. No se
omiten pruebas por lentitud. Los logs bajo `build/calibration/` sobreviven
al reinicio del contenedor; una caché de audio que perdió sus trazas de
`/tmp` se vuelve a capturar, aunque conserve los frames y PCM. Desde esta
reanudación, `/tmp/dw1-audio-reference` apunta al directorio ignorado
`build/evidence/reference`; los manifiestos registran su ruta resuelta para
conservar las capturas nuevas completas, incluidas ambas trazas de hardware.

Resultados finales remotos comunicados por el dueño: suites ejecutadas de
forma serial, sin omisiones, con `DW1_DISC_DIR` definido,
`SDL_VIDEODRIVER=offscreen` y `SDL_AUDIODRIVER=dummy`.

| Verificación | Resultado | Log en `build/calibration/` |
|---|---|---|
| native-headless | 82/82 | `final-native-headless.log` |
| native-asan | 80/80 | `final-native-asan.log` |
| native (SDL) | 92/92 | `final-native.log` |
| web | 39/39 | `final-web.log` |
| native_wall_10min | Aprobada, 600,19 s | `final-wall.log` |

Estos resultados cierran la verificación completa de la historia. Los logs
`owner-*` y las mediciones anteriores quedan como historial. Las correcciones
puntuales de revisión posteriores se verifican con sus pruebas afectadas;
la repetición completa posterior queda a cargo del dueño. La revisión puntual
pasó `libc_heap_pad`, `scene_timing_unit`, `reference_capture_unit` y `sdl_pacing`
(4/4, sin omisiones). Incluye tasa de pared y cantidad de VBlanks incorrectas,
cachés mal formadas, listas GPU DMA de varios paquetes y una pausa real de
150 ms: cero resincronizaciones sin pausa y una con pausa, con su log.
No se realizó commit.


## Correcciones posteriores a las suites del orquestador

Esta sección conserva la secuencia de mediciones y sondeos. Sus pendientes
intermedios son históricos; el estado vigente se informa arriba y en las
tablas de aceptación.

Los logs originales de las tres suites se preservan en
`build/calibration/orchestrator-current-tree/`. La medición de pared de esa
corrida es la evidencia del árbol entregado por el orquestador (arriba).

- Las capturas de audio terminaban al cerrar su último segmento, antes de
  `runLandingScreen`. Ahora ambos lados esperan además todos los checkpoints.
  Hay pruebas sintéticas de checkpoint posterior al audio, sin quitar puntos
  de control. `movie0` y `xa_op1` completaron sus capturas dobles de Redux;
  `movie0_skip` compara ambos checkpoints con diferencia cero, 45 imágenes
  de película y cero fallos de contenido (`resume8-movie0-skip-compare.log`).
- `buDevInit` comprueba el sector de cabecera y reutiliza el directorio
  almacenado; solo la bandera de tarjeta nueva exige `buInit` completo.
  Volver a leer los 16 sectores en cada apertura agotaba la ventana de
  entradas de `card_newgame`. La corrección produjo el mismo archivo de
  tarjeta que Redux, SHA-256
  `18509839ea2e902ff1edc0c00db558d9cd2a98ec63ee13c7bf69f858263886dc`
  (`card-dev-init-test.log`); se añadió una prueba de reutilización del TOC.
- Los bits reservados de `_snd_openflag` no representan secuencias abiertas.
  El presupuesto de CPU sigue las ramas de `_SsVmFlush`: añade 12 ciclos
  por voz con historial silencioso y resta 21 si ENVX es distinto de cero. Los 31 bits reservados estaban añadiendo 8928
  ciclos a cada tick, suficientes para cambiar la fase de lectura del pad.
- El preset SDL fallaba también en el baseline `d79a22a` sin dispositivo de
  video. Los cuatro tests reciben drivers `offscreen`/`dummy`; pasan todos
  en `resume7-sdl-env.log`, sin cambiar su comportamiento ni sus aserciones.
- La aserción de muestras por campo usa la tasa medida del reloj: 734 o
  735 muestras enteras, según el residuo del acumulador.

La calibración de la fase posterior al reinicio de Timer 2 sigue en curso.
Los sondeos conservan sus nombres bajo `build/calibration/`; no sustituyen
las comparaciones finales ni las suites completas.

### Continuación: fase de STR y cierre de música

El transporte STR conserva el plazo absoluto de cada sector. Una interrupción
que demora su entrega no vuelve a desplazar el plazo siguiente. El sondeo
`stream-deadline-native` y su repetición `vsync-tail-intro` alcanzan
`runLandingScreen=7820`, `runMainMenu=8026`, `newGameScene=8316`,
`newGameStateMachine=8497`, `setupNameSelectorBox=8773` y
`setupNameSelectorBox#2=9106`, todos iguales a Redux. La prueba
`delayed_sector_dispatch` de `cd_stream` verifica el plazo del siguiente sector
tras una interrupción de 1000 ciclos (`resume30-deadline-test.log`).

`stream-poll-probe` confirma 38 ciclos por sondeo vacío de `StGetNext`
(600 llamadas) y 82 por `VSync(-1)` sin interrupciones (786469 llamadas).
`vsync-tail-probe` mide 60 ciclos de retorno de VSync; la salida de `v_wait`
añade 22 según sus diez instrucciones y dos accesos a RAM. La publicación
de un slot listo de StGetNext añade 40 ciclos (17 instrucciones y 6 accesos).
`resume29-unit.log`: libsnd, cd_stream, clock, pack y av_stubs pasan (5/5).

La ruta que salta el video aún requiere corrección: el sondeo `vsync-tail-map`
alcanza el segundo selector en 1928 frente a 2048. El reinicio de Timer 2
ocurre en 478954157, frente a 478954577 proyectados desde Redux: faltan
420 ciclos de fase (antes, 538). La comparación anterior de mapa no es
validación del árbol actual. Se está descomponiendo `finalizeMusic` antes de
la lectura SCN para identificar el trabajo de CPU ausente.

El baseline limpio `d79a22a`, extraído con `git archive`, reproduce los cuatro
fallos SDL (`resume32-baseline-sdl-tests.log`): dispositivo de video ausente
y las aserciones de marca que fallan al abortar antes de arrancar el juego.
La repetición con drivers offscreen/dummy queda registrada en
`resume32-baseline-sdl-offscreen.log`; la del árbol actual, en
`resume32-current-sdl-tests.log`.

`quit-probe/spans.jsonl` identifica el trabajo ausente: `finalizeMusic` tarda
4090 ciclos (882151800..882155890), de los que 184 son su bucle propio,
46 son `seqStop`/`seqClose` inactivos, 938 los diez `SsVabClose` (dos de
305 y ocho de 41), 1226 `SsEnd` y 1696 `SsQuit` con `SpuQuit` (1678).
El port cobraba 1024; ahora reproduce los 4090 según ramas/estado, sin
selección por escena. `SpuFree` tarda 232 con el primer registro terminal
(sin bloques asignados); la parte propia de `SsVabClose` válido es 73.

`music-close-map` vuelve a alcanzar el segundo selector en **2048**, igual a
Redux, tras el costo medido de cierre. No se exige igualdad de ciclos:
el reinicio queda en 478957200; los checkpoints de aceptación sí exigen
VBlank idéntico. `resume34-unit.log` pasa 6/6 (libspu, libsnd, cd_stream,
clock, pack y av_stubs). `resume33-movie0-compare.log` pasa los dos
checkpoints de movie0 (482 y 7820), sus 3636 frames, audio bloqueante y
A/V. Esa comparación precede a los costos de cierre y se repetirá en la
suite completa del árbol final.

El baseline limpio con offscreen/dummy pasa 5/5 incluidos sus fixtures,
igual que el árbol actual (`resume32-baseline-sdl-offscreen.log` y
`resume32-current-sdl-tests.log`).


### Orden de Pause y media de despacho de ReadN

El último callback ReadN emite Pause antes del retorno de la IRQ: Redux
escribe el comando en 941832657, entra a ReturnFromException en 941833567
y restaura el contexto en otros 155 ciclos. El port ahora inicia Pause antes
de esa cola de 1065 ciclos; cobrar toda la IRQ antes de emitirlo desplazaba
el plazo de la siguiente lectura. No se cambia el latch ni el verificador.

La media de despacho anterior usaba solo SCN (61 intervalos, 225800,557
ciclos por sector). SS aporta 27 intervalos de 225789 ciclos. La media
conjunta es 225797,011, es decir 28 ciclos sobre el período nominal 225769.
El mismo valor se aplica a todos los ReadN, independientemente del archivo
o escena. Es una aproximación explícita al despacho de Redux por ramas;
la aceptación sigue exigiendo cero VBlanks, sin tolerancias. La alternativa
basada solo en SS (20 ciclos) adelanta el mapa y se descartó.

`weighted-dispatch-map` reinicia Timer 2 en 478954598, 21 ciclos después
de la referencia proyectada (478954577), y alcanza el segundo selector
en 2048, igual a Redux. Esta es evidencia de sondeo, pendiente aún de las
comparaciones completas y las suites finales.


La media SCN+SS de 28 ciclos aún deja la ruta con intro en 9226. Se amplió
la muestra a todos los ReadN de `title`, la escena de calibración completa:
342 intervalos, 225795,602 ciclos por sector, redondeados a **27** ciclos
de despacho adicionales. Se excluyen únicamente extremos con entrega de
IRQ demorada (identificables por el intervalo de recuperación más corto que
el período nominal); las parejas interiores demorada/recuperación se conservan.
`build/calibration/measure-readn-dispatch.py` reproduce los tramos y la media;
`title-readn-dispatch.json` conserva los extremos y conteos. El valor es global,
sin selección por escena ni archivo. El sondeo `title-dispatch-map` conserva
2048 para el segundo selector. `resume40-matching.log` confirma nuevamente
`Compiled binaries match with target binaries`.

### Continuación: sondeo de ReadN y VSync por ramas

`read-counter-probe` registra 2450 lecturas de Timer 1. Sus ticks ocurren
cada 2146 ciclos, con fase 5 respecto del reloj de hardware del port.
El sondeo de CdReadSync ya conserva los puntos de despacho del bucle C:
25 ciclos de entrada, bucle de 218 sin IRQ ni reintento del contador,
396 en CdReady y 21 de salida. VSync(-1) repite la lectura inestable
en pasos de 20 ciclos. `clock_poll_branch` difiere el despacho hasta
la rama y drena también las IRQ vencidas durante un handler.

`read-start-probe` separa ReadN en confirmación FIFO (797 us), arranque
(20 ms) y primer sector tras seek. La confirmación ReadN consume 911
ciclos de servicio más la ruta BIOS; Pause consume 898. La ruta sin
DMA SPU instalado ahorra 9 ciclos por servicio y 18 en el último sector.
No se usa una media por archivo en el bucle bloqueante. La aproximación
de despacho fuera del sondeo y dentro de IRQ sigue pendiente de validar.

`tick-mode-probe`: SsSetTickMode(2)=88 y SsSetTableSize(1,1)=909 ciclos.
Los bloques propios de initializeMusic y el intervalo CdRead→CdReadSync
se cobran en su orden original, bajo DW1_PORT.

`vsync-branch-map` conserva el segundo selector en 2048;
`vsync-branch-intro` aún lo alcanza en 9226 frente a 9106. No es cierre
de aceptación. `resume59-units.log` registra expectativas unitarias antiguas
de retorno fijo que deben actualizarse al verificar el modelo final.

`landing-return-probe` descubre trabajo de CPU faltante después de
VSync: ResetGraph(1)=219, GsSetOrign=12 y GsDrawOt=428 ciclos.
GsDrawOt entra en su consulta VSync en +88; su wrapper aporta 12 antes
y 9 después de DrawOTag. Estas correcciones se están verificando
con los sondeos gpu-tail-map e intro.

`landing-nested-probe` completa el desglose del último frame de landing:
PutDispEnv=2722; SetDispMask(1)=86; PutDrawEnv=11263 (consulta VSync
en +473, copia BIOS=10443); GsSetDrawBuffClip=11336;
GsSetDrawBuffOffset=11357; GsSwapDispBuff=25593, de los que 92 son
propios. Se corrigió también el orden de la consulta VSync de PutDrawEnv.
`gpu-nested-map` conserva setupNameSelectorBox#2=2048.
`resume65-units.log` pasa 9/9: reloj, CD, GPU, tarjeta, audio y sus fixtures.
Las latencias de regresión del disco sintético de libcd incluyen ahora
el sondeo por ramas; el callback asíncrono conserva una comprobación
independiente del plazo físico y del ahorro de la ruta sin DMA SPU.


### Decisión del dueño: pulsaciones sostenidas

La decisión congelada del 2026-10-25 descarta continuar compensando con
el reloj la fase del pad en la intro completa. Los guiones de newgame,
map_load y title_audio mantienen Start del selector 120 VBlanks y esperan
30 más antes de Cross. Los Start del título duran 24 VBlanks; Cross pasa de 8 a 16 y las
direcciones a 12, por debajo del umbral de repetición de 15 del menú. El diálogo inicial termina su
guion antes de los selectores; sus diálogos posteriores se anclan al selector
correspondiente para no interrumpir Start con eventos de una fase anterior.
Los Start de salto
de película conservan sus 60 VBlanks. Se vuelven a capturar las referencias
afectadas; el latch del pad y la exigencia de diferencia cero permanecen.

Se conservan solo los costos de la continuación respaldados por Redux:
`irq-ack-cutoff-probe/spans.jsonl` mide 25 ciclos desde el retorno del
callback hasta la última consulta I_STAT, no hasta ReturnFromException;
`spu-poll-probe` mide SetRCnt(2): escritura de modo en +58, retorno en +65.
`vhb-tail-probe` mide SsUtGetVBaddrInSB en 40 ciclos, con 4 anteriores
y 18 posteriores propios de readVHBFile. `intro-music-spans` confirma los
intervalos en la ruta con intro completa. No se persigue el residuo de
377 ciclos previo a esa carga para cambiar qué pulsación recibe el juego.

`resume68-units.log`: 8/8 pasan con las expectativas del modelo de CD/VSync
por ramas. `resume69-matching.log`: binarios idénticos al original tras los
bloques DW1_PORT adicionales. La comprobación del corte I_STAT distingue
una IRQ antes del verificador, durante el callback y después de la última
consulta; la tercera publica el pad normalmente.

`owner-units-final.log` pasa 11/11 sin omisiones: clock, libcd, libsnd,
libgpu, card, libspu, cd_stream, pack, scene_runner, scene_state_unit y
reference_capture_unit. Los sondeos `owner-input-native` y
`owner-scoped-native` demostraron que 24 campos no garantizaban recibir
Start; sus capturas exploratorias se detuvieron y no se usan como aceptación.

`owner-card-newgame.log` pasa con la nueva captura doble: checkpoints
compartidos MOV=482, landing=642 y menú=848, todos con diferencia cero;
loadSavegame=1555 frente a 1561 es informativo, según la spec. Coinciden
las 11 variables en cuatro ventanas y los 131072 bytes de la tarjeta final
(SHA-256 18509839ea2e902ff1edc0c00db558d9cd2a98ec63ee13c7bf69f858263886dc).
`owner-native-environment.log` vuelve a pasar 5/5 en SDL con el árbol
reconstruido. La comparación con d79a22a ya documentada confirma que los
fallos sin offscreen/dummy eran previos a esta historia.

`owner-card-continue.log` pasa: tarjeta idéntica, estado en cuatro ventanas
y 584 valores de guardado/carga coincidentes. El sondeo
`owner-continue-native` confirmó loadSavegame=1385 tras limitar la dirección
a 12 campos; con 16 el menú repetía la dirección y elegía otra opción.
El inventario isr_spins se regeneró para las nuevas líneas de sound.c
(508/511 → 529/532), sin hallazgos ni pendientes nuevos; la primera corrida
web detectó ese metadato desactualizado y se repetirá tras la corrección.

`owner-fnptr-update.log` regenera las tres ubicaciones de sound.c del
inventario fnptr (400/455/748 → 406/476/790). La revisión del diff confirma
que solo cambian líneas: 1041 conversiones resueltas, 119 unidades y cero
diagnósticos directos de aridad sin resolver.

`owner-naming-reference.log`: las dos capturas nuevas coinciden en imagen,
estado y trazas. Se conserva el hash visual fijado
`83623016b8fd5e72594a1ec2b774d9e2007237017191d798d418470b0872d476`;
no fue necesario cambiarlo tras sostener las entradas.

La repetición `owner-web-final.log` pasa los inventarios, pero su prueba
web_scene_title coincide con la regeneración del título por la suite
headless y pierde `run-1.ppm`. Es una colisión entre las ejecuciones de
CTest, no evidencia de paridad; se repite web después de que headless
termine todas sus capturas del título (`owner-web-serial-title.log`).

La primera referencia nueva de newgame registra segundo selector=9154 y
unload=9679, iguales al sondeo nativo `owner-names-native`. La intro hasta
el primer selector también conserva diferencia cero. Esto es observación
de la primera pasada, no reemplaza la captura doble ni la comparación
formal del guion final que está ejecutando la suite.

`owner-web-serial-title.log`: suite web completa **39/39**, cero fallos y
cero omisiones, 718,13 s. Incluye la comparación del título en ambos
navegadores; terminó tras corregir los inventarios y evitar la colisión
con la captura headless.

Reanudación del 2026-10-26: tras perderse la captura en curso al reiniciar
el contenedor, se repite newgame completo. La primera pasada vuelve a dar
setupNameSelectorBox#2=9154, unloadNewGameScene=9679, gameLoop=16927
y randomLimit#17=28953. La captura doble final pasó en 2295,44 s: imagen,
estado y trazas temporales y de hardware idénticas. La comparación nativa
completa de newgame todavía está pendiente. La corrida reanudada
queda en `owner-headless-resumed.log` (progreso también en
`owner-headless-resumed.live.log`). No se reutiliza la captura parcial.

`owner-headless-resumed.log`: reference_newgame_state y reference_map_load
aprobadas. `scene_map_load_compare` pasa en 149,93 s con los diez puntos
estrictos en cero y el hash visual fijado; el segundo selector ocurre en
1976 y checkArenaMap en 9535 en ambos lados con las entradas sostenidas.
Los tiempos anteriores de la tabla se sustituirán por esta medición al
completar las filas de las películas.

La recaptura doble de title_audio pasa en 1451,31 s y la comparación
nativa en 299,32 s (`owner-headless-resumed.log`). El guion sostenido
alcanza el segundo selector en 9154 en ambos lados; los puntos compartidos
quedan en cero y pasan los dos segmentos de contenido de voz.

La corrida reanudada también aprueba xa_op1, movie0 y movie0_skip.
Movie0 compara 3636 frames, MOV_playMovie=482 y landing=7820;
movie0_skip compara 45 frames, MOV_playMovie=482 y landing=642. No se
modifican las excepciones de MDEC de la historia 3.7. Las tablas anteriores
ya usan estas capturas nuevas. La métrica XA de movie0 conserva 54,424 dB.

Limitaciones informativas vigentes: SsSeqPlay de title_audio ocurre en
17049 frente a 17052 (−3 VBlanks), fuera de los puntos compartidos
estrictos. Su contenido de voz pasa; las métricas PCM (mínimos −2,503 y
8,872 dB) y regla B siguen siendo reporte no bloqueante por decisión
congelada, sin ampliar tolerancias de los checkpoints ni del video.

### Cierre del bloque headless reanudado

`owner-headless-resumed.log`: **21/21**, cero fallos y cero omisiones,
7651,38 s. El detalle íntegro queda en
`owner-headless-resumed-detail.log`, preservado antes del siguiente CTest.
Newgame pasa nueve checkpoints, quince variables y dieciséis llamadas rand:
segundo selector=9154 y unload=9679 en ambos lados. randomLimit#17 ocurre
en 28981 frente a 28953 (+28), informativo según la spec; su estado y
secuencia aleatoria coinciden. No se cambia el latch para corregir esa
diferencia no estricta.

Card_newgame y card_continue vuelven a pasar con capturas dobles nuevas;
la tarjeta final coincide byte por byte, y continue valida 584 valores de
guardado/carga. Después se completaron el bloque inicial headless y las
suites completas ASan y SDL, serialmente y con offscreen/dummy (ver resultados
finales arriba).

La repetición inicial headless encontró una expectativa vieja en av_stubs:
SsUtGetVBaddrInSB exigía aún los 64 ciclos genéricos, aunque la función
usa los 40 medidos en vhb-tail-probe. Se actualiza únicamente esa
aserción; no cambia el modelo ni requiere nuevas referencias.
`owner-av-{native-headless,native-asan,native}.log`: 2/2 en cada preset,
incluido el fixture, cero omisiones. La repetición anterior se conserva en
`owner-headless-prefix-before-av-fix.log`; se repite el bloque completo.

`owner-headless-prefix.log`: **62/62**, cero fallos y cero omisiones,
1250,89 s; detalle en `owner-headless-prefix-detail.log`. La unión con las
21 pruebas reanudadas cubre los **82 tests configurados**, con pack_disc
como fixture repetido entre bloques. Todos pasan en el árbol actual.
La traza del título se repite exactamente en ambas corridas:
clock=eae9b3f14dff0dac82412be3d02836d59a734ecf01751622e998a216a9b48b90,
timing=a288f0c2e537ea6f52e96599f2fda3615393996b587eba5af3513a59d9a17154.
La verificación final de ASan y SDL se registra arriba.


## Corrección E3: regresión de pared en diez minutos (2026-10-26)

La aprobación histórica de la 3.10 no era repetible en este servidor:
`wall-10min-local-2026-10-26.log` registra nueve resincronizaciones y
`wall-9000-detail-probe.log` reproduce dos en los primeros 9000 VBlanks.
El diagnóstico mide **18,961 y 18,890 ms de CPU por VBlank** en las dos
transiciones afectadas, por encima de los 16,664 ms disponibles. La espera
acumulada de `produce` es apenas 6,556 ms en las 27 consultas del primer
tramo y 9,131 ms en las 22 del segundo; no duerme en las dos consultas que
resincronizan. La contrapresión de A3 no es la causa principal.

El muestreo del hilo del juego atribuye 275/373 muestras a rasterización y
74/373 a `next_due`: el sondeo de VSync vuelve a recorrer los 32 slots de
la misma cola miles de veces sin que cambie. `clock.c` conserva ahora el
mínimo de eventos de dispositivo hasta que una inserción, cancelación,
despacho o reset invalide la caché. Cada consulta sigue comparando con el
VBlank y mantiene el desempate por secuencia. No se modifican costos,
frecuencia, muestras, umbral de resincronización ni la contrapresión de audio.

La regresión corta sintética reproduce textura y fundido de pantalla completa
seguidos por el VSync real: con el reloj anterior da cinco resincronizaciones
en 120 VBlanks; con la caché, cero, sin overflow ni underrun. Los casos nuevos
de `test_clock` verifican las mutaciones de la cola después de consultar su
mínimo. `clock` y todos los tests `sdl_` pasan (10/10). La traza con disco de
1000 VBlanks se conserva byte por byte (SHA-256
`1355d14b6709ce0a2e364f87f7829494e2c8591eebb3b13f0eaba188531dbb69`).

La corrida corregida de 9000 VBlanks da 149978334834 ns y cero
resincronizaciones. Las dos corridas consecutivas aisladas de diez minutos pasan:

| Corrida | VBlanks | Pared (s) | Error (VBlank) | Resincronizaciones |
|---|---:|---:|---:|---:|
| 1 | 36006 | 600,012999970 | 0,010463155 | 0 |
| 2 | 36006 | 600,012884626 | 0,003541510 | 0 |

Logs: `evidence/story-e3/wall-10min-fixed-{1,2}.log`. El audio registra cero
overflows y 6/5 underruns en estas cargas largas; el gate corto `sdl_pacing`
conserva cero en ambos contadores. Las suites completas con disco siguen en
curso; todavía no se declara la aceptación integral.
Diagnóstico, procedimiento y logs:
[wall-pacing-cause.md](../implementation-artifacts/evidence/story-e3/wall-pacing-cause.md).
