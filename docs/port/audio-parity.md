# Diagnóstico de paridad de audio sobre e303346

2026-10-19. La comparación AD-15 sigue fallando. No se modifican su regla,
`run.py`, `audio.py`, los inputs, los checkpoints ni el origen de captura.
No hay cambios en `src/`, `include/` ni commits.

## Corrección verificada

`libapi.c:on_target` ponía el origen de RCnt2 en el ciclo de atención del
callback. Una interrupción enmascarada que cruzase el target alargaba todos
los períodos posteriores. Ahora el contador conserva el origen del último
target vencido y su fase de división por ocho. Varios targets vencidos
coalescen en la interrupción pendiente; el próximo target queda en la
retícula original. No se cambia el período de SS_TICK240: `0x44e8 * 8`
ciclos, exactamente 183,75 muestras.

`test_clock` bloquea desde el ciclo 7990 hasta el 25003, relativos al origen,
con período 8000: entrega una interrupción pendiente, lee el residuo correcto
y entrega la siguiente exactamente en 32000. Enlazar esta misma prueba con
`libapi.c` de `e303346` falla en la lectura y en la segunda interrupción.
`test_libsnd` retrasa el primer tick 1992 ciclos y comprueba tanto los 240
ticks en un segundo como la recuperación de fase en el segundo tick. Esta
prueba también se ejecuta en ASan y WASM/Node.

Esta corrección **no cambia las trazas de title_audio**: en estas corridas no
se presenta la latencia que activa el defecto. No resuelve la divergencia
clase B y no debe presentarse como su causa demostrada.

## Primera divergencia y relojes

Se ejecuta la comparación original `compare_trace` sobre las nuevas trazas
y las copias de referencia en `build/audio-debug/ref`. Dos ejecuciones
nativas nuevas producen trazas idénticas entre sí y a la captura nativa
aportada por el orquestador.

| Tramo | Escrituras port / referencia | Primera divergencia AD-15 |
|---|---:|---|
| 1 | 46659 / 50967 | VBlank 0, registro `0x188`, escritura 1: port `0x0000`, referencia ausente |
| 2 | 39856 / 39760 | VBlank 0, registro `0x020`, escritura 0: port ausente, referencia `0x061f` |

La divergencia del orden global del tramo 2 sigue en su quinta escritura:
EON `0x198`, port `0x0027`, referencia `0x002b`. **Es reverb, no ruido**:
NON está en `0x194/0x196`. La regla AD-15 ordena por VBlank y registro, por
eso informa antes la voz ausente en `0x020`.

Se verificaron tres diferencias temporales separadas:

1. `spu_capture_begin` y `capture.lua:audioCheckpoint` usan la entrada al
   mismo checkpoint y restan su ciclo y contador de VBlank. El origen del
   port no tiene una definición distinta. Una instrumentación temporal,
   retirada del árbol final, midió `runLandingScreen` en ciclo 133361431,
   VBlank 236, a 10934 ciclos del VBlank nominal (14,24 muestras). En la
   referencia, el primer tick está en muestra 60 y el siguiente, ya en
   VBlank 1, en muestra 244. El primer intervalo parcial del port contiene
   tres ticks; el de referencia, uno. No se pueden equiparar desplazando
   timestamps sin cambiar el origen aprobado. El costo fijo de entrada
   PsyQ no reproduce la fase de ejecución de las instrucciones MIPS.
2. El port programa 565044,48 ciclos/VBlank, `33868800 * 1001 / 60000`.
   El contador de líneas del Redux local programa
   `floor(33868800 / (60 * 263)) = 2146` ciclos/línea: 564398 ciclos/frame,
   734,893229 muestras. Esto concuerda con la pendiente observada en la
   referencia; en 1200 VBlanks el port introduce aproximadamente 1010
   muestras adicionales, suficientes para otros 5–6 ticks. Se conserva la
   constante del port porque la spec/épica prescribe expresamente
   `44100 * 1001 / 60000` muestras/VBlank. Elegir el período cuantizado de
   este emulador como período del port requiere resolver esa contradicción
   con la calibración de 3.10; no constituye una medición del hardware PS1.
   Fuente inspeccionada: `build/pcsx-redux-src/src/core/psxcounters.cc`,
   commit `2068784a552b598940191e8431585b7f96408b3f`, el mismo fijado por
   el contenedor. No se incorpora código de Redux al port.
3. El estado musical al entrar en `gameLoop` ya está en otra fase. Las dos
   voces iniciales de referencia (2 y 3) aparecen en el port en VBlank 35,
   muestras 26310/26310 frente a 220/221, con los seis atributos idénticos:
   `(061f,061f,0b50,41d6,80ff,5fcf)` y
   `(0088,000d,104a,4472,80ff,5fd4)`. También coinciden los cuatro bundles
   siguientes, en VBlank 49 del port frente a 14 de referencia. El primer
   intervalo entre esos eventos es 10106 muestras en ambos. La diferencia
   inicial de EON no demuestra por sí sola un defecto de asignación de
   voces: compara estados de momentos musicales distintos. Sigue pendiente
   contrastar ENVX/asignación con la misma historia de eventos antes del
   checkpoint; no se infiere su equivalencia de estos seis bundles.

