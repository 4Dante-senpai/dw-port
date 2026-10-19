# Historia 3.5: estado de implementación

**2026-10-19: incompleta, traducción parcial del paso (1).** Se conserva el
checkpoint de seis entradas VAB/volumen y se traducen los cuerpos de otras 14
entradas, las voces explícitas y el temporizador. `SsStart` y
`SsSeqCalledTbyT` conservan `stub-m2:` porque falta despachar los eventos SEQ.
Abrir/reproducir una secuencia configura su estado, pero **todavía no produce
música SEQ**. No se cierra la historia ni se afirma paridad de audio.

## Sonda previa de música

Dos corridas del original USA con el PCSX-Redux fijado y los inputs de
`newgame.toml` anteriores a `gameLoop` verificaron la ventana renegociada:

| Medición | Ambas corridas |
|---|---|
| `gameLoop`, VBlank desde arranque | 17560 |
| Fin del tramo 2 | 18760 |
| Ventana | 1200 VBlanks |
| VBlanks con SEQ en reproducción | 1138 |
| Duración SEQ observada | 18,985633 s |

La secuencia se detuvo/cerró en 17623 y volvió a abrirse/reproducirse en 17685;
se cuenta la reproducción, no toda la ventana. La condición previa de ≥10 s
queda superada. Script y logs externos: `/tmp/dw1-3-5-gate-v2/run_probe.py`,
`run-1.log`, `run-2.log`. Las líneas `GATE`, normalizadas quitando espacios
finales, son idénticas; SHA-256 de `gate.txt`:
`af4160275d1a80e8adb782c0710eb2e2852811d145e464d9a0934b1d9eda107e`.
La sonda usa SPU dummy/NullSync y no sirve como evidencia PCM.

## Captura y comparación

`spu_capture_begin`/`spu_capture_end` toman streams prestados. El backend drena
el audio anterior al comienzo, conserva el callback de salida, escribe PCM16
estéreo a 44,1 kHz y finaliza la cabecera WAV al cerrar. Los errores de E/S se
propagan; un reinicio del reloj invalida el tramo sin intentar retroceder el SPU.

La traza `dw1-spu-trace-v1` contiene cuatro columnas: VBlank relativo, muestra
relativa, offset de registro y valor. Las dos últimas están en hexadecimal.
La muestra se calcula como `(ciclo − ciclo inicial) / 768`, con división entera,
también cuando el checkpoint cae entre muestras.

`title_audio.toml` delimita `runLandingScreen`→entrada de
`unloadNewGameScene` y `gameLoop`+1200 VBlanks. El intervalo intermedio queda
fuera de ambos WAV y trazas. El runtime cierra el primer tramo antes de ejecutar
la función final y termina automáticamente al cerrar el segundo.

El comparador aplica la regla B de AD-15: compara las secuencias de valores por
registro dentro de cada VBlank, sin imponer orden entre registros. Reporta el
desfase máximo y medio en muestras, sin umbral. Una divergencia identifica el
primer VBlank/registro/valor distinto. Alinea cada par de WAV mediante correlación
cruzada de ambos canales; calcula el SNR en ventanas de 5 s, incluyendo la cola,
sin resampleo ni ajuste de ganancia. Exige ≥40 dB en cada ventana.

Los archivos de audio y trazas se escriben fuera del repositorio; los sellos en
`build/reference/` y los informes en `build/port/` contienen solo hashes,
configuración y métricas. La caché incluye hashes del adaptador, comparador,
BIOS, ejecutable y escena, y verifica los ocho archivos de las dos corridas.
Una captura fallida elimina el sello anterior.

## Bloqueo de la referencia PCM

La sonda `/tmp/dw1-3-5-gate-v2/disk_probe.py` ejecuta dos corridas con backend
SDL `disk`, `UseNullSync=false`, `Mute=false`, `Speed=1`, `Interp=2`,
`KeyOnDelay=6` y `SDL_AUDIO_DISK_TIMESCALE=4`. SDL negocia F32LE, estéreo,
44,1 kHz y periodos de 64 frames. La conversión a PCM16 invierte la división
por 32767 del sink, sin resamplear. `KeyOnDelay=6` es el valor de Redux, pendiente
de verificar contra hardware.

