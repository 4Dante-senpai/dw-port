# Gate de imagen y espera de audio: ejecución detenida

Seguimiento tras la decisión del dueño: [nueva parada por trabajo CPU](device-wait-followup.md).
Este documento conserva la evidencia de la primera ejecución.

Fecha: 2026-10-26. Base: `73ffac194c5fdc795ea175681fc6aaf9cd388e97`.
Builds y pruebas ejecutados dentro del contenedor, sin daemon remoto y sin commits.

## A1: implementación y medición

`--state` reutiliza `validate_comparison` y `compare_capture`: hash fijado/RGB555,
regiones declaradas y diff con cantidad de píxeles y caja. Una diferencia devuelve 1,
salvo `image_informative = {reason, pixels, box}` válido. Los errores de configuración,
referencia o captura siguen siendo fatales incluso con imagen informativa.

Medición de las capturas disponibles al iniciar, con el checkout y binarios en HEAD.
Las dos capturas de referencia de cada Escena eran idénticas. No se cambiaron referencias:
se agregaron sus hashes existentes a las tres Escenas que aún no los declaraban.

| Escena | Píxeles distintos RGB555 | Caja inclusiva x/y | Disposición |
|---|---:|---|---|
| map_load | 0 | — | Gate estricto, hash existente sin cambios |
| newgame | 402 | x=87..217, y=46..121 | Informativa: modelo 3D de Jijimon pendiente |
| card_newgame | 474 | x=58..255, y=53..92 | Informativa: paridad visual pendiente; causa no verificada |
| card_continue | 469 | x=58..255, y=53..92 | Informativa: paridad visual pendiente; causa no verificada |

`image-gate-runner.log`: prueba sintética del main real con `--state`, igualdad,
diferencia (retorno 1 y diff), excepción explícita (retorno 0), falta de motivo,
hash incorrecto y PPM mal formado, además de las pruebas anteriores del runner.
Los logs `image-gate-*-tests.log` registran las pruebas CTest seleccionadas.

## A3: condición explícita de parada encontrada

Se ensayó quitar la espera del productor y pacear todos los retornos de VSync.
También se probó el hook de pared en `psyq_vsync_query_cost`, usado por las consultas
VSync(-1) que `CdReadSync` representa sin llamar a la función pública `VSync`.
Se conservó el origen de escena para no alterar las trazas virtuales.

El juego real con Pack siguió desbordando. Instrumentación temporal, primer desborde:

```
cycle=6784604 vblank=12 last_vsync=2 level=8004 frames=223
produce -> spu_sync -> capture_vblank -> run_until -> clock_advance
        -> clock_advance_cost -> CdInit -> dw1_game_main
```

Comando: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy build/port/native/dw1 --pack build/port/native/us.pack --frames 120`.
Resultado completo en `vsync-audio-stop.log`: `overflows=63613 underruns=6`.
En `CdInit`, `port/psyq/libcd.c:314` avanza de una vez `CLOCK_CD_INIT_CYCLES`
(120 ms virtuales), además de los ACK. En ese tramo no hay retorno de VSync en el port.
El asm original contiene consultas VSync en `CD_sync`; el modelo actual agrega esos
costos en bloques y no reproduce esas llamadas en esta ruta. Agregar pausas al observer
VBlank o a `spu_sync` incumpliría AD-8; introducir nuevos puntos de polling requiere
resolver cómo preservar exactamente los costos y las trazas existentes.

Los ensayos `sdl_frame0` y `sdl_pacing` fallaron; sus logs se guardan, incluido un
ensayo con el emulador de referencia pausado para evitar competencia con esa captura.
No se modificaron remuestreo, capacidad del anillo ni constantes de audio.

Se aplicó «Detener y reportar» de la spec. Todos los cambios de producción del
ensayo A3 y la instrumentación temporal se retiraron: el audio SDL conserva el
comportamiento previo. A3 sigue sin implementar. No se ejecutaron `native_wall_10min`
ni `--frames 1200`, ni se afirma que pasen sus criterios.

La recaptura de referencia de `newgame` iniciada para medir nuevamente fue interrumpida
al detener la tarea. El runner invalida esa caché antes de capturar: hay que regenerarla
antes de volver a ejecutar la Escena. No se completaron las suites con disco ni la
comparación posterior de trazas de los tres presets. A1 no se declara aceptada de punta
a punta y A3 requiere una decisión del dueño antes de continuar.

## Verificación del árbol final

- `cmake --preset native && cmake --build --preset native -j 3`: correcto.
- `cmake --preset native-headless && cmake --build --preset native-headless -j 3`: correcto.
- `cmake --preset native-asan && cmake --build --preset native-asan -j 3`: correcto.
- `ctest --preset native -R 'scene_runner|scene_compare|scene_state|sdl_audio' --output-on-failure`: 4/4, 0 omitidos (incluye fixture `pack`).
- `ctest --preset native-headless -R 'scene_runner|scene_compare|scene_state|clock' --output-on-failure`: 4/4, 0 omitidos (incluye fixture `pack`).
- `ctest --preset native-asan -R 'scene_runner|scene_compare|scene_state|clock' --output-on-failure`: 4/4, 0 omitidos (incluye fixture `pack`).
- `git diff --check`: correcto.

Estas pruebas seleccionadas no equivalen a las suites completas con disco requeridas
para aceptar la spec. Los ensayos fallidos de audio corresponden al prototipo retirado,
no a una corrección aceptada ni al árbol final.