## Duración de escena anterior a libsnd

Se compiló en un worktree separado el commit `d7eeb91`, cuyo libsnd todavía
contiene los stubs, y se ejecutó con el mismo Pack, inputs por checkpoint y
ventanas cero. Solo se observó `FIRST_SCREEN_FRAME_COUNTER`; la salida de
imagen exigida por el antiguo host se dirigió a `/dev/null`.

| Checkpoint relativo a runLandingScreen | Port anterior a libsnd | Referencia newgame |
|---|---:|---:|
| runMainMenu | 164 | 206 |
| newGameScene | 454 | 496 |
| newGameStateMachine | 591 | 677 |
| setupNameSelectorBox | 867 | 953 |
| setupNameSelectorBox#2 | 1110 | 1286 |
| unloadNewGameScene | 1494 | 1670 |
| gameLoop | 2029 | 8918 |

Así se reproduce la diferencia de **176 VBlanks con libsnd todavía en
stubs**. Las trazas históricas `build/newgame2{,-asan,-core}.state` muestran
los mismos valores. La captura actual de audio tiene como último índice
1494, no una duración necesariamente igual a 1494: la instrumentación
midió 1495 entre sus checkpoints (236 a 1731). El último índice de
referencia es 1669 y su metadata declara duración 1670. Se distinguen
índices y duraciones para evitar una corrección artificial de un frame.

La decisión 5 de la spec 3.3 ya documenta que la comparación de estado por
checkpoint admite diferentes VBlanks por costos de CD sin calibrar (3.10).
`scene_title_compare` compara una imagen; tampoco demuestra igualdad de
duración. No se parchean las cargas ni se agregan esperas a estas escenas.

## Evidencia privada y límites

La evidencia de esta pasada queda fuera del checkout en
`/tmp/dw1-audio-parity/`: `compare.py`, `comparison.json`,
`timer-fixed-{1,2}/run-1-{1,2}.spu`, WAV, logs, `pre-libsnd.state` y la
prueba de regresión `regression-before.log`. El script llama al comparador
AD-15 original; el prefijo de orden global es solo diagnóstico y nunca
declara aprobada una traza.

SHA-256 de las nuevas trazas nativas:

- Tramo 1: `828523582b4f70efd7d51bde5dd932347510e3358676e30bb9e79a140f9645ca`.
- Tramo 2: `87d9ef5cf09a94acdf8ea33ff22812faee022d10bb3d7c221f3886e80a25f909`.

Se repitió el comando solicitado con `DW1_DISC_DIR=/dw/game_rom/extracted`
y `DW1_AUDIO_DIR=/tmp/dw1-audio-parity/requested`. Sale 1 antes de capturar:
`missing/stale audio reference evidence`. El stamp existente apunta a
`/ref/title_audio`, sin montar en este contenedor. Se conserva ese stamp y
se captura el ejecutable directamente con los mismos inputs y segmentos
para compararlo con las copias `.spu`. No hay WAV de referencia montado:
no se puede calcular ni afirmar SNR ≥ 40 dB.

La paridad clase B, la fase de CPU/cargas, la discrepancia del período de
vídeo y la paridad PCM permanecen abiertas. No se ha validado aquí el
retardo KON de seis muestras configurado en Redux ni las hipótesis NRPN
anotadas en `libsnd.md`.

## Verificación en el contenedor

Se compilaron `native-headless`, `native-asan` y `web`. Las suites completas
de CTest sin `DW1_DISC_DIR` dieron:

| Preset | Aprobadas | Omitidas por fixture/disco | Fallidas |
|---|---:|---:|---:|
| native-headless | 44 | 9 | 1 |
| native-asan | 42 | 9 | 1 |
| web | 25 | 3 | 0 |

El único fallo de ambas suites nativas es `scene_title_audio_compare`, por
`missing/stale audio reference evidence`; no se oculta ni se convierte en
skip. La comparación directa con las trazas copiadas también falla, como
detalla la tabla anterior. Tras ampliar la prueba de tick se recompilaron
los tres targets y se repitieron los tests afectados: `clock`/`libsnd` en
nativo y ASan, `libsnd` en web/Node, todos aprobados. No se detectaron errores
de sanitizadores.