El código del SHA fijado confirma que el callback avanza el reloj maestro con
NullSync desactivado. También rellena con ceros si faltan datos en su cola.
**La hipótesis de un sink sin alteraciones temporales no está verificada.**
En el primer tramo se observaron los siguientes resultados:

| Evidencia | Corrida 1 | Corrida 2 |
|---|---|---|
| Ciclo inicial | 4877936398 | 4877936398 |
| Ciclo final | 5820069761 | 5820069761 |
| VBlanks | 1670 | 1670 |
| Frames PCM | 1226736 | 1226736 |
| Frames no nulos | 345725 | 345725 |
| SHA-256 de traza | `5e9b08922f02ae8543365e8261bb4112b14762a9d8db20d6538bf96eee33a761` | igual |
| SHA-256 de WAV | `6666b0c286926b9b354987412ca2503d32815cc57ec043a50912ddcc46677f7d` | `e5bf3f1d4c1b9a496d8b8f8a991ed51905a1027608718745cffb8bdbda1fa200` |

La secuencia de frames no nulos es idéntica byte a byte, con hash
`54d60faf0488bccd474008f464c3bb51d5ee485a666a1663ce5c991500416c26`,
pero los ceros ocupan posiciones distintas. La comparación referencia contra
referencia falla: primera ventana, SNR −4,924 dB, alineación −163 muestras.
Este resultado es compatible con rellenos del sink, pero no identifica por sí
solo todos los lugares donde se originan. Quitar los ceros alteraría el audio
y no es una solución válida. Estas capturas no se aceptan como referencia.

Las dos corridas terminaron el tramo 2 en el VBlank 18760. Sus 39760 escrituras
también son idénticas, con desfase máximo/medio de 0 muestras y hash
`508bfbc3612abea56809282ddad5df6f583d4dc02e5cff5310741aaf4aaf895c`.
Los WAV vuelven a diferir: `dd497fed680440de6714b3b3fdef7a3fd0d7929946ef7be237aeb3a269c929fc`
y `975e44ca216b3ea268a3c8891421071f00d184c507b08ce2d99b139650bcba06`.
La peor ventana da −0,917 dB, con alineación −144 muestras. El tramo 1 contiene
50967 escrituras, también con desfase máximo/medio de 0 muestras. El informe
completo externo es `/tmp/dw1-3-5-disk-probe/comparison.json`.

Al pasar esos archivos reales por el validador de producción, la captura se
rechaza con `non-deterministic reference segment 1 wav`; no queda
`build/reference/title_audio/audio.json`. La validación posterior reutiliza
los archivos de la sonda, sin lanzar una tercera pareja de emuladores.

## Traducción del paso (1), parcial

El checkpoint `04fbf9d` ya traducía `SsVabOpenHeadSticky`, `SsVabTransBody`,
`SsVabTransCompleted`, `SsVabClose`, `SsUtGetVBaddrInSB` y `SsSetMVol`.
Este pase traduce los cuerpos de `SsEnd`, `SsInit`, `SsQuit`, `SsSeqClose`,
`SsSeqStop`, `SsUtAllKeyOff`, `SsSeqPlay`, `SsSeqSetVol`, `SsSetTableSize`,
`SsSetTickMode`, `SsSeqOpen`, `SsUtKeyOffV`, `SsUtAutoVol` y `SsUtKeyOnV`.
Esto no equivale a completar sus dependencias de reproducción: falta el
secuenciador llamado por el tick de `SsStart`.

`libsnd.c` contiene las entradas públicas y el estado VAB. `libsnd_vm.inc`
contiene las rutas de voces alcanzadas por las utilidades explícitas;
`libsnd_seq.inc`, la inicialización, delta VLQ, parada y volumen de SEQ;
`libsnd_pitch.inc`, la tabla literal `_svm_ptable` de DW1. Cada función
traducida identifica su fuente con un comentario `asm:`.

