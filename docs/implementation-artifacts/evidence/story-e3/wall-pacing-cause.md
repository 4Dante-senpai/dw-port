# E3: causa del atraso de pared

Fecha: 2026-10-26. Base: `0352b300bc1aadc9e7cce616a3c7b7c6cdc4d2a2`.
Builds y pruebas dentro del contenedor actual, sin cambiar su cuota de 2,5 CPUs
ni hacer commits. No se ejecutan otros builds o tests durante las corridas de pared.

## Reproducción antes de corregir

```sh
cmake --preset native
cmake --build --preset native -j2
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  build/port/native/dw1 --pack build/port/native/us.pack --vblanks 9000
```

La instrumentación temporal registra los nanosegundos dormidos en `produce`,
el hilo (`SDL_GetCurrentThreadID`) y las pilas de las esperas de más de 15 ms.
En `pace`, antes de dormir, registra el atraso respecto del deadline y la CPU
consumida desde la consulta anterior (`CLOCK_THREAD_CPUTIME_ID`). El ancla está
en VBlank absoluto 1: el contador de resincronización se imprime relativo a ella.
La instrumentación se retiró antes de construir y verificar la corrección.

- `wall-9000-initial-probe.log`: 150115824737 ns, dos resincronizaciones
  (7702 y 8298), `overflows=0 underruns=6`.
- `wall-9000-detail-probe.log`: dos resincronizaciones (7701 y 8299).
  En el primer tramo absoluto 7676..7702, CPU media **18,961 ms/VBlank**;
  solo **6,556 ms de espera de audio en todo el tramo de 27 consultas**.
  En 8280..8301, CPU media **18,890 ms/VBlank**, con **9,131 ms de audio
  en las 22 consultas**. En ambos VBlanks que resincronizan el productor no
  ejecutó `SDL_Delay`: su tiempo acumulado es aproximadamente 1 µs.
  El atraso aumenta gradualmente hasta 68,758 y 67,478 ms, respectivamente.
- El muestreo adicional usa un timer de CPU del hilo del juego, notificación
  `SIGEV_THREAD_ID`, señal `SIGPROF` y período de 5 ms. Captura `backtrace`
  en VBlanks absolutos 7600..8400. La muestra perturba la carga; sus tiempos
  no sustituyen la corrida detallada. Las pilas seleccionadas y su resumen
  están en `wall-9000-cpu-samples.log`; el log completo queda fuera de fuentes
  en `build/e3-wall-samples-preserved.log`.

En las dos ventanas de transición 7676..7715 y 8276..8315 hay 373 muestras:
275 (73,7 %) en rasterización (`triangle`, `edge`, `shade`, `texel`, `put`,
`to5`, `texture_row`), 74 (19,8 %) en `next_due`, siete en conversión del display.
Las pilas de la cola pasan por `run_until` / `clock_wait_until` o sondeo de
VSync; las de rasterización por `gpu_gp0` / `DrawOTag`. No atribuyen el exceso
principal a lectura del Pack, al hilo SDL principal ni al callback de audio.
El cgroup registró un único throttling de 6929 µs durante las primeras
corridas y el resto del trabajo de diagnóstico: no explica el exceso sostenido
de CPU del hilo del juego.

## Corrección y regresión corta

`next_due` conserva el índice del mínimo de eventos de dispositivo. Lo invalida
al programar, cancelar, despachar o resetear la cola. Sigue comparándolo con el
próximo VBlank en cada consulta y conserva el desempate por secuencia. Se evita
recorrer los 32 slots miles de veces entre dos mutaciones de la misma cola.
No cambia ningún ciclo, costo, tasa, umbral de pacing, píxel o muestra.

`test_sdl_pacing` reproduce sin disco la combinación textura/fundido de pantalla
completa y sondeo real de VSync. Con el reloj anterior, 120 VBlanks dan cinco
resincronizaciones (`wall-short-before.log`); con la caché dan cero, sin overflow
ni underrun (`wall-short-after.log`). `test_sdl_pacing.py` exige esos ceros.
`test_clock` añade inserción de un mínimo anterior, cancelación del mínimo,
cola vacía, programación inmediata y reutilización de slot desde callback,
y reset con mínimo pendiente. Los casos existentes conservan el desempate y
el orden VBlank/dispositivo.

`wall-short-tests.log`: **10/10**, incluido `sdl_frame0` y el test completo
`sdl_pacing`, cero omisiones. `wall-9000-fixed.log`: **9000 VBlanks,
149978334834 ns, cero resincronizaciones**, `overflows=0 underruns=3`.
Los underruns de la carga larga se reportan; el gate corto exige cero y pasa.

La traza de reloj de los primeros 1000 VBlanks con disco coincide byte por byte
entre el binario headless previo y el corregido: SHA-256
`1355d14b6709ce0a2e364f87f7829494e2c8591eebb3b13f0eaba188531dbb69`.
Archivos locales: `build/e3-clock-{before,after}.trace`; comparación mediante `cmp`.

## Aceptación

Dos corridas consecutivas, aisladas, del mismo binario, mediante:

```sh
DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh \
  --preset native -R '^native_wall_10min$' -V
```

| Corrida | VBlanks | Nanosegundos | Error (VBlank) | Resincronizaciones | Audio overflow/underrun |
|---|---:|---:|---:|---:|---:|
| 1 | 36006 | 600012999970 | 0,010463155 | 0 | 0 / 6 |
| 2 | 36006 | 600012884626 | 0,003541510 | 0 | 0 / 5 |

Logs: `wall-10min-fixed-{1,2}.log`. Las dos exceden 600 s y su error es
menor a un VBlank; no se cambiaron los criterios del test.

Las suites completas con disco están en curso, en secuencia y con `ctest -V`
y comprobación de cero omisiones: `native-headless`, `native-asan`, `native`.
Se restauraron las capturas de audio preservadas de
`build/evidence/reference` a `/tmp/dw-reference`, destino original de sus
manifiestos, conservando un enlace desde la ruta anterior y desde
`/tmp/dw1-audio-reference`. Así se liberan 3,3 GB del volumen del repositorio.
No se cambian fingerprints ni hashes: los capturadores verifican o regeneran
las cachés normalmente. No se declara cerrado el trabajo antes de las suites.