Con `DW1_DISC_DIR=/dw/game_rom/extracted`, las ocho pruebas nativas
seleccionadas aprobaron: `pack_disc`, `gte_tables_disc`, `dw1_overlay`,
`reference_title`, `scene_title`, `scene_title_compare`,
`reference_newgame_state` y `scene_newgame_state`. Las referencias visual y
de estado se regeneraron y sus dos corridas fueron deterministas. La
comparación de estado aprobada confirma que el desfase de VBlanks admitido
por 3.3 no impide ese test; no valida la cronología de audio.

ASan aprobó las seis pruebas con disco del port: `pack_disc`,
`gte_tables_disc`, `dw1_overlay`, `scene_title`, `scene_title_compare` y
`scene_newgame_state`. Se reutilizaron las referencias ya validadas por la
misma herramienta, excluyendo su regeneración automática para evitar
escrituras concurrentes sobre los fixtures compartidos. La comparación de
estado tardó 543,62 s y terminó sin errores de sanitizadores. Logs:
`disc-native.log`, `disc-asan.log` y `disc-newgame-asan.log` en el directorio
privado indicado arriba. `git diff --check` aprobó.

Los logs de build, suites, comando solicitado y capturas directas de esa
pasada están en `/tmp/dw1-audio-parity/`. En esa pasada no se modificaron las
reglas de comparación ni el stamp de audio que apuntaba a `/ref`.

## Decisión 3 del dueño: gate de contenido (2026-10-19)

La última entrada del Spec Change Log de 3.5 sustituye el gate anterior.
`audio.compare_voice_content` compara la lista global completa de pares
`(registro, valor)` de voces (`registro < 0x180`), en orden y conservando
duplicados. Ante una diferencia informa el índice de escritura y, para cada
lado, VBlank, muestra, registro y valor, o `missing`. No busca offsets ni
acepta prefijos. El tramo 1 se compara entero. La regla B original, los
desfases de muestras y el SNR siguen ejecutándose y se guardan en el JSON
del runner aunque falle el contenido; sus diferencias no bloquean hasta
3.10. También se reportan duraciones PCM desiguales y ventanas bajo 40 dB.
El reporte conserva los límites de captura de ambos lados incluso si el
contenido diverge antes de comprobar la duración mínima.
El determinismo de dos corridas, hashes, archivos externos y errores de
proceso/sanitizadores conserva sus comprobaciones.

El tramo 2 comienza en la primera entrada a `SsSeqPlay` posterior a
`gameLoop`, usando `checkpoint_after` en ambos capturadores. El símbolo
original es `0x800CD73C` (`config/us/symbols.txt`); su asm llama a
`Snd_SetPlayMode`. Se instrumenta la misma entrada en el port, antes del
cuerpo. `seqPlay` llama a `SsSeqSetVol` en `0x800C5E30` antes de entrar en
`SsSeqPlay`: anclar en este último evita confundir preparación/carga con el
inicio de reproducción. Las llamadas anteriores a `gameLoop` no cuentan.

El primer evento posterior del juego se define como la siguiente entrada
a una de las 21 API públicas de `libsnd` usadas por el juego, enumeradas
en `stop_on` del TOML.
El tick llama rutinas internas `snd_*`/`vm_*`, sin pasar por esas entradas.
Ambos capturadores cierran antes de ejecutar la API del juego; si la misma
dirección puede abrir y cerrar, procesan primero el cierre del tramo activo.
El adaptador Redux comparte un breakpoint por dirección. Las pruebas
sintéticas verifican esta coincidencia de dirección, el rechazo de anclas
tempranas y la exclusión de las escrituras del evento que cierra el tramo.

El footer `# end <muestras> event:<símbolo>` registra el límite real,
`floor((ciclo_fin − ciclo_inicio)/768)`, sin inferirlo de la última escritura.
El gate exige cierre por evento configurado y al menos 441000 muestras
(diez segundos) en ambos lados. No admite un cierre por timeout ni una traza
antigua sin límite musical verificable. Este cambio invalida el TOML y
requiere recapturar la referencia, sin adaptar su prefijo a posteriori.

### Evidencia de la nueva captura

La captura nativa en `/tmp/dw1-decision3-port` confirma las **744/744**
escrituras de voz del tramo 1 frente a la copia de referencia de
`build/audio-debug/ref/run-1-1.spu`, sin desplazamiento de contenido.
El desfase máximo de muestras de esas escrituras es 138180 y la media
40169,2285; son diagnósticos. `copied-comparison.json` conserva el resultado.

El tramo 2 nativo tiene 654 escrituras de voz antes de la siguiente API del
juego, `SsUtKeyOnV`, a las 409127 muestras (**9,277256 s**) del ancla.
Ese intervalo no satisface el mínimo aprobado de diez segundos. Se conserva
el límite y se informa el fallo; no se prolonga la captura más allá del evento
ni se cambia la escena para ocultarlo. Las copias antiguas del tramo 2,
originadas en `gameLoop`, no demuestran el nuevo límite y necesitan recaptura.