El estado usa punteros nativos y estructuras tipadas: `SndBank[16]`,
`SndSeq` de `0xb0` bytes y **`SndVoice` de `0x36` bytes**, con aserciones de
layout. DW2 usa `0x38`; no se reutiliza su registro de voz. El VH es mutable,
como en el asm. Las tablas del llamador conservan el stride SEQ de DW1 en
los targets i686 y WASM32.

Las rutas nuevas de voces incluyen `_SsVmInit`, `_SsVmVSetUp`,
`_SsVmDoAllocate`, `_SsVmKeyOnNow`, `vmNoiseOn`, `note2pitch2`,
`_SsVmKeyOffNow`, `_SsVmSeqKeyOff`, `_SsVmSetSeqVol`, `SeAutoVol`,
`SetAutoVol` y el vaciado de registros/ENVX de `_SsVmFlush`. Las escrituras
staged de volumen, pitch, dirección, ADSR, KON/KOFF, ruido y reverb se aplican
al hacer tick. La selección automática de voces para notas SEQ sigue pendiente.
No hay entrada alcanzada que active auto-pan; esa rama de `_SsVmFlush` no
está traducida. Las rampas AutoVol conservan el contexto compartido de
programa/tono del asm: no se corrige ese comportamiento a uno por voz.

`SsStart` configura RCnt2 mediante `SetRCnt`/`InterruptCallback` del reloj
existente, incluida la división por dos para las frecuencias bajas y la ruta
VSync. `SS_TICK240` usa el target `0x44e8`. Su callback solo ejecuta el
prólogo de voces; no consume eventos SEQ. `GetVideoMode`, añadido a libetc,
devuelve el estado inicial NTSC de `_etc_vmode` de DW1 USA; no se añadió una
ruta PAL que el juego no alcanza.

En libspu se conservan el allocator, DMA/FIFO, transferencias y volumen común
del checkpoint. Se añaden `SpuInit`, `SpuStart`, `SpuQuit`, `SpuSetKey`,
`SpuSetVoiceAttr`, `SpuGetVoiceEnvelope`, `SpuSetNoiseVoice` y
`SpuSetNoiseClock`, con las rutas alcanzadas de `_SpuInit`, `_spu_init`,
`_spu_note2pitch` y `_SpuSetAnyVoice`. `SpuVoiceAttr` conserva sus 64 bytes y
los offsets SDK, verificados por la prueba de ABI. Los modos ADSR se toman
del asm: release 3 es lineal y solo release 7 activa el bit exponencial.
`SpuInit` conserva la RAM excepto los 16 bytes de ADPCM dummy en `0x1000`.
El evento virtual existente de DMA sustituye la instalación de eventos BIOS;
`SpuQuit` deshabilita su entrega, sin borrar RAM ni voces.

`SsVabOpenHeadWithMode` solo tiene la rama sticky alcanzada; no se traduce la
asignación automática. Los helpers de transferencia solo cubren escritura
DMA/FIFO, reserva y modo inicial de ocho bytes por unidad: no lectura DMA,
callbacks configurables ni la cola diferida `_spu_env`, que ninguna entrada
alcanzada habilita. La protección de memoria de reverb de 3.4 se conserva.

## Fuente y diferencias respecto del original

Atribución: el código nuevo se escribió contra
`asm/us/main/psyq/{libsnd,libspu,libetc}.s` de DW1 USA y sus tablas en
`asm/us/main/data/psyq/`. Se consultó la arquitectura de `Wyrelade/DW2-PC`
(CC0 1.0), pero no se copió ni adaptó código de sus bibliotecas de sonido.
Se conserva la atribución CC0 del backend SPU existente. No se integró el
borrador m2c anterior. No se modificaron `src/` ni `include/`.

