# Historia 3.5: estado de implementación

**2026-10-19: incompleta, traducción parcial del paso (1).** Se tradujeron seis
entradas de libsnd y sus dependencias de transferencias/asignación/volumen de
libspu. Quedan 15 entradas, el secuenciador y el administrador de voces. La
infraestructura previa de captura y comparación se conserva. Este pase no
completa los criterios de aceptación de la historia.

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

Las entradas terminadas son `SsVabOpenHeadSticky`, `SsVabTransBody`,
`SsVabTransCompleted`, `SsVabClose`, `SsUtGetVBaddrInSB` y `SsSetMVol`.
`SsVabOpenHeadWithMode` está traducida solo por su rama sticky, la única
alcanzada por las entradas terminadas; la rama de asignación automática sigue
pendiente. El estado VAB vive en `SndBank[16]`, con punteros nativos, sin
almacenarlo en direcciones fijas de RAM PS1. El VH es mutable, como en el asm.

En libspu se tradujeron `SpuInitMalloc`, `SpuMalloc`, `SpuFree`,
`SpuSetTransferStartAddr`, `SpuSetTransferMode`, `SpuWrite`,
`SpuIsTransferCompleted`, `SpuSetCommonAttr`, y las rutas internas alcanzadas de
`_spu_gcSPU`, `_SpuIsInAllocateArea_`, `_spu_FsetRXXa`, `_spu_Fw`, `_spu_t`,
`_spu_FwriteByIO`, `_spu_FiDMA`, `_spu_setInTransfer` y `_spu_getInTransfer`.
No son traducciones completas de todas las rutas de esos helpers: se cubren
DMA de escritura, FIFO, reserva de transferencia y modo de memoria inicial
(ocho bytes por unidad). No hay callbacks de transferencia configurables ni
lectura DMA en la API de libspu de este pase. Se integra la lista de asignación
con la protección de reverb de 3.4. Cada función tiene comentario `asm:`.

Atribución: las traducciones nuevas de `libsnd.c` y `libspu.c` se escribieron
contra `asm/us/main/psyq/{libsnd,libspu}.s` de DW1 USA y las tablas de datos
correspondientes. Se consultó la arquitectura de `Wyrelade/DW2-PC` (CC0 1.0),
pero no se copió ni adaptó código de sus bibliotecas de sonido. La atribución
CC0 del backend SPU existente se conserva. El borrador m2c previo no se integró.
El administrador de voces sigue pendiente: su stride DW1 es `0x36`, frente a
`0x38` de DW2; no se introdujo ningún registro de voz de DW2.

Puntos de entrada del asm utilizado (líneas de `asm/us/main/psyq/`):
`libsnd.s`: `SsSetMVol` 4374, `SsUtGetVBaddrInSB` 5619, `SsVabClose` 10195,
`SsVabOpenHeadWithMode` 10233, `SsVabOpenHeadSticky` 10509,
`SsVabTransBody` 10526 y `SsVabTransCompleted` 10581.
`libspu.s`: `_spu_gcSPU` 948, `SpuInitMalloc` 1164,
`_SpuIsInAllocateArea_` 1237, `SpuMalloc` 1283, `SpuFree` 1479,
`SpuWrite` 2890, `SpuSetTransferStartAddr` 2921, `SpuSetTransferMode` 2952,
`SpuIsTransferCompleted` 2971 y `SpuSetCommonAttr` 3054.

Detalles verificados en el asm: estados VAB 0/1/2 (libre/transferido/cabecera),
reserva global entre apertura y transferencia, 64 o 128 programas según firma
y versión, tamaños de muestras en unidades de cuatro u ocho bytes según
versión, y número de muestras leído con `lbu`. Los índices compactos de
programas y extremos acumulados de muestras se escriben en el VH. La dirección
pública solo se devuelve en estado 1, y cerrar un estado 2 no lo libera.
El DMA redondea el tamaño a 64 bytes: el buffer fuente debe incluir ese relleno,
como en el contrato original. `SpuSetTransferStartAddr` acepta
`0x1010..0x7fff8`, redondea hacia arriba a ocho bytes y no escribe TSA hasta
`SpuWrite`. El volumen maestro multiplica los argumentos firmados por 129.