### Verificación de la decisión 3 en el contenedor

Desde el checkpoint `0860f80`, se ejecutaron los builds y las suites completas
de los tres presets, sin `DW1_DISC_DIR` en esas suites:

| Preset | Aprobadas | Omitidas por fixture/disco | Fallidas |
|---|---:|---:|---:|
| native-headless | 44 | 10 | 0 |
| native-asan | 42 | 10 | 0 |
| web | 25 | 3 | 0 |

Logs externos: `/tmp/dw1-decision3-build-{native,asan,web}.log` y
`/tmp/dw1-decision3-ctest-{native,asan,web}.log`. Las omisiones incluyen
`scene_title_audio_compare` sin sello durante la recaptura; no acreditan
paridad clase B. `scene_audio_unit`, `scene_runner`, `scene_audio_runtime` y
`reference_capture_unit` aprobaron. También se repitieron las pruebas
focalizadas tras ampliar el reporte de límites y el caso de duración PCM
desigual: se conserva el SNR calculado como diagnóstico, mientras un WAV
truncado sigue bloqueando. No se detectaron errores de sanitizadores.

La evidencia nativa de esta sesión se regeneró en
`/tmp/dw1-decision3-port`, reproduciendo exactamente el conteo y el límite
musical anteriores. El contenido del tramo 1 y sus métricas frente a la
referencia copiada se guardan en `copied-comparison.json`.

### Recaptura completa y gate real

Se completó la captura nueva con el TOML vigente:

```sh
DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/reference/capture.py title_audio --audio --out /tmp/dw1-decision3-reference --max-vblanks 40000 --timeout 1800
DW1_DISC_DIR=/dw/game_rom/extracted DW1_AUDIO_DIR=/tmp/dw1-decision3-runner python3 -I -B tools/port/scenes/run.py title_audio --build native-headless --compare-audio
```

Las dos corridas de Redux produjeron trazas y PCM idénticos byte a byte.
`--reuse` con ese mismo directorio verificó los hashes y reutilizó el sello.
Ambas referencias alcanzaron `gameLoop` en el VBlank 17560 y `SsSeqPlay` en
17685; cerraron antes de `SsUtKeyOnV`, 572 VBlanks después del ancla.
No hay dependencia de `/ref` en esta comprobación.

El runner realizó dos corridas nativas deterministas y devolvió **1**:

| Tramo | Resultado de contenido | Límite port / referencia |
|---|---|---|
| 1 | 744/744 escrituras idénticas | 1099910 / 1226736 muestras, `checkpoint` |
| 2 | Divergencia en la primera escritura; 654 / 660 escrituras totales | 409127 / 419933 muestras, `event:SsUtKeyOnV` |

Primera divergencia del tramo 2, índice 0: port **VBlank 1, muestra 59,
registro `0x000` = `0x078e`**; referencia **VBlank 0, muestra 108,
registro `0x000` = `0x0143`**. Es una diferencia de valor de voz, no solo de
fase, y bloquea según la decisión 3. Su causa dentro del estado musical o de
`libsnd` queda sin determinar en esta implementación del gate; no se ajusta
el inicio ni se descartan escrituras para obtener un prefijo común.

Los límites musicales también incumplen el mínimo aprobado:
**9,277256 s** en el port y **9,522290 s** en la referencia. La divergencia de
contenido se informa primero; `capture_bounds` conserva ambos límites aunque
esa diferencia impida llegar a la validación de duración. Incluso corrigiendo
los valores, esta ventana no demostraría los diez segundos exigidos.

Los diagnósticos se calcularon a pesar del fallo:

| Métrica no bloqueante | Tramo 1 | Tramo 2 |
|---|---:|---:|
| Desfase de muestras máximo / media, por ocurrencia de registro | 138180 / 27771,6409 | 69 / 52,5055 |
| Lag PCM por correlación, muestras | -38954 | -58 |
| Peor SNR, dB | -3,739384 | 8,491279 |

La regla B difiere en ambos tramos y las duraciones PCM son desiguales;
esas métricas no bloquean. Se preserva el reporte completo en
`build/port/native-headless/scenes/title_audio/audio.json`, con solo hashes
y métricas. Audio y trazas están en los directorios externos anteriores.
`/tmp/dw1-decision3-port/fresh-comparison.json` contiene además la comparación
directa de las trazas, y `/tmp/dw1-decision3-runner.log` la salida del runner.

Se repitió `scene_title_audio_compare` con `native-asan`, la referencia fresca
y los fixtures de preparación excluidos para reutilizar esa captura. También
falla por la misma primera escritura del tramo 2; sus dos corridas son
deterministas y los cuatro hashes de trazas/PCM coinciden con los de nativo.
No hay diagnósticos de ASan/UBSan. Log:
`/tmp/dw1-decision3-audio-asan.log`; evidencia externa:
`/tmp/dw1-decision3-asan/native-asan/title_audio`.
Este fallo real clase B permanece abierto, independientemente de las suites
sin disco aprobadas antes de disponer de la referencia fresca.

