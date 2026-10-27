# A3: parada tras habilitar las esperas bloqueantes de dispositivo

Fecha: 2026-10-26. Base: `73ffac194c5fdc795ea175681fc6aaf9cd388e97`.
Todo se ejecutó dentro de este contenedor. No se hicieron commits.

Se releyó la spec actualizada y se ensayó su opción 1: productor sin espera,
un acumulador racional de pared compartido por VSync y los saltos entre eventos
de esperas CD/GPU/MDEC/tarjeta. Sin pausas dentro de callbacks, escrituras de
registros o `spu_sync`; headless y capturas de Escena omiten la espera.
Esto evita el desborde inicial de `CdInit`, pero no satisface la aceptación real.

## Resultado y nueva condición de parada

Comando con el ensayo, sin emulador de referencia compitiendo por CPU:

```
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy build/port/native/dw1 --pack build/port/native/us.pack --frames 1200
```

`device-wait-load1200.log`: finaliza en frame 1199, ciclo 1560010275,
con **overflows=630 underruns=3**. Una repetición instrumentada registra
**overflows=570 underruns=1** (`device-wait-probe1200.log`). No se cumple el gate.

`device-wait-cpu-stop.log` registra un ensayo acotado a 400 VBlanks:

- Último punto permitido de pacing: VBlank 356; siguiente: VBlank 368.
- Trabajo de CPU: 6742591 ciclos entre esos puntos (aproximadamente 199 ms
  virtuales). Costos GPU/MDEC/tarjeta/SPU: cero. Los 26993 ciclos CD y 3189 PsyQ
  del intervalo no proporcionan un punto de espera fuera de callbacks antes del desborde.
- El backtrace del avance CPU desde ciclo 200970462 a 202671907 es
  `clock_advance_cost -> DecDCTvlcBuild -> MOV_playMovie -> startMovie -> playMovie -> dw1_game_main`.
  `port/psyq/libpress.c:148-191` confirma que construye la tabla VLC en CPU;
  no es una espera al dispositivo MDEC. Después continúa trabajo de CPU hasta
  el siguiente punto permitido; no se atribuye todo ese intervalo a esa única función.
- El anillo desborda en VBlank 368, ciclo 207712577, **antes** del siguiente
  pacing: se pierden 314 + 223 muestras en esta corrida.
- El reloj de pared del ensayo corto termina con 400 VBlanks,
  6665786562 ns y cero resincronizaciones; esto no reemplaza el gate de diez minutos.

Se aplica la condición actualizada «Detener y reportar»: hay una carga real que
avanza demasiado tiempo virtual ejecutando CPU sin VSync ni una espera bloqueante
de dispositivo donde se permita pausar. Hace falta una decisión del dueño sobre
este caso; no se introdujo una pausa en CPU ni se reclasificaron sus costos como
espera de MDEC para hacerlo pasar.

## Árbol entregado y verificaciones

Se retiraron el ensayo A3 y toda su instrumentación, incluidos los tests
experimentales: los archivos C y CMake de audio/reloj/SDL vuelven exactamente a HEAD.
El parche sin instrumentación queda en `/tmp/e3-a3-device-wait-attempt.patch`
como ayuda local para continuar; no es parte de la solución entregada.
Se conserva la enmienda AD-8 ya decidida por el dueño y todo el trabajo A1.
A3 **no está implementada**.

- Con el ensayo: `sdl_frame0` pasa. La primera parte de `sdl_pacing` pasa con
  overflows=0, underruns=0, ratio 1,0142; su aserción de velocidad de captura falla
  (5,225 s frente al límite de 5 s), incluso con el emulador pausado.
  El test completo no pasa: ver `device-wait-sdl-tests.log`.
- Árbol restaurado: build `native` correcto; tests `clock` y `sdl_audio` 2/2.
  Ver `device-wait-restored-tests.log`.
- A1: se repite `python3 -I -B tools/port/scenes/test_runner.py`;
  ver `image-gate-runner-followup.log`. Las mediciones y verificaciones anteriores
  siguen en `image-gate-and-vsync-status.md` y `image-gate-*-tests.log`.
- La recaptura auténtica de `newgame` se reinició, pero se interrumpió al detener
  A3. La caché sigue inválida; no se falsificó su fingerprint ni se cambiaron hashes
  de referencia. Debe regenerarse antes de aceptar las suites con disco.
- No se ejecutaron `native_wall_10min` ni las suites completas con disco de los
  tres presets después de esta nueva parada. No se declara paridad posterior de
  trazas/checkpoints ni aceptación integral de A1/A3.
