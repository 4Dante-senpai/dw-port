# SPU y libspu (historia 3.4)

`port/backend/spu.{c,h}` adapta `Wyrelade/DW2-PC/backend/psxspu.{c,h}` y
`psxspu_gauss.inc`, CC0 1.0 (submódulo fijado en `reference/dw2-pc`). No requiere
SDL ni datos del disco. Implementa 512 KiB, 24 voces ADPCM, interpolación gaussiana,
ADSR, ruido, modulación de pitch, KON/KOFF/ENDX, reverb a 22,05 kHz y transferencias
FIFO/DMA. Los offsets de registros son relativos a `0x1f801c00`.

Cada acceso a registros o DMA sintetiza primero hasta `clock_now()/768`.
`spu_sync()` permite drenar sin acceder a registros; el observador de VBlank del
Host lo hace incluso sin llamadas de sonido. No se acumulan fracciones flotantes:
35.964 VBlanks producen exactamente 26.459.973 frames estéreo. El avance rápido
con registros intactos y RAM cero conserva exactamente el LFSR de ruido y la fase
y posición de reverb; una prueba lo contrasta con síntesis muestra por muestra.

El callback de CD nulo aporta silencio. El callback de salida nulo descarta PCM;
el Host headless utiliza este valor predeterminado. No hay decodificador XA,
secuenciador `libsnd`, resampleo ni dispositivo de audio en esta historia.
Los callbacks se ejecutan en el hilo del reloj y no deben reentrar al SPU.
El acceso directo a `spu_ram` sirve para cargar fixtures; el código del juego usa
FIFO/DMA. Las transferencias que exceden la RAM abortan en debug.

Las seis entradas utilizadas por DW1 se traducen del asm indicado en cada función:
`SpuClearReverbWorkArea`, `SpuGetKeyStatus`, `SpuSetReverb`, `SpuSetReverbDepth`,
`SpuSetReverbModeParam` y `SpuSetReverbVoice`. Se conservan máscaras selectivas,
los cuatro estados de voz, presets, modo CLEAR_WA y el caso especial de modo 0
(128 bytes finales). Las rutinas internas usan el estado inicial de la biblioteca:
lista de asignaciones vacía y registros inmediatos. El allocator, cola diferida,
`SpuInit`, `SpuSetVoiceAttr`, `SpuWrite` y sus consumidores `libsnd` corresponden a
3.5, según la decisión de alcance de la spec. El Backend ya ofrece DMA y consulta
de finalización, sin exponer anticipadamente esas entradas PsyQ. El reset del port
reinicia tanto Backend como estado de reverb. `src/` e `include/` no cambian.

Las pruebas L1 `spu` y `libspu` corren también como WASM bajo Node. Los hashes
congelados de PCM/ENVX provienen de `tools/port/audio/spu_vectors.py`, calculado
aparte con las ecuaciones de [psx-spx](https://psx-spx.consoledev.net/soundprocessingunitspu/),
sin importar código del sintetizador. Cubren los cinco filtros y shifts 0..12,
loop/repeat/ENDX, fases ADSR, ruido, impulsos y preset ROOM con RAM sintética,
protección del área de trabajo, CD, DMA, sincronía previa a escritura y ausencia
de deriva al fraccionar intervalos. No se presentan estos hashes como comparación
de audio contra consola: esa comparación corresponde a 3.5.

Hipótesis y límites heredados: el coste DMA se fija provisionalmente en cuatro
ciclos CPU por byte y se valida aquí como contrato temporal determinista, pendiente
de calibración contra referencia. Los datos quedan copiados al iniciar DMA, y el
estado ocupado dura hasta ese vencimiento; no se modela arbitraje de bus por byte.
El sweep de volumen con fase negativa conserva la limitación explícita de la base
DW2-PC; las pruebas de esta historia usan volúmenes directos. La secuencia de
reverb calcula ambos canales juntos a 22,05 kHz como la base; la paridad de audio
contra hardware todavía debe medirse en 3.5.

Validación en el contenedor: configuración y build de `native-headless`,
`native-asan` y `web`; CTest de `spu_vectors`, `spu`, `libspu`, `gen_stubs` y
regresión `av_stubs` (nativo y sanitizadores); suite web y `web_scene_title` con
`DW1_DISC_DIR` configurado. La prueba AV anterior se actualiza para usar atributos
válidos y verificar el tiempo DMA y los retornos reales de libspu. El build PS1
con CodeWarrior y GCC cruzado auxiliar termina con `make compare` idéntico.
El título nativo también pasa `reference_title` y `scene_title_compare` contra
dos capturas deterministas de PCSX-Redux. Las suites generales se ejecutan; el
único fallo inicial era el contrato obsoleto de `av_stubs`, corregido y repetido
satisfactoriamente. Las pruebas de disco omitidas en web se repiten con el disco
configurado, incluido `web_scene_title`; en sanitizadores se mantienen omitidas
las pruebas dependientes del disco, cubiertas por el build nativo habitual.

## Captura opcional (infraestructura 3.5)

`spu_capture_begin(FILE *trace, FILE *wav)` abre un tramo relativo al ciclo y
VBlank actuales. Acepta cualquiera de los dos streams o ambos; pertenecen al
llamador. `spu_capture_end()` drena el PCM pendiente, finaliza la cabecera WAV
y propaga errores de escritura/flush/seek. El callback `SpuOutput` continúa
recibiendo las mismas muestras. El WAV usa PCM16 little-endian, estéreo,
44,1 kHz. Las pruebas cubren comienzos entre muestras, cierre, reset del reloj,
errores de E/S y la coexistencia con el callback.

Las trazas registran `(ciclo − origen) / 768`. El gate vigente de 3.5
(decisiones 3/4, 2026-10-19) compara las escrituras de voz ordenadas:
tramo 1 completo entre `runLandingScreen` y `unloadNewGameScene`; tramo 2
desde la primera nota nueva (dirección de muestra + KON, conservando el bundle
entero) tras el primer `SsSeqPlay` posterior a `gameLoop`, hasta el primer
evento de juego de `stop_on`. Cada lado debe ofrecer ≥9 s; se compara el
intervalo común. La regla B por VBlank, desfases y SNR se reportan sin bloquear
hasta 3.10. Se exige la misma causa de cierre en ambos lados; el tramo 1
requiere checkpoint, y cada WAV debe coincidir con el intervalo de su traza
con tolerancia de un frame. Véanse [libsnd.md](libsnd.md) y
[reference.md](reference.md) para los comandos y la evidencia actual.

`spu_capture_note_anchor()` conserva PCM provisional hasta confirmar KON;
recorta los bytes anteriores al origen sin cambiar el callback de salida.
La regresión L1 compara PCM no silencioso exacto con una captura sin recorte,
a ambos lados de la confirmación, cruzando bloques de copia de 4096 bytes.

### Semántica del asignador comprobada contra DW1

Se conserva la reutilización parcial de un bloque libre cuando la tabla está
llena: `0x800C7AAC`–`0x800C7B0C` evita dividirlo y almacena el tamaño pedido,
perdiendo su sufijo. Una prueba L1 fija ese resultado. `SpuInitMalloc` tampoco
comprueba `top == NULL` cuando `num > 0`: las escrituras de `0x800C7738` y
`0x800C7760` son incondicionales. El llamador debe proporcionar una tabla
válida; solo `num <= 0` evita acceder a ella. No se añade un retorno de error
que el original no tiene.