La implementación del gate está verificada, pero **3.5 sigue sin satisfacer
la aceptación**: quedan la diferencia de contenido musical y una ventana
válida de diez segundos antes del primer evento. No se modificaron `libsnd`,
el reloj ni los inputs para eludir estos fallos, ni se hicieron commits.


## Investigación de los bloqueos sobre b97a378 (2026-10-19)

No se modifica la decisión 3, el comparador, la definición de evento ni los
registros incluidos. La instrumentación temporal de libsnd se retira del
árbol final; los probes de Redux y sus lecturas de RAM quedan fuera del
checkout, en `/tmp/dw1-volume-probe`. No hay cambios en `src/` ni commits.

### El primer volumen pertenece a una voz conservada

El primer grupo tras `SsSeqPlay` contiene los volúmenes de seis voces, sin
nuevos KON ni direcciones de muestras. `Snd_SetPlayMode` llama a
`_SsVmSetSeqVol` (`800CD868` → `800D1D18`) antes del primer evento de la SEQ.
El asm filtra por máscara reservada, identificador de SEQ y VAB, pero **no
por estado activo**. `_SsVmKeyOffNow`, Stop y Close conservan programa, tono,
velocidad y pan. Open reinicia los volúmenes de canal a 127; `seqPlay`
(`800C5E30`) fija 80/80 mediante `SsSeqSetVol` antes de `SsSeqPlay`.

Por tanto, la comparación inicial observa todavía la fase de la canción
anterior. En nativo, la voz 0 conserva programa 3, tono 0, velocidad 80,
índice de volumen de canal 2 y pan 64. Con banco=127, programa=110,
tono=127/pan=64 y SEQ=80/80,
la fórmula del asm produce `078e/078e`. La referencia escribe `0143/0020`,
con pan asimétrico. No es una escala global de volumen: las voces 3 y 5
conservan valores que sí coinciden en ambos lados.

El segundo probe de Redux confirma directamente la estructura de voz de
`0x36` bytes en `8014E6F8`, en `_SsVmSetSeqVol` (`800D1E6C`) durante el
`SsSeqPlay` de VBlank 17685: velocidad=40, nota=70, SEQ=0, VAB=2,
programa/compact=5, tono=0, índice CC7=5, pan=64 y estado=0. AutoVol y sus
contadores/objetivos están a cero. Los punteros de banco/programas/tonos
en `8014EE04/44/84` dan banco=127, programa=127/pan=64 y tono=90/pan=20.
La estructura SEQ en `801484C4` contiene volumen=80/80 y CC7=127. No se
deducen esos atributos del resultado: se leen en el original. El master
SPU no interviene en VOLL/VOLR y no hay una rampa AutoVol pendiente.
Con esos valores, los pasos enteros del recálculo son base=5160,
ganancia de programa/tono=3656 y niveles L/R=2302/730 antes de elevar al
cuadrado y dividir por 16383: 323/32 (`0143/0020`). Con los datos nativos
son 10320, 8938 y 5630/5630: 1934/1934 (`078e/078e`). Se conservan las
divisiones y truncamientos del asm; no hace falta cambiar signo ni shifts
para explicar la diferencia.

El primer arranque anterior de esa SEQ se observa en Redux en VBlank
17369; el segundo llega 316 VBlanks después. En nativo los ciclos de esos
dos arranques son 1194457197 y 1343632806: 4,404514 s entre ellos, frente
a unos 5,27 s en el original. Ambas ejecuciones llegan al nuevo arranque
con una fase distinta de la canción anterior. Es consistente con los
costos de escena/CD sin calibrar documentados arriba; no demuestra por sí
solo qué operación concreta causa toda la diferencia temporal. No se
introduce una corrección de ganancia que contradiga estas lecturas.

`sequence_restart_volume` reproduce sin disco los dos pares y la transición
CC7=110 → Stop/Close/Open → CC7=127. La variante con velocidad=40,
programa=127, tono=90/pan=20 produce `00e9/0017` antes de parar y
`0143/0020` al reiniciar. Pasa con el código del checkpoint; no se presenta
como una prueba que fallase antes de una corrección de producción. También
impide ocultar el problema borrando metadatos de voces o descartando voces
inactivas, comportamientos que no corresponden al original.

En las trazas del probe, las primeras 42 escrituras de voz son recálculos o
pitch bend sobre ese estado anterior. A partir del primer note-on nuevo
(bundle `004,000,002,006,008,00a`) coinciden las 612 escrituras restantes del
port con el prefijo correspondiente de referencia. La referencia tiene seis
más antes del evento del juego, por su ventana más larga. Este análisis es
**diagnóstico**: no salta escrituras ni acepta prefijos en el gate.

