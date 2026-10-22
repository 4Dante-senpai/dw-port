# Stubs M2 con comportamiento definido

Quedan 11 entradas del juego con comportamiento M2 o parcial. Las entradas M2
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

Video: `CdRead2` y el anillo `St*` consumen sectores reales del Pack y publican
frames STR completos; el audio intercalado se decodifica y alimenta al SPU.
Ver [xa.md](xa.md). Las seis entradas de `libpress` usan el decodificador
MDEC y VLC; ya no son stubs M2. Tablas del Pack, atribución, costes
provisionales y límites se documentan en [mdec.md](mdec.md).
El ancho 320 se convierte en 480 palabras VRAM para RGB24; 20 slices de
24×240 completan el frame. Los callbacks son eventos del reloj sin recursión.

`av_stubs` conserva la prueba del consumidor original `MOV_playMovie(0)` con
el Pack sintético sin frames válidos: EOF produce el frame terminal negro,
20 slices y 10 VBlanks con los costes actuales. Es una regresión del port,
no una medida del hardware. `cd_stream` verifica por separado sectores válidos,
publicación de varios chunks, wrap del anillo, contrapresión, XA y EOF parcial.

Al eliminar el último stub generado, el arranque sin disco ya no emite trazas
de llamadas pendientes. `port_frame_done` registra `frame N` al alcanzar el
límite del Host, y `frame0` exige esa entrada ordenada por ciclos. Esto conserva
la evidencia del arranque sin depender de que queden funciones sin implementar.
