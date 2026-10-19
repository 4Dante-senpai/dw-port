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
La determinación de dos corridas, hashes, archivos externos y errores de
proceso/sanitizadores conserva sus comprobaciones.

El tramo 2 comienza en la primera entrada a `SsSeqPlay` posterior a
`gameLoop`, usando `checkpoint_after` en ambos capturadores. El símbolo
original es `0x800CD73C` (`config/us/symbols.txt`); su asm llama a
`Snd_SetPlayMode`. Se instrumenta la misma entrada en el port, antes del
cuerpo. `seqPlay` llama a `SsSeqSetVol` en `0x800C5E30` antes de entrar en
`SsSeqPlay`: anclar en este último evita confundir preparación/carga con el
inicio de reproducción. Las llamadas anteriores a `gameLoop` no cuentan.

El primer evento posterior del juego se define como la siguiente entrada
a una de las 21 API públicas de `libsnd`, enumeradas en `stop_on` del TOML.
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