### El final de la ventana es un evento del guion

La pila nativa del primer `SsUtKeyOnV` después del ancla es:
`gameLoop → tickScript → tickTextboxHandling → createAnimatedUIBox →
playSound → startSound → SsUtKeyOnV`. Abre un cuadro de diálogo; no procede
del secuenciador. Redux lo alcanza en VBlank 18257, 572 después del ancla
17685, y nativo tras 409127 muestras (9,277256 s). Mantener izquierda desde
el primer VBlank después del ancla deja exactamente el mismo límite nativo.
La entrada no alarga este tramo del guion.

Se explora además avanzar diálogos con Cross cada 60 VBlanks, desde +600
hasta +6000 relativos al primer `SsSeqPlay` después de `gameLoop`. Se
encuentran otros dos arranques nativos, en ciclos 3074921517 y 4492511592.
Sus siguientes `SsUtKeyOnV` llegan 4,656191 s y 3,091281 s después,
respectivamente: apertura de otro cuadro y sonido de animación del partner.
Estos recorridos son probes externos; no sustituyen el TOML aprobado ni
constituyen una ventana válida por sí mismos.

Se repite el cuarto arranque global con la lista completa de `stop_on`
aprobada, sin más Cross tras los diálogos: idle termina a 136325 muestras
(3,091270 s) y mantener izquierda desde el ancla+1 termina a 118663
(2,690771 s), ambos por `SsUtKeyOnV`. El primer arranque global tampoco
puede ofrecer diez segundos: el siguiente `SsSeqPlay` llega en 4,404514 s
nativos, y Stop/Close ya son eventos anteriores a ese límite superior.
No se encontró una ventana válida en estos recorridos. Esto no es una
enumeración exhaustiva de todas las entradas posibles del juego.

### Decisión pendiente del dueño

La historia no queda aceptada. No se ha demostrado un error de fórmula de
libsnd/libspu que justifique modificar producción: los datos distintos de
las voces retenidas explican exactamente la primera divergencia. Corregir
el desfase completo de escena/CPU/CD requiere localizar y calibrar sus
costos, trabajo excluido de 3.5 y reservado a 3.10 por el spec.

Para el bloqueo de duración, la enmienda mínima cuantificable sería cambiar
el mínimo de diez a nueve segundos para esta ventana (el menor límite
medido es 9,277256 s). Requiere aprobación del dueño y **no** resuelve el
bloqueo de contenido: quedan los recálculos de voces anteriores y seis
escrituras finales adicionales en referencia. Otra opción es aprobar una
escena/fixture diferente y verificar primero una ventana válida, o ampliar
el alcance para investigar la calibración de 3.10. Ninguna de estas
alternativas se aplica aquí; tampoco se excluyen escrituras iniciales ni
se cruza/omite el primer evento del juego.

### Verificación de esta investigación

Se compilan `native-headless`, `native-asan` y `web` con CMake y se ejecutan
sus suites sin `DW1_DISC_DIR`/`DW1_AUDIO_DIR`: nativo 44 aprobadas y 10
omitidas; ASan 42 y 10; web 24 aprobadas, 3 omitidas y un timeout inicial
en la aserción de arranque de `web_boot`. La repetición aislada de
`web_boot` aprueba sus ocho casos en Chromium/Firefox, sin modificar el
test ni su timeout. `libsnd` se repite tras el último ajuste del fixture y
aprueba en los tres targets. No hay diagnósticos ASan/UBSan.
Logs externos: `/tmp/dw1-volume-ctest-{native,asan,web}.log`,
`/tmp/dw1-volume-web-boot-retry.log` y
`/tmp/dw1-volume-libsnd-{native,asan,web}.log`.

La recaptura final usa el TOML aprobado, sin instrumentación ni cambios de
ventana:

```sh
DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/reference/capture.py title_audio --audio --out /tmp/dw1-volume-reference --max-vblanks 40000 --timeout 3600
```

Las dos corridas producen trazas y WAV idénticos byte a byte en ambos
tramos. SHA-256 de traza/PCM por tramo:

| Tramo | Traza | WAV |
|---|---|---|
| 1 | `9df80fc28e8810c2dcdea63cd17a11dbf639e9190bba3b297d246afd6ae71c92` | `d3a04edc0d060ff298202b48444e5b7be20c95748a3b64e559d781376a96d842` |
| 2 | `6a3a5f9fe35ca105c4f0becc6e1f637070c8f257af7c570e9af1c549968cbe2c` | `e47a70bc0103443f1d0a00873db97990aa24ad1882b11f823143d7a32eeb3f5e` |