Diferencias defensivas explícitas: puntero VH nulo e identificadores negativos
menores que −1 se rechazan; las consultas/transferencias rechazan id 16 aunque
el asm deja pasar ese índice fuera del arreglo. El allocator rechaza solicitudes
que redondean a cero y conserva el registro terminal de tamaño cero al agotar
exactamente RAM; el asm puede corromper la lista en esos casos. Estas diferencias
no constituyen una hipótesis sobre la consola ni se usan para afirmar paridad.
La API original no proporciona longitud del VH/VB: los buffers válidos siguen
siendo responsabilidad del llamador; las pruebas incluyen el relleno DMA.

Hipótesis no verificadas: se hereda el coste provisional del backend de cuatro
ciclos CPU por byte DMA. El evento de fin de libspu usa ese mismo vencimiento;
no hay calibración contra PCSX-Redux ni evidencia de paridad de trazas/PCM.
Las nuevas funciones no inicializan por sí solas el SPU: `SsInit`/`SpuInit`
siguen pendientes. `src/` e `include/` no se modificaron.

Pendientes exactamente las siguientes 15 entradas de libsnd, con sus helpers:
`SsEnd`, `SsInit`, `SsQuit`, `SsStart`, `SsSeqClose`, `SsSeqStop`,
`SsUtAllKeyOff`, `SsSeqPlay`, `SsSeqSetVol`, `SsSetTableSize`,
`SsSetTickMode`, `SsSeqOpen`, `SsUtKeyOffV`, `SsUtAutoVol`, `SsUtKeyOnV`.
El recorrido estático de los `jal` directos desde las 21 entradas identifica
ocho entradas públicas pendientes de libspu:
`SpuInit`, `SpuStart`, `SpuQuit`, `SpuSetKey`, `SpuSetVoiceAttr`,
`SpuGetVoiceEnvelope`, `SpuSetNoiseVoice`, `SpuSetNoiseClock` y sus helpers.
Ese recorrido no resuelve llamadas indirectas; su cobertura sigue pendiente.
Faltan las pruebas L1 de tick/SEQ/voz; `test_libsnd.c` cubre VAB, transferencias,
asignación/liberación y volumen común. Ninguna entrada pendiente perdió su
marcador `stub-m2:` ni se declaró terminada.

## Verificación en el contenedor

Los builds `native-headless`, `native-asan` y `web` compilan en este contenedor.
Se ejecutaron sus suites completas mediante `ctest --preset <preset>`.
La primera ejecución nativa y ASan detectó cinco regresiones de arranque:
`frame0`, `watchdog`, `scene_flags`, `scene_state_unit` y `scene_audio_runtime`.
La causa era cancelar un evento inexistente durante el reset de libspu: esa
llamada asignaba el reloj al hilo principal antes de crear el hilo del juego.
Se corrigió la cancelación y se reconstruyeron los tres presets. La repetición
de esas cinco pruebas, las cuatro de SPU/libsnd y su fixture `pack` pasa 10/10
en ambos presets nativos. No hay diagnósticos ASan/UBSan. Las pruebas finales
SPU/libspu/libsnd en Node/WASM pasan 3/3.

`test_libsnd.c` es L1 y no usa disco: comprueba cabeceras VAB sintéticas de
versiones 4/5 y formatos 0x70/0x71, estados/reservas, los 16 bancos, tablas
mutadas, copia de VB y relleno DMA, fin de transferencia, FIFO, wrap de RAM,
fragmentación/límites/liberación del allocator, protección de reverb y máscaras
de volumen común. Los valores esperados se dedujeron del asm de DW1.
El build regenera `psyq_stubs.c` con `gen_stubs.py`; no quedan símbolos PsyQ
ausentes del enlace, aunque sí los 15 stubs M2 de libsnd enumerados arriba.

Logs de las suites completas: `/tmp/dw1-libsnd-ctest-{native,asan,web}.log`.
Builds finales: `/tmp/dw1-libsnd-reset-build-{native,asan,web}.log`.
Repeticiones tras la corrección:
`/tmp/dw1-libsnd-reset-tests-{native,asan,web}.log`.
Las pruebas que requieren disco/referencia se omiten sin `DW1_DISC_DIR`.
No se verificó la paridad de audio del juego. No se ejecutó el build matching
porque no está disponible `cc_mips.dll`. `git diff --check` pasa;
no se hicieron commits ni se reescribió la infraestructura de captura.