Detalles conservados del asm: estados VAB 0/1/2, reserva global entre apertura
y transferencia, programas compactos escritos en el VH y extremos acumulados
de muestras, 64/128 programas según versión/firma y muestras de cuatro/ocho
bytes según versión. El DMA redondea a 64 bytes; el llamador debe proporcionar
ese relleno. Abrir un SEQ inválido reserva el bit de su slot antes de fallar;
el slot 31 no se acepta. `SsSeqSetVol` solo recalcula voces con `flags == 1`;
fuera de ese estado almacena los argumentos sin recortarlos. La ganancia de
SEQ es cuadrática y la de `SsUtKeyOnV` lineal. Los divisores de pan difieren:
63 en key-on/volumen SEQ y 64 en AutoVol.

Diferencias defensivas explícitas: se rechazan VH/SEQ/tablas nulos, bancos e
índices fuera de rango, tablas mayores de 32 secuencias, tonos compactos
inválidos y divisores cero de tempo/resolución/AutoVol/pan. El asm puede
acceder fuera de rango o provocar una excepción en esos casos. El allocator
rechaza tamaños que redondean a cero y conserva su registro terminal al
agotar exactamente la RAM. La API no tiene longitudes VH/VB/SEQ: los buffers
válidos y el fin de los VLQ siguen siendo responsabilidad del llamador.
Estas defensas no se usan para afirmar paridad con la consola.

Hipótesis y límites sin verificar contra el original:

- Se hereda el coste provisional de cuatro ciclos CPU por byte DMA. No se
  calibró el tiempo de las llamadas ni las esperas de inicialización de SPU;
  los bucles de espera de CPU del asm se omiten según el límite de 3.10.
- El port usa el evento DMA del reloj en lugar de los eventos y registros de
  bus BIOS. No modela las secciones críticas de CPU de inicialización/fin ni
  la cadena alternativa de `SsStart2`, fuera de las entradas pedidas.
- No se verificó reinicializar SPU con una transferencia ya pendiente, ni la
  equivalencia de esas sustituciones con interrupciones del original.
- La actualización de volumen de voces SEQ está traducida pero todavía no
  tiene una prueba con una voz creada por el secuenciador. Solo se probaron
  los registros de control SEQ y las voces explícitas `SsUt*`.
- No se ejecutó `title_audio` con disco, no se verificaron sus 44 llamadas de
  efectos ni la paridad de trazas/PCM. Las pruebas sintéticas no demuestran
  que la escena real alcance sus checkpoints con estas inicializaciones.

## Pendiente exacto de esta continuación

De las 15 entradas pedidas, **`SsStart` sigue parcial**, junto a su callback
`SsSeqCalledTbyT`. Ambas conservan el marcador `stub-m2:`. Sus temporizadores
y vaciado de voces funcionan; faltan el recorrido de secuencias y el despacho
MIDI. Los cuerpos de las otras 14 están traducidos, pero las operaciones
`SsSeqOpen`/`SsSeqPlay` aún no forman una ruta de música funcional.

La continuación debe traducir estas rutinas y sus dependencias alcanzadas:

- Despacho: resto de `SsSeqCalledTbyT`, `_SsSndPlay`, `_SsSeqPlay`,
  `_SsGetSeqData`, `_SsSeqGetEof`, `_SsGetMetaEvent`, `_SsNoteOn`,
  `_SsSetProgramChange`, `_SsSetPitchBend` y `_SsSetControlChange`.
- Transiciones: `_SsSndNextSep`, `_SsSndPause`, `_SsSndReplay`,
  `_SsSndCrescendo`, `_SsSndDecrescendo` y `_SsSndTempo`.
- Controles: `_SsContBankChange`, `_SsContDataEntry`, `_SsContMainVol`,
  `_SsContPanpot`, `_SsContExpression`, `_SsContDamper`, `_SsContExternal`,
  `_SsContNrpn1`, `_SsContNrpn2`, `_SsContRpn1`, `_SsContRpn2`,
  `_SsContResetAll` y `_SsSetNrpnVabAttr0` a `_SsSetNrpnVabAttr19`.
- Voces de SEQ: `_SsVmAlloc`, `_SsVmKeyOn`, `_SsVmKeyOff`, `_SsVmSeKeyOn`,
  `note2pitch`, `_SsVmPBVoice`, `_SsVmPitchBend`, `_SsVmSetProgVol`,
  `_SsVmSelectToneAndVag`, `_SsVmSetVol` y `_SsVmDamperOn`.