Redux alcanza `gameLoop` en VBlank 17560 y el ancla en 17685; el primer
evento es `SsUtKeyOnV` en 18257. Su trailer sigue siendo
`# end 419933 event:SsUtKeyOnV` (9,522290 s). El trailer nativo es
`# end 409127 event:SsUtKeyOnV` (9,277256 s), VBlank relativo 556.

El runner nativo se ejecuta con:

```sh
DW1_DISC_DIR=/dw/game_rom/extracted DW1_AUDIO_DIR=/tmp/dw1-volume-port python3 -I -B tools/port/scenes/run.py title_audio --build native-headless --compare-audio
```

La referencia fresca reproduce el bloqueo: tramo 1 aprobado, 744 escrituras;
tramo 2 falla en escritura 0, port VBlank 1/muestra 59, `000=078e`, frente
a referencia VBlank 0/muestra 108, `000=0143`. Las trazas nuevas tienen
654 y 660 escrituras de voz respectivamente; después de las primeras 42,
las 612 nativas coinciden exactamente con el prefijo de referencia, que
tiene otras seis al final. Se vuelve a comprobar solo como diagnóstico,
sin modificar la comparación bloqueante. El reporte con regla B, desfases
y SNR sigue en `build/port/native-headless/scenes/title_audio/audio.json`;
el log completo está en `/tmp/dw1-volume-compare-native.log`.

Se repite el mismo comando con `--build native-asan`; también sale con
código 1 por esa primera divergencia, sin diagnósticos ASan/UBSan.
Las dos corridas de cada build son deterministas y sus cuatro hashes por
tramo (traza/WAV, nativo/ASan) coinciden entre builds. Log:
`/tmp/dw1-volume-compare-asan.log`; reporte:
`build/port/native-asan/scenes/title_audio/audio.json`. Los medios y probes
siguen fuera del checkout. Se retira la instrumentación temporal y
`port_trace.log`, generado durante los probes.


## Decisión 4 del dueño: ancla de nota nueva y ventana común

2026-10-19, sobre checkpoint `440f7c6`. Esta sección sustituye el gate de
la decisión 3 descrito en las investigaciones anteriores; no cambia sus
hallazgos sobre el cálculo de volumen ni modifica `libsnd`, `libspu`, los
inputs o el primer evento del juego que cierra el tramo.

El TOML arma el tramo 2 en el primer `SsSeqPlay` posterior a `gameLoop` y
selecciona `anchor = "note_on"`. El origen efectivo es la primera escritura
del primer bundle nuevo con dirección de muestra y KON de la misma voz.
El port y Lua acumulan un flush hasta KON alto; guardan el primer acceso y
las direcciones escritas por voz. El primer KON no nulo que cruza esa máscara
confirma el ancla y conserva el bundle desde su primera escritura. Si no
confirma, KON alto limpia el candidato. Esto excluye el recálculo heredado
sin deducir actividad de la envolvente, buscar valores coincidentes ni
retirar escrituras después del ancla. Los dos algoritmos y el símbolo
`SsSeqPlay = 0x800CD73C` están documentados en [reference.md](reference.md).

El origen de traza y PCM se mueve al mismo ciclo/VBlank. El port recorta los
frames provisionales del WAV mediante índices absolutos de muestra, sin
cambiar la fase del mixer ni su callback de salida. Redux selecciona ese
mismo intervalo del tap indexado. Un cierre sin nota nueva confirmada falla.
`stop_on` sigue intacto: la primera entrada pública del juego cierra antes
de ejecutar su cuerpo; el secuenciador usa rutinas internas.

El tramo 1 compara todas sus escrituras de voz. El tramo 2 exige al menos
396900 muestras (9 s) en **cada** captura y compara todas las escrituras de
voz del intervalo común `[0, min(fin_port, fin_referencia))`, incluidas
las duplicadas, en su orden global. El reporte incluye el límite común y
las escrituras posteriores excluidas. Valor, registro, orden o cantidad
diferentes dentro del intervalo bloquean con el primer índice divergente.
Regla B, desfases y SNR siguen calculándose sobre los tramos capturados y
reportándose sin bloquear hasta 3.10. La sincronización de escena/CD y las
voces heredadas quedan pendientes de esa historia, según la enmienda AD-15.

Las pruebas sintéticas comprueban el umbral exacto de nueve segundos, rechazo
por debajo del umbral en cualquiera de los lados (incluido 9 s menos una
muestra), el extremo común
exclusivo, orden/valor/registro/cantidad y diagnósticos no bloqueantes.
Las pruebas de integración sin disco ejecutan los detectores C y Lua:
recálculo heredado, KON cero, dirección y KON de voces distintas, voces de
ambas mitades de KON y bundle que cruza una muestra. Verifican el origen
exacto, el bundle completo, el recorte del WAV y el rechazo de un cierre
sin nota nueva. Las dos corridas de captura de audio pasan a ejecutarse en
serie para respetar el límite de CPU del contenedor; se mantiene la exigencia
de trazas y WAV idénticos byte a byte.


