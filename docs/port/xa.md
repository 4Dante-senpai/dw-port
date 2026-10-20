# XA y streaming STR (3.6)

El decodificador en `port/backend/xa.c` deriva de
[Wyrelade/DW2-PC, backend/xadec.c](https://github.com/Wyrelade/DW2-PC), CC0 1.0.
La descripción independiente y las tablas zigzag provienen de
[psx-spx, CDROM XA Audio ADPCM](https://psx-spx.consoledev.net/cdromformat/#cdrom-xa-audio-adpcm-compression).
No se incorpora código GPL del emulador al port.

## Modelo

Se admiten 4/8 bits, mono/estéreo y 37.800/18.900 Hz. Los dos historiales
ADPCM persisten entre sectores. El interpolador tiene siete fases de 29 taps:
cada seis entradas produce siete pares PCM16 a 44.100 Hz. Mono alimenta ambos
canales. A 18.900 Hz se inserta primero el promedio de la muestra anterior y
la actual, y luego la actual. Esta diferencia frente a la duplicación de DW2-PC
se comprobó en la implementación de la revisión fijada de Redux; psx-spx documenta
las tablas pero no constituye evidencia explícita de ese promedio. La paridad
con disco cubre el coding 0x01 de DW1; los demás formatos tienen oráculo L1.
Los bits reservados 1, 3, 5 y 7 de coding descartan el sector; libcd lo registra
con `xa rejected coding=... lba=...`.

`cdrom_stream_sector` reutiliza `pack_read` posicional, sin cursor compartido ni
acceso directo al archivo. Reconstruye Size1 tanto para F2 como para datos F1.
`CdRead2` agenda sectores a 75 o 150 Hz del reloj virtual. En modo RT los F2 con
submodo Audio|Form2 se entregan al decodificador después de sincronizar el SPU;
los restantes pasan al anillo. `CdInit` reproduce `CD_initvol` (0x800B16B8):
volúmenes CD 0x3fff y SPUCNT 0xc001; inicializa los volúmenes principales solo
si ambos niveles actuales son cero. Se verificó en el ensamblador original y
en las escrituras de Redux antes de MOV_playMovie. El callback CD usa esos
registros existentes del SPU.
SF y Setfilter siguen rechazados.

El anillo conserva cabeceras de 32 bytes y chunks contiguos de 2016 bytes,
estados libre/construcción/publicado/prestado y marcador de vuelta. Solo se
publican frames completos. Un frame prestado retiene sus slots hasta StFreeRing.
Si no hay espacio se reintenta el mismo LBA en el siguiente periodo, sin volver
a decodificar audio anterior. EOF descarta el frame parcial y devuelve un
encabezado terminal tras los frames pendientes. `MOV_playMovie` consume los
frames originales sin cambios en src/include; libpress sigue generando negro.
El avance de un periodo en StGetNext evita millones de iteraciones del busy-loop;
es una hipótesis de planificación, pendiente de calibración en 3.10.

La FIFO contiene 16384 pares; el comportamiento heredado ante overflow descarta
el más antiguo. La cadencia del STR y la sincronización del SPU deben impedir
este caso en el juego. Los contadores de underrun excluyen silencio inicial y
posterior al fin. Pausa cancela el evento y conserva el PCM ya decodificado.

## Evidencia

`xa_vectors.py` es un oráculo escalar independiente: calcula ocho hashes fijos
a partir de dos sectores sintéticos por formato, incluyendo continuidad ADPCM
e interpolación. `test_xa` compara SHA-256 y tamaños, rechaza coding inválido y
verifica el WAV. `test_cd_stream` construye un Pack sin datos comerciales y
verifica el ritmo, la contrapresión, el enrutamiento XA, wrap y EOF.
Estas pruebas se registran en CTest nativo, ASan/UBSan y Node/WASM.
El runner de escenas reserva 40.000 frames, igual que la captura de referencia y
audio: el límite anterior de 10.000 cortaba nueva partida durante los STR reales.

`xa_op1.toml` define una única ventana desde la entrada hasta el retorno de
MOV_playMovie, con el mismo mecanismo de instrumentación del port y breakpoint
RA en Redux. La referencia usa Speed=1. El segundo tap guarda la salida de
FeedXA antes de voldiv (formato DW1XA1: ciclo u64 y par s16 LE); el del port,
DW1_XA_WAV, guarda la salida del decodificador antes del SPU. La matriz ATV y las
escalas analógicas de Redux no se han añadido al modelo del port; su efecto debe
medirse con el gate, sin ajustar ganancia durante la comparación.

Bloquean SNR XA >=40 dB en toda la ventana alineada por correlación y la secuencia
ordenada completa de todas las escrituras SPU (incluidos registros globales).
El WAV mezclado, regla B y desfases se reportan como diagnóstico. Dos pasadas
verifican igualdad byte a byte de cada PCM y traza antes de aceptar la referencia.
Una referencia ausente devuelve 77. Los WAV/trazas quedan fuera del repositorio;
build/reference y build/port contienen solo hashes y métricas de comparación.

## Verificación local (2026-10-20)

Se compilaron los presets `native-headless`, `native-asan` y `web` dentro del
contenedor. Los ocho vectores y el Pack sintético pasan en los tres; también
pasan las pruebas de anillo, libcd, stubs y adaptadores de captura. `make compare`
informa `Compiled binaries match with target binaries`, sin cambios en src/include.

`reference_xa_op1` produjo dos trazas y dos PCM (mezclado y XA) idénticos byte a
byte. SHA-256 del WAV XA de referencia:
`f0f79b7555aeecbf644f9db24ee0540d5746222aadd2e9508a5244cfb32c8f15`.
`scene_xa_op1_compare` en nativo y ASan/UBSan compara 5.346.096 pares, con desfase cero, longitudes
iguales y SNR de **54,424193 dB** sobre una sola ventana completa. WAV XA del port:
`707139f118d3b3a4b4da0daa72042784bc8c8a488ee09756537e536253f6e0b4`.
Ambas ventanas terminan por retorno de MOV_playMovie; hay **cero escrituras SPU**
dentro de ambas ventanas, con comprobación de todos los registros y sin recortar
colas. Las escrituras de inicialización CD verificadas ocurren antes de la entrada.

Los límites de la ventana mezclada difieren: 5.352.599 muestras en el port y
5.367.690 en Redux. El PCM mezclado no alcanza paridad (incluye ventanas con SNR
−inf); queda explícitamente diagnóstico conforme a la decisión 2. La prueba XA
no usa su ganancia, silencio ni duración para ajustar o recortar el decodificador.

La imagen Docker no pudo reconstruirse aquí porque el contenedor carece de
cliente/daemon Docker. Se recompiló e instaló localmente PCSX-Redux de la misma
revisión fijada con el parche actualizado, y se verificó que el parche aplica
sobre sus fuentes originales. Antes de ejecutar CI con el tap nuevo hace falta
reconstruir/publicar la imagen y fijar su nuevo digest; no se publicó ninguna imagen.

Las regresiones `scene_title`, `scene_title_compare` y
`scene_title_audio_compare` pasan en nativo y ASan/UBSan. La referencia de nueva
partida se recapturó dos veces con el presupuesto de 40.000 frames y produjo
estados idénticos; `scene_newgame_state` pasa en nativo y ASan/UBSan (183,49 s y
669,08 s respectivamente, dos ejecuciones del port por preset). El grupo nativo con disco,
referencias y audio del título terminó con 7/7 pruebas, sin omisiones. La suite
ASan/UBSan restante terminó con 47/47, también sin omisiones. En WebAssembly se
corrigió el acceso al Pack sintético mediante MEMFS/Node y se volvieron a ejecutar
los tests afectados; los ocho vectores XA y `cd_stream` pasan.

`StGetBackloc` se contrastó con el ensamblador: conserva la ubicación del último
chunk, avanza un sector BCD y devuelve el ID del frame publicado sin sumarle uno.
El test multichunk verifica ambos valores en los tres presets.