- Atributos alcanzados por los controles: `SsUtGetProgAtr`, `SsUtGetVagAtr`,
  `SsUtSetVagAtr`, `_SsUtResolveADSR`, `_SsUtBuildADSR`,
  `SsUtSetReverbDelay`, `SsUtSetReverbDepth`, `SsUtSetReverbType`,
  `SsUtSetReverbFeedback`, `SsUtReverbOff` y `SsUtReverbOn`.

Los helpers de voces explícitas ya traducidos deben reutilizarse donde el
asm lo permita, sin asumir el layout de DW2. También queda por cubrir cualquier
ruta adicional de libspu que alcance esa traducción de controles; no se declara
completo su cierre transitivo. La cadena alternativa `_SsTrapIntrVSync` de
`SsStart2` no es alcanzada por las 21 entradas.

Faltan las pruebas L1 de eventos note-on/off, program change, tempo, bucles
y fin de SEQ con escrituras SPU en ticks calculados aparte. La prueba SEQ
actual solo verifica cabecera, delta VLQ, tiempos iniciales y estado de
apertura/reproducción/parada/cierre/volumen; no sustituye esa aceptación.

## Verificación en el contenedor

Los tres builds se ejecutan con `cmake --build --preset <preset>` y las
suites completas con `ctest --preset <preset> -j4`. Los resultados de esta
continuación y las repeticiones tras revisar ADSR se registran debajo.

`test_libsnd.c` sigue siendo L1 sin disco. Conserva VAB, estados/reservas,
mutaciones VH, transferencia/VB/relleno DMA, allocator y volumen común.
Añade layouts ABI, máscaras/pitch/notas/ADSR/ruido de `SpuVoiceAttr`,
key-on/off diferidos para voces bajas y altas, pitch fraccional, rampas
AutoVol rápidas/lentas y AllKeyOff. El SEQ sintético verifica los registros
indicados arriba. El reloj verifica 240 vaciados de voces en un segundo
virtual y ausencia de escrituras tras `SsEnd`. Los valores esperados se
calcularon desde el asm DW1, sin DW2 ni disco como oráculo.

El build ejecuta `gen_stubs.py`: cero símbolos PsyQ ausentes del enlace en
los tres presets. Este contador no representa cobertura funcional: quedan
los dos marcadores M2 explícitos anteriores. La infraestructura de traza,
`run.py`, `title_audio.toml`, CI y el tap PCM se conservaron. No se ejecutó
el build matching, reservado al orquestador con `cc_mips.dll`.

Resultados de las suites completas de este pase:

| Preset | Build | Pasan | Fallan | Omitidas |
|---|---|---:|---:|---:|
| `native-headless` | correcto | 44 | 1 | 9 |
| `native-asan` | correcto | 42 | 1 | 9 |
| `web` | correcto | 25 | 0 | 3 |

El único fallo nativo/ASan es `scene_title_audio_compare`:
`missing/stale audio reference evidence; run capture.py --audio`. El fixture
`reference_title_audio` se omite sin disco, pero existe un sello cuyos archivos
externos ya no están vigentes. No se borró ni sustituyó esa evidencia para
convertir el fallo en una omisión, ni se modificó el comparador. Las demás
omisiones requieren disco/referencia. No se declara la suite nativa verde.

Tras corregir el bit de release exponencial al revisar el asm, se reconstruyen
los tres presets. Se repiten SPU, libspu, libsnd, av_stubs y las regresiones de
arranque/escenas: 10/10 (incluido el fixture pack) en nativo y ASan, sin
diagnósticos ASan/UBSan. La suite web completa se repite con esa corrección:
25 pasan y 3 se omiten, sin fallos.

Logs completos: `/tmp/libsnd-{native-headless,native-asan,web}-ctest.log`.
Builds finales: `/tmp/libsnd-final-{native-headless,native-asan,web}-build.log`.
Repeticiones finales: `/tmp/libsnd-final-{native-headless,native-asan,web}-ctest.log`.
`git diff --check` pasa. No se hicieron commits.