### Recaptura y comparación de la decisión 4

Se recaptura fuera del checkout, con dos pasadas secuenciales:

```sh
DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/reference/capture.py title_audio --audio --out /tmp/dw1-decision4-reference --max-vblanks 40000 --timeout 3600
DW1_DISC_DIR=/dw/game_rom/extracted DW1_AUDIO_DIR=/tmp/dw1-decision4-port python3 -I -B tools/port/scenes/run.py title_audio --build native-headless --compare-audio
DW1_DISC_DIR=/dw/game_rom/extracted DW1_AUDIO_DIR=/tmp/dw1-decision4-port python3 -I -B tools/port/scenes/run.py title_audio --build native-asan --compare-audio
```

Logs externos: `/tmp/dw1-decision4-reference.log`,
`/tmp/dw1-decision4-compare-native.log` y
`/tmp/dw1-decision4-compare-asan.log`. Los reportes de métricas quedan en
`build/port/{native-headless,native-asan}/scenes/title_audio/audio.json`.


La referencia nueva produce trazas y WAV idénticos entre sus dos corridas.
SHA-256 por tramo:

| Tramo | Traza | WAV |
|---|---|---|
| 1 | `9df80fc28e8810c2dcdea63cd17a11dbf639e9190bba3b297d246afd6ae71c92` | `d3a04edc0d060ff298202b48444e5b7be20c95748a3b64e559d781376a96d842` |
| 2 | `021ec2f42f9def9b4cee885b9b1cf24ef616aed4aeab05b1a16c9a0e6880999c` | `f558ca326ede0aa018310844b09c188b0e9232f26daf6b14e1a9050c98f48c8f` |

El gate nativo aprueba ambos tramos:

| Tramo | Escrituras idénticas | Intervalo comparado | Cierre port / referencia |
|---|---:|---|---|
| 1 | 744 | tramo completo | 1099910 / 1226736 muestras, `checkpoint` |
| 2 | 612 | `[0, 398962)` muestras | 398962 / 409718 muestras, `event:SsUtKeyOnV` |

La música dura 9,046757 s en el port y 9,290658 s en referencia desde la
primera nota nueva: ambas superan nueve segundos. La referencia tiene seis
escrituras de voz después del extremo común; el port, cero. No hay divergencia
de contenido dentro del gate. El desfase máximo de las 612 escrituras es
15 muestras (media 2,155229). El primer bundle de referencia empieza en
ciclo 9989638189, con `004=0d74`, seguido de `000=08bb`, `002=08bb`,
`006=3700`, `008=80ff`, `00a=000f`.

Los diagnósticos siguen mostrando diferencias: regla B del tramo 1 en
VBlank 0, `0x188`, escritura 1 (port cero/referencia ausente); tramo 2 en
VBlank 0, `0x198` (port `0x002d`/referencia `0x003d`). Peor SNR por ventana:
−3,739384 dB y 8,493601 dB, respectivamente; desfase PCM −38954 y −8
muestras. Esto no demuestra paridad PCM ni sincronización de escena; son
pendientes de 3.10, no relajaciones adicionales del gate aprobado.


La comparación ASan también aprueba los dos tramos con las mismas métricas,
sin diagnósticos ASan/UBSan. Las dos ejecuciones de cada build son
deterministas; todos los hashes de traza/WAV del port coinciden además
entre `native-headless` y `native-asan`. No se modifica código de juego,
la fórmula de volumen de `libsnd` ni el backend PCM del emulador.


### Verificación final de la decisión 4

Builds `native-headless`, `native-asan` y `web` aprobados, con `-j2` como
máximo y sin solapar builds, presets de ctest ni capturas de referencia.
Las suites se ejecutan con `DW1_DISC_DIR` y `DW1_AUDIO_DIR` retiradas del
entorno, una por vez:

| Preset | Aprobadas | Omitidas | Fallos |
|---|---:|---:|---:|
| native-headless | 45 | 9 | 0 |
| native-asan | 43 | 9 | 0 |
| web | 25 | 3 | 0 |

Las omisiones corresponden a pruebas que requieren disco explícito.
`scene_title_audio_compare` sí se ejecuta y aprueba en native y ASan usando
el pack existente y la referencia nueva. `web_boot` aprueba en esta pasada
sin repetir ni modificar el timeout. Las pruebas nuevas del detector,
configuración, mínimo de nueve segundos y ventana común aprueban en los
presets aplicables. Logs: `/tmp/dw1-decision4-ctest-{native,asan,web}.log`;
builds: `/tmp/dw1-decision4-build-{native,asan,web}.log`.
`git diff --check` no informa errores. No se crean commits.
