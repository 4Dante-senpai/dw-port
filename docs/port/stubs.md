# Stubs M2 con comportamiento definido

Quedan 25 entradas del juego con comportamiento M2 o parcial. Las entradas M2
avanzan el reloj una vez; las parciales pueden llamar APIs del reloj/SPU.
Este inventario no implica paridad audiovisual.
El diagnóstico de `title_audio` sobre `e303346` y la corrección de fase
de RCnt2 se registran en [audio-parity.md](audio-parity.md); no cambian este
inventario ni cierran la comparación clase B. La investigación sobre
`b97a378` añade cobertura L1 del recálculo de voces conservadas entre SEQ;
no introduce stubs ni cambia el criterio de aceptación.

| Función | Comportamiento M2 | Historia de reemplazo |
|---|---|---|
| `MemCardInit` | Reinicia el comando pendiente; tarjeta ausente. | 3.8 |
| `MemCardStart` | Tarjeta ausente; no inicia trabajo. | 3.8 |
| `MemCardExist` | Registra McFuncExist y devuelve 1; no accede al buffer. | 3.8 |
| `MemCardAccept` | Registra McFuncAccept y devuelve 1; no accede al buffer. | 3.8 |
| `MemCardReadFile` | Registra McFuncReadFile y devuelve 1; no accede al buffer. | 3.8 |
| `MemCardWriteFile` | Registra McFuncWriteFile y devuelve 1; no accede al buffer. | 3.8 |
| `MemCardCreateFile` | Devuelve McErrCardNotExist; salidas intactas. | 3.8 |
| `MemCardDeleteFile` | Devuelve McErrCardNotExist; salidas intactas. | 3.8 |
| `MemCardFormat` | Devuelve McErrCardNotExist; salidas intactas. | 3.8 |
| `MemCardGetDirentry` | Devuelve McErrCardNotExist; salidas intactas. | 3.8 |
| `MemCardSync` | Sin comando: -1; con comando: 1, McFunc y McErrCardNotExist, sin espera. | 3.8 |
| `CdRead2` | Inicia el stream sintético (1). | 3.7 |
| `StSetRing` | Stream sintético; sin E/S ni callbacks de CD. | 3.7 |
| `StUnSetRing` | Stream sintético; sin E/S ni callbacks de CD. | 3.7 |
| `StSetStream` | Stream sintético; sin E/S ni callbacks de CD. | 3.7 |
| `StCdInterrupt` | Stream sintético; sin E/S ni callbacks de CD. | 3.7 |
| `StFreeRing` | Libera sin efectos (0); buffer estático. | 3.7 |
| `StGetBackloc` | Posición sintética cero; devuelve 0. | 3.7 |
| `StGetNext` | Siempre 0 con el mismo frame negro 320×240 y frameCount 0xFFFFFFFF. | 3.7 |
| `DecDCTReset` | Reinicia callback y salida pendiente. | 3.7 |
| `DecDCTin` | Acepta entrada sin decodificar. | 3.7 |
| `DecDCTvlc2` | Éxito (0); cabecera run-level vacía. | 3.7 |
| `DecDCTvlcBuild` | No construye tablas: el stream es sintético. | 3.7 |
| `DecDCToutCallback` | Instala o elimina callback; devuelve 0. | 3.7 |
| `DecDCTout` | Llena size longs de ceros; callbacks iterativos en el hilo del juego, sin recursión. | 3.7 |

`libds` no tiene funciones llamadas por el Juego; no se agregan entradas.

`libspu`: las seis entradas de la historia 3.4 usan el SPU del Backend;
no son stubs M2. Ver `docs/port/spu.md` y las pruebas `spu`/`libspu`.

Audio de `libsnd` (paso 1 de 3.5, 2026-10-19): las 21 entradas públicas y
sus dependencias alcanzadas están traducidas del asm USA de DW1.
`SsStart`/`SsSeqCalledTbyT` ya despachan SEQ: notas, programas, controles,
pitch, tempo, NRPN/bucles, fin y transiciones. No quedan marcadores M2 en
libsnd. Las pruebas L1 sin disco verifican los ticks y registros SPU de
SEQ/VAB sintéticos y las utilidades `SsUt*`; las entradas necesarias de
libspu ya tienen implementación. Atribución, límites y la sustitución
no verificada para los ADSR de NRPN 11..14 se detallan en
[libsnd.md](libsnd.md#secuenciador-completado-desde-224e9ea).
La paridad de trazas/PCM con disco depende de la referencia externa del
orquestador y no se afirma con estas pruebas sintéticas.

El inventario M2 de esta página es manual, basado en los marcadores `stub-m2:`;
`gen_stubs.py` genera únicamente símbolos ausentes del enlace. El build vuelve
a ejecutarlo: las 21 entradas de libsnd ya estaban definidas como símbolos,
así que su contador de implementadas no mide la retirada de estos stubs M2.
Atribución, diferencias defensivas respecto del asm e hipótesis en
[libsnd.md](libsnd.md). No se afirma paridad de audio ni se cierra 3.5.

Tarjeta: se conserva únicamente la convención de comando/resultado de
`reference/dw2-pc/psyq/libmcrd.c` (CC0, atribución en el código). El último comando
registrado se consume una vez por `MemCardSync`, en ambos modos, con resultado
de tarjeta ausente; los punteros de salida nulos se permiten. `MemCardInit` borra
el comando. No hay acceso a archivos ni a buffers de lectura/escritura. El menú
consulta -1 para iniciar Exist y 1 para consumir su resultado.

Video: no se consumen sectores STR. `StGetNext` publica siempre el mismo encabezado
y bitstream estáticos. El ancho 320 se convierte en 480 palabras VRAM para RGB24;
20 slices de 24×240 completan el frame. `DecDCTout` termina de drenar las salidas
pendientes antes de retornar. Una llamada desde el callback registra la siguiente
salida; cada callback retorna antes de ejecutar el siguiente, conservando el orden
y evitando recursión. Solo se admite una salida pendiente, como en `MOV_onSliceDecoded`.

La prueba L1 `av_stubs` enlaza los objetos originales del overlay `mov` y el Pack
sintético de `test_pack`. `MOV_playMovie(0)` devuelve 1, produce exactamente
20 slices y consume 8 VBlanks con el reloj y los costes actuales. La prueba
verifica los buffers negros antes de cada transferencia, el orden de los slices,
`flipPending` al final del frame, la VRAM y la ausencia de recursión. Esa duración
es una constante de regresión M2, no una medición del hardware original.

Al eliminar el último stub generado, el arranque sin disco ya no emite trazas
de llamadas pendientes. `port_frame_done` registra `frame N` al alcanzar el
límite del Host, y `frame0` exige esa entrada ordenada por ciclos. Esto conserva
la evidencia del arranque sin depender de que queden funciones sin implementar.

Limitación heredada observada: `MOV_playMovie` declara `DRAWENV draw` y lo pasa
a `PutDrawEnv` sin inicializarlo. Esta historia mantiene `src/` intacto como exige
la especificación. ASan/UBSan no verifican lecturas de memoria sin inicializar;
la prueba del frame sintético no demuestra que ese estado de dibujo sea válido
en otros layouts de pila o configuraciones de compilación.
