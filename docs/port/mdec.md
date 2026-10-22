# MDEC y libpress (historia 3.7)

`port/backend/mdec.c` adapta el decodificador de `Wyrelade/DW2-PC`
(`backend/mdec.c`, CC0 1.0). El orden zigzag, la cuantización y los comandos siguen la documentación de
[psx-spx](https://psx-spx.consoledev.net/macroblockdecodermdec/).
La IDCT usa los 13 bits superiores de la escala cargada y acumula en 64 bits.
Por decisión 4 del dueño se conserva la fracción entre las dos pasadas transpuestas:
la primera guarda la suma completa; la segunda calcula
`floor((sum + 33554431) / 67108864)`, con mitades hacia abajo.
La descuantización AC calcula `floor((signed10 * qt[k] * q_scale + 4) / 8)`
y satura a 11 bits; DC no aplica `q_scale/8`, y `q_scale=0` usa el doble
sin zigzag. Los desplazamientos negativos usan piso explícito y portable.

YUV→RGB conserva como hipótesis los factores enteros 1436, −352, −731 y
1815, divididos por 1024, con redondeo al más cercano y mitades hacia arriba.
Se añade luminancia, se satura a −128..127 y se aplica +128.
psx-spx describe la IDCT como aproximada y declara desconocidos la precisión
fija y el redondeo del color. No se afirma exactitud del hardware ni se añade
el posible límite de 9 bits sin verificar. No se incorpora código GPL de
PCSX-Redux ni instrucciones MIPS ejecutadas por el port. La tabla zigzag
describe un algoritmo; no procede del ejecutable.

Comandos implementados: 1 (run-level a RGB15/RGB24, signo y STP), 2
(cuantización luminancia/crominancia) y 3 (escala IDCT). La FIFO permite
lecturas que dividen un macroblock. Los datos incompletos del frame terminal
sintético del anillo producen negro. No se implementan las profundidades
monocromas de 4/8 bits, que no alcanza el consumidor de DW1.

`port/psyq/libpress.c` traduce las entradas alcanzadas del asm USA
`asm/us/main/psyq/libpress.s`: reset, entrada/salida DMA, callback, expansión
LZ con predictor XOR de `DecDCTvlcBuild`, VLC v2 y DC diferencial v3 de
`DecDCTvlc2`. Como el original, versiones menores que 3 siguen v2 y las
restantes siguen v3; no se inventa validación del magic. El relleno final
incluye 65 halfwords FE00 por el delay slot del bucle original.

Las tres regiones `mdec_iq`, `mdec_coef` y `data_2` se leen de
`\\SLUS_010.32;1` en el Pack. `tools/port/psyq/press_tables.py` genera solamente
sus offsets y tamaños a partir de `config/us/{main.yaml,symbols.txt}`.
No hay tablas de cuantización, escala ni VLC originales en el código fuente.
Ambos hosts rechazan tablas ausentes o un ejecutable corto antes del arranque.
`press_tables_disc` extrae las regiones directamente de sectores del disco y
compara sus SHA-256 con los hashes registrados por el host, sin reutilizar
el lector del Pack para obtener el valor esperado.

`DecDCTout` termina mediante un evento del reloj virtual. El callback puede
encolar el siguiente slice, que se despacha después de volver del callback
actual. Reset cancela una salida pendiente. El coste provisional de 1024
ciclos por macroblock está aislado en `port/psyq/clock_costs.h`: permanece
como **hipótesis sin calibrar**. Pause bloqueante usa los promedios de segunda
respuesta de psx-spx mediante AD-8: 2168860 ciclos a 1×, 1097107 a 2× y
7666 ya pausado. Los valores quedan como hipótesis que recalibra 3.10.
El comando de lectura sigue activo al retirar los callbacks STR. Pause llega
a VBlank 122, pero la cola GPU lleva el retorno a 123 frente a 122 del original;
no se ajusta MDEC ni GPU para compensarlo. La traza causal y los resultados
vigentes están al final de [reference.md](reference.md).
`MOV_waitForFlip` cede 1024 ciclos por iteración
sólo bajo `DW1_PORT`; el build PS1 conserva su bucle original.

## Verificación y evidencia

`mdec_vectors.py` calcula independientemente el producto completo `Sᵀ A S`,
con un único redondeo final y tablas sintéticas asimétricas;
el fichero versionado contiene sólo
12 hashes de salida. `mdec` verifica q_scale 0, 1 y 63, coeficientes negativos,
saturación, EOB, ambos formatos, STP y lecturas parciales. `libpress` verifica
VLC v2/v3, escape, lookup extendido, expansión LZ/XOR, errores de tablas,
reset y 1000 callbacks consecutivos sin recursión. Se ejecutan en nativo,
ASan/UBSan y Node/WASM; la prueba de ejecutable corto usa un Pack sintético
nativo. `av_stubs` conserva el consumidor original sobre EOF sintético.

Las escenas `movie0` y `movie0_skip` capturan cada `PutDispEnv` dentro de
`MOV_playMovie`; Start se mantiene desde el VBlank relativo 120 hasta el 180.
Redux se ejecuta dos veces con Speed=1 y exige frames y marcas temporales
idénticos. El port también se ejecuta dos veces. Por instrucción posterior
del dueño, esta continuación guarda frames, PCM y diffs bajo el árbol ignorado
`build/evidence/{reference,port}/`, para sobrevivir al reinicio del contenedor.
El comparador admite ese árbol y sigue rechazando evidencia en los fuentes.
Se aplican las tolerancias y excepciones aprobadas sin modificar el bloque congelado.
Los gates exigen máximo por componente ≤4 y media de **cada canal** ≤0,5,
salvo los cuatro pares exactos de componentes azules que autoriza la decisión 5.
Se declaran en `tools/port/scenes/movie0.toml`, en `mdec_allowances`, con
frame, coordenadas desde cero, canal y valores port/referencia. El comparador
acepta esos pares sin retirarlos del cálculo de las medias ni de las métricas
brutas. Un componente distinto >4, un par listado que empeora o cualquier
media de canal >0,5 sigue fallando. Una mejora a ≤4 no necesita excepción.
`test_runner.py` verifica los tres caminos de fallo para cada par, rechaza
también pares desplazados con el mismo error y comprueba que una mejora pasa.
Los requisitos restantes siguen siendo:
3636 frames para OP1 completo (decisión del dueño tras verificar el original), retorno 1/0, mismo VBlank de salida con Start,
y diferencia del desfase entre último frame y última entrega XA ≤ intervalo
medio entre frames consecutivos de la referencia. Una divergencia se conserva
como fallo; no se compensa ni desplaza la secuencia.

Los tests de referencia usan fixture, lock compartido, skip 77 sin recursos
y timeout de dos horas. `gen_stubs.py` rechaza generar stubs de libpress.
La presencia de estas pruebas no constituye por sí sola evidencia de paridad;
los resultados de ejecución se registran en [reference.md](reference.md).

Para reproducir los gates de video con el disco propio disponible:

```sh
export DW1_REFERENCE_DIR="$PWD/build/evidence/reference"
export DW1_AUDIO_DIR="$PWD/build/evidence/port"
DW1_DISC_DIR=/path/to/disc ctest --preset native-headless -R 'scene_movie0' --output-on-failure
DW1_DISC_DIR=/path/to/disc ctest --preset native-asan -R 'scene_movie0' --output-on-failure
ctest --preset web -R '^(mdec|libpress|mdec_vectors|press_tables)$' --output-on-failure
```

Los dos primeros comandos incluyen sus fixtures de Pack/referencia; los
fallos abiertos de [reference.md](reference.md) deben seguir apareciendo.

### Histórico: ensayo anterior de redondeo por pasada (revertido)

Los resultados de esta sección corresponden al commit d57a330. La decisión 4
posterior exige restaurar el modelo anterior y evaluar las variantes de color
sobre OP1 completo. Los resultados vigentes se registran al final; los fallos
de cuenta 3637 y la decisión de retener el modelo por pasada quedaron superados.

#### Revisión del redondeo (2026-10-21)

Se sustituyó la fracción conservada entre pasadas por el redondeo de cada
pasada de `real_idct_core`: escala desplazada tres bits y
`floor((sum + 4095) / 8192)`. La descuantización explicita también el piso
para negativos; YUV usa los coeficientes decimales publicados. Los vectores
sintéticos independientes se regeneraron con el mismo modelo documentado.
Esto sigue la descripción disponible de psx-spx, que **no especifica de
forma exacta el redondeo de color del hardware**.

Comprobación rápida contra los mismos frames de Redux, canales R/G/B,
en unidades de 1/255. Las medias se calculan sobre los 76 800 píxeles
completos de cada canal, sin recortes:

| Frame | Máximo antes → después | Media antes (R/G/B) | Media después (R/G/B) |
|---|---|---|---|
| 2318 | 4/2/5 → 3/2/5 | 0,316198 / 0,235156 / 0,378021 | 0,364010 / 0,266289 / 0,429453 |
| 2587 | 3/2/5 → 3/2/3 | 0,277773 / 0,229531 / 0,364323 | 0,331523 / 0,262956 / 0,415273 |
| 3439 | 4/2/5 → 4/3/5 | 0,035690 / 0,040195 / 0,039492 | 0,043464 / 0,049479 / 0,050260 |
| 3453 | 3/2/5 → 3/3/4 | 0,037070 / 0,043451 / 0,041979 | 0,045469 / 0,053034 / 0,051875 |

Los dos excesos restantes de esta selección son azules: frame 2318,
coordenada (144,80), port 181 frente a referencia 176; frame 3439,
(176,96), port 24 frente a 19. Coordenadas desde cero. Aunque dos máximos
mejoran, las medias aumentan; no se presenta el cambio como una mejora
general de fidelidad. Evidencia exacta, hashes, harness y píxeles residuales:
`build/verification-3-7-gates/{quick-before,quick-after,quick-residual-pixels}.json`.
Los cuatro streams VLC expandidos permanecen idénticos antes y después.
El resultado completo y la condición de parada se registran en
[reference.md](reference.md).

En la siguiente continuación se hizo un único ensayo acotado con seis
variantes: modelo conservado, piso de color, mitades hacia arriba en color,
mitades hacia arriba en IDCT, entrada de 9 bits y truncado de Y+color hacia
cero. El piso de color alcanza máximo ≤4 en los cuatro frames, pero la media
azul de 2318 sube a **0,5068229166666667**, fuera de 0,5. El truncado completo
produce dos excesos de 5 en 3439. Las otras variantes conservan los dos
excesos originales. Se conserva el modelo previo; ninguna variante supera
ambos gates. No se sigue buscando ni se ajustan coeficientes a los píxeles.
Resultados completos en `build/verification-3-7-gates/bounded/results.json`.

La observación opcional `DW1_MOVIE_TIMING_TRACE=1` registra `PadRead`,
entrada/salida MDEC, callbacks, decode, espera y pausa CD en stderr con
ciclos, VBlank relativo y número de presentación. No avanza el reloj ni
modifica las capturas de referencia. Las sondas Redux se conservan sólo en
`build/verification-3-7-gates/`, usando direcciones del asm original, sin
consultar fuentes de su decodificador.

La regresión completa posterior confirma que la selección rápida no basta:
con el modelo por pasada hay 26 frames fuera del máximo y 30 componentes
(29 B y 1 R), frente a cuatro componentes de cuatro frames en el modelo
anterior. El máximo global pasa de 5 a 6. Medias RGB sobre todos los frames:
0,218758 / 0,169088 / 0,251815 antes; 0,254643 / 0,200700 / 0,290384 después.
Máxima media de un frame por canal: 0,336966 / 0,284609 / 0,408008 antes;
0,386133 / 0,290169 / 0,469544 después. Ninguna media excede 0,5.
La lista exacta de píxeles y la matriz de verificación están en
[reference.md](reference.md); los datos sin redondear están en
`build/verification-3-7-gates/full-before-after.json`. No se revierte ni
se vuelve a ajustar tras el ensayo acotado: queda pendiente la decisión
del dueño sobre el modelo y la fidelidad residual.

Verificación final secuencial: L1 nativo 51/51, ASan 49/49 y web 34/34;
`scene_movie0_compare` y `scene_movie0_skip_compare` fallan en nativo y
ASan por los gates documentados. Título, estado inicial, audio de título
y XA pasan. `make compare` pasa. Los máximos completos RGB son 5/4/6,
y los informes nativo/ASan de OP1 y Start son idénticos. OP1 conserva A/V
dentro de un frame y XA de 54,424193 dB. La matriz completa, residuos y
límites del PCM mezclado están en [reference.md](reference.md); no queda
ninguna repetición solicitada pendiente, pero la historia sigue abierta.

## Histórico: decisiones 3 y 4 (2026-10-21)

IDCT restaurada: fracción conservada entre pasadas, acumulación de 64 bits y un redondeo final; divisiones negativas explícitas. El oráculo Python calcula el producto completo SᵀAS y regenera 12 hashes sintéticos. Los factores RGB conservados son 1436/−352/−731/1815 divididos por 1024, redondeo al más cercano con mitades hacia arriba; Y+color se satura a −128..127 antes del +128. psx-spx deja precisión y redondeo de color sin especificar.

CdControlB(CdlPause) avanza el reloj AD-8 y despacha eventos; el estado del comando de lectura sobrevive a StUnSetRing. Pause cancela sectores pendientes. Costes como hipótesis de 3.10, sin ajustar MDEC/GPU. La escena exige 3636 cuadros según la decisión del dueño. Se conserva la referencia: sólo se migra su sello por expected_frames, comprobando antes la identidad; capturas y hashes no cambian.

## Pruebas secuenciales

| Trabajo | Resultado | Pruebas | Evidencia |
|---|---|---:|---|
| L1-native-headless | PASS | 51/51 | [log](../../build/verification-3-7-owner/final-L1-native-headless.log) |
| L1-native-asan | PASS | 49/49 | [log](../../build/verification-3-7-owner/final-L1-native-asan.log) |
| L1-web | PASS | 34/34 | [log](../../build/verification-3-7-owner/final-L1-web.log) |
| reference_movie0-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-reference_movie0-native-headless.log) |
| reference_movie0_skip-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-reference_movie0_skip-native-headless.log) |
| scene_movie0_compare-native-headless | FAIL | 0/1 | [log](../../build/verification-3-7-owner/final-scene_movie0_compare-native-headless.log) |
| scene_movie0_skip_compare-native-headless | FAIL | 0/1 | [log](../../build/verification-3-7-owner/final-scene_movie0_skip_compare-native-headless.log) |
| scene_movie0_compare-native-asan | FAIL | 0/1 | [log](../../build/verification-3-7-owner/final-scene_movie0_compare-native-asan.log) |
| scene_movie0_skip_compare-native-asan | FAIL | 0/1 | [log](../../build/verification-3-7-owner/final-scene_movie0_skip_compare-native-asan.log) |
| scene_title_compare-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-scene_title_compare-native-headless.log) |
| reference_newgame_state-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-reference_newgame_state-native-headless.log) |
| scene_newgame_state-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-scene_newgame_state-native-headless.log) |
| reference_title_audio-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-reference_title_audio-native-headless.log) |
| scene_title_audio_compare-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-scene_title_audio_compare-native-headless.log) |
| reference_xa_op1-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-reference_xa_op1-native-headless.log) |
| scene_xa_op1_compare-native-headless | PASS | 1/1 | [log](../../build/verification-3-7-owner/final-scene_xa_op1_compare-native-headless.log) |
| make-compare | PASS | 0/0 | [log](../../build/verification-3-7-owner/final-make-compare.log) |

CTest: 147 pruebas; {'Passed': 143, 'Failed': 4}. Builds nativo, ASan y web correctos. Referencias reutilizadas; sin recapturas. make compare conserva los binarios matching.

## Única búsqueda acotada de color

Cada variante compara OP1 completo, 3636 cuadros, incluidos bordes negros, contra los mismos frames de Redux. nearest usa mitades hacia abajo; fixed cuantiza los factores decimales publicados al entero más cercano para esa precisión; signed trunca Y+color hacia cero antes del sesgo +128, biased lo hace después del sesgo y antes de saturar a 0..255. No se ajustaron factores a píxeles.

| Variante | Máximo RGB | Medias globales RGB | Mayor media de cuadro RGB | Cuadros con máximo >4 |
|---|---|---|---|---:|
| restored | 4/3/5 | 0.218757606 / 0.169087693 / 0.251815285 | 0.336966146 / 0.284609375 / 0.408007813 | 4 |
| decimal-nearest | 4/3/5 | 0.218909935 / 0.169114941 / 0.251951693 | 0.343346354 / 0.284609375 / 0.418815104 | 4 |
| decimal-floor | 4/3/4 | 0.324380221 / 0.307123126 / 0.321362296 | 0.520820313 / 0.569895833 / 0.562695313 | 0 |
| fixed-8 | 4/3/5 | 0.218757606 / 0.169195645 / 0.250436800 | 0.336966146 / 0.284609375 / 0.393255208 | 4 |
| fixed-12 | 4/3/5 | 0.218909935 / 0.169110171 / 0.251951693 | 0.343346354 / 0.284609375 / 0.418815104 | 4 |
| fixed-16 | 4/3/5 | 0.218909935 / 0.169112467 / 0.251951693 | 0.343346354 / 0.284609375 / 0.418815104 | 4 |
| decimal-trunc-signed | 4/3/5 | 0.322552642 / 0.321432811 / 0.369058303 | 0.523684896 / 0.483828125 / 0.632148438 | 14 |
| decimal-trunc-biased | 4/3/4 | 0.324380221 / 0.307123126 / 0.321362296 | 0.520820313 / 0.569895833 / 0.562695313 | 0 |

Ninguna variante cumple ambos límites. decimal-floor y decimal-trunc-biased respetan el máximo, pero exceden la media de 0,5. Saturar el intervalo firmado y luego sumar +128 equivale algebraicamente a desplazarlo primero y saturar a 0..255 cuando se conserva el mismo piso. Se conserva el modelo restaurado. El replay completo reproduce las métricas del juego y los hashes históricos de los cuatro cuadros.

Antes (d57a330, redondeo por pasada): máximo RGB 5/4/6; medias globales 0,2546425716790429 / 0,2006998375618812 / 0,2903838388396131. Después (restaurado): máximo RGB 4/3/5; medias globales 0,21875760622937293 / 0,1690876929489824 / 0,2518152853696828; mayores medias de cuadro 0,33696614583333334 / 0,284609375 / 0,4080078125. Todas las medias pasan, cuatro componentes azules aún exceden 4/255:

| Cuadro | Canal | Píxel desde cero | Port | Referencia | Error |
|---|---|---|---:|---:|---:|
| 2318 | B | (144,80) | 181 | 176 | 5/255 |
| 2587 | B | (224,64) | 53 | 48 | 5/255 |
| 3439 | B | (176,96) | 24 | 19 | 5/255 |
| 3453 | B | (176,96) | 21 | 16 | 5/255 |

A/V OP1: error 87181 ciclos ≤1128796,8052269602. XA aislado: 54,42419314227058 dB ≥40, desfase cero y misma duración. Start A/V: error 1003094 ≤1128792,2272727273; el fallo es el VBlank de retorno. El PCM mezclado de OP1/XA sigue sin acreditar 40 dB (SNR −inf y duración distinta); es diagnóstico no bloqueante según la especificación.

Diagnósticos de audio conservados (el PCM mezclado no es gate de esta historia):

| Escena / segmento | Registros | XA SNR dB | PCM SNR dB | Duración PCM distinta |
|---|---|---|---|---|
| native-headless-movie0 / 1 | match | 54.42419314227058 | -inf | True |
| native-headless-movie0_skip / 1 | match | sin ventana ≥5 s | sin ventana ≥5 s | n/a |
| native-headless-title_audio / 1 | match | sin ventana ≥5 s | -3.7066586255974574 | True |
| native-headless-title_audio / 2 | match | sin ventana ≥5 s | 8.492502978433144 | True |
| native-headless-xa_op1 / 1 | match | 54.42419314227058 | -inf | True |

## Start y reloj

psx-spx publica promedios utilizables de segunda respuesta de Pause: 0x21181c (2168860) a velocidad simple, 0x10bd93 (1097107) a doble velocidad y 0x1df2 (7666) ya pausado. Se usan como hipótesis de 3.10; no se sustituye ese dato por la medida Redux de 1240612 ciclos. [Fuente primaria](https://psx-spx.consoledev.net/cdromdrive/#cdrom-response-timings).

| Evento | Port ciclos / VB / cuadro | Redux ciclos / VB / iteración |
|---|---|---|
| PadRead ve Start | 176859242 / 121 / 56 | 803688388 / 120 / 45 |
| Pause entra | 176859370 / 121 / 56 | 803689630 / 120 / 45 |
| Pause sale | 177956541 / 122 / 56 | 804930242 / 122 / 45 |
| Retorno 0 | 178187005 / 123 / 56 | 804931349 / 122 / 45 |

La latencia ausente de Pause era real: antes sólo se cobraban 64 ciclos y se retornaba en 121. También se perdía el estado de lectura al desmontar el anillo. Corregidos ambos, Pause termina en 122, pero el retorno sigue fuera del gate: 123 frente a 122. ClearImage/DrawSync cuestan 230464 ciclos en el port frente a 1107 hasta el retorno instrumentado original (1094 hasta el breakpoint end). El modelo GPU cobra 480×480 píxeles más la entrada PsyQ; PadRead ya sucede un VBlank después del original. La primera entrada MDEC conserva la diferencia previa de 20 VBlanks. El último MDEC ya terminó al salir del decode y waitForFlip no espera. No se ajusta GPU, MDEC ni otra constante para ocultar el desfase; la calibración completa de fases queda para 3.10.

## Cuenta de cuadros del original

El STR contiene cuadros 1..3719 y sí contiene 3637. La tabla original fija límite 3637. Las dos trazas Redux leen cabeceras 1..3637, envían al MDEC 1..3636 y presentan 1..3636. Secuencia final: presenta 3635 (4856057611), MDEC 3636 (4856069147), lee 3637 (4856956789), escribe MOVIE_COMPLETED=1 (4856956797), presenta 3636 con el flag activo (4857186405), retorna 1 (4858416486, VB7304). Nunca envía 3637 al MDEC. La cuenta coincide con la matriz corregida por el dueño. Evidencia previa: build/verification-3-7-gates/frame-count.json, str-headers.json y build/evidence/reference/movie0/headers-{1,2}.jsonl.

## Límites y evidencia

La historia sigue abierta por los fallos visuales y Start. A/V, XA, registros y diagnósticos PCM completos están en summary.json. El PCM mezclado es diagnóstico no bloqueante y no demuestra paridad del mixer; el XA corto de Start no alcanza la ventana de cinco segundos. La excepción ASan histórica de reference_newgame_state a los 177,92 s fue SIGTERM de una captura duplicada, no un diagnóstico ASan ni un timeout (asan-interruption.txt).

Las pruebas sintéticas Pause verifican reloj/eventos, velocidades, desmontaje previo y cancelación de sectores. av_stubs ahora exige exactamente 12 VBlanks en vez de 10 porque incluye la nueva pausa a 2×; no es una relajación del gate de escena. El primer fallo de esa expectativa obsoleta y su repetición completa se conservan en initial-results-stale-expectation.json y initial-L1-native-headless-stale-expectation.log. scene_audio_runtime conserva la regresión del guard de evidencia bajo build/. No hay cambios nuevos en src/ ni bytes de ejecutable en fuentes.

Cruces: {'movie0_native_asan_identical': True, 'movie0_skip_native_asan_identical': True, 'restored_frames_match_old_hashes': True, 'full_replay_matches_game_metrics': True, 'source_unchanged_during_tests': True, 'head_unchanged': True, 'spec_unchanged': True}.

Evidencia actual bajo build/verification-3-7-owner/: source-sha256.json, spec.sha256, search.py, search-plan.json, search-results.json, full.c, full-channel-metrics.json, full-residual-pixels.json, skip-timing-port.json, reference-stamp-migration.json, final-results.json y logs. Capturas bajo /dw/build/evidence. El informe anterior queda en build/verification-3-7-owner/prior-report.md.

Informe y JSON completos: `build/verification-3-7/{report.md,summary.json}`. No quedaron reruns solicitados pendientes.

## Decisiones 5 y 6: resultados vigentes (2026-10-21)

## Excepción exacta del MDEC

`movie0.toml` contiene `mdec_allowances`: frame, coordenadas desde cero, canal y valores port/referencia. El comparador sólo exime esos pares exactos cuando exceden 4; no los elimina de medias, máximos, hashes o evidencia. Todo otro componente >4 falla, cada par que empeora falla y todas las medias por canal deben seguir ≤0,5. Una mejora ≤4 pasa sin excepción. Los valores no están en constantes de código.

`test_runner.py` verifica los tres caminos de fallo para cada uno de los cuatro pares, además de aceptación exacta, rechazo de pares desplazados con el mismo error y aceptación de mejoras. La IDCT, YUV→RGB y sus vectores no cambian en esta continuación.

| Frame | Canal | Píxel (x,y) | Port / referencia | Error aceptado |
|---|---|---|---|---|
| 2318 | B | (144,80) | 181 / 176 | 5/255 |
| 2587 | B | (224,64) | 53 / 48 | 5/255 |
| 3439 | B | (176,96) | 24 / 19 | 5/255 |
| 3453 | B | (176,96) | 21 / 16 | 5/255 |

La comparación completa conserva 3636 cuadros, máximos RGB 4/3/5 y medias globales RGB 0,21875760622937293 / 0,1690876929489824 / 0,2518152853696828. Mayores medias de cuadro RGB: 0,33696614583333334 / 0,284609375 / 0,4080078125, todas ≤0,5. Nativo y ASan aceptan exactamente los cuatro pares y ninguna otra excepción.

## Investigación GPU acotada

Se hizo una sola sonda nativa con observación de ClearImage, DrawSync, VSync y aplicación de input. Los observadores no avanzan tiempo y se retiraron antes de los builds definitivos. `gpu-probe.json`, `gpu-investigation.json` y `observation.patch` conservan la evidencia en `build/verification-3-7-decisions56/`.

El rectángulo del consumidor es (0,0,480,480). El asm USA `_clr` decide por `x&63` y `w&63` en 0x800933c4..0x800933e8: con ancho 480 (resto 32) elige GP0(60h), no el relleno rápido GP0(02h). La vía de rectángulo desactiva máscara, amplía el área de dibujo y pone offset cero, restaurando área/offset después. No se justifica sustituirlo por un coste de fast-fill.

La sonda mide cero trabajo GPU previo al ClearImage final y exactamente 230400 píxeles. No hay área equivocada, cola anterior ni cargo duplicado de esos píxeles. ClearImage cobra entrada 64 y agenda 230400 ciclos; DrawSync cobra 64 dentro de ese plazo y espera los 230336 restantes: total desde el retorno de Pause hasta el retorno de MOV_playMovie: 230464 ciclos. `_sync` USA espera cola PsyQ vacía, DMA2 inactivo y GPUSTAT.26 listo (0x800941c4..0x80094230); no corresponde eliminar su espera por el hecho de que el rasterizador host ya escribió VRAM.

[psx-spx, GPUSTAT](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gpu-status-register) respalda la espera durante ejecución, y [Quick Rectangle Fill](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#quick-rectangle-fill) distingue ese comando del rectángulo renderizado. No aportan una fórmula utilizable para convertir este rectángulo a ciclos de CPU. El coste actual de un ciclo por píxel es una hipótesis sin calibrar; los 1107 ciclos del emulador original no prueban por sí solos el coste del hardware. No se encontró un bug respaldado que autorice corregir fórmula o espera. No se modifican GPU, pad, reloj, MDEC ni costes.

| Evento | Port: ciclo / VBlank / frame presentado | Redux: ciclo / VBlank / iteración |
|---|---|---|
| Aplica Start | 176293877 / 120 / 55 | input configurado en 120 |
| PadRead anterior | 175729153 / 119 / 55 | — |
| VSync entra | 176858689 / 120 / 55 | — |
| PadRead ve Start | 176859242 / 121 / 56 | 803688388 / 120 / 45 |
| Pause entra | 176859370 / 121 / 56 | 803689630 / 120 / 45 |
| Pause sale | 177956541 / 122 / 56 | 804930242 / 122 / 45 |
| ClearImage vuelve | 177956605 / 122 / 56 | 804931078 / 122 / 45 |
| DrawSync vuelve | 178187005 / 123 / 56 | 804931323 / 122 / 45 |
| Retorno 0 | 178187005 / 123 / 56 | 804931349 / 122 / 45 |

Start se aplica en 120, sin error de un VBlank en el latch. No hay PadRead entre el de 119 y el de 121: la siguiente iteración llega a VSync en 120 y espera a 121 antes de leer. La fase del bucle difiere (56 frente a 45); el primer MDEC ya difiere, VBlank 9 frente a 29. Atribuir cada ciclo previo requiere calibrar CPU/CD/VLC y no queda resuelto por esta sonda. Se detiene aquí la investigación: Start sigue en 123 frente a 122. Pause mantiene la hipótesis de segunda respuesta psx-spx a 2×, 1097107 ciclos más 64 de entrada; 3.10 recalibra fases y costes.

## Reruns secuenciales

| Trabajo | Resultado | Pruebas aprobadas | Segundos | Log |
|---|---|---:|---:|---|
| L1-native-headless | PASS | 51/51 | 201.07 | [log](../../build/verification-3-7-decisions56/final-L1-native-headless.log) |
| L1-native-asan | PASS | 49/49 | 209.46 | [log](../../build/verification-3-7-decisions56/final-L1-native-asan.log) |
| L1-web | PASS | 34/34 | 525.67 | [log](../../build/verification-3-7-decisions56/final-L1-web.log) |
| reference_movie0-native-headless | PASS | 1/1 | 443.65 | [log](../../build/verification-3-7-decisions56/final-reference_movie0-native-headless.log) |
| reference_movie0_skip-native-headless | PASS | 1/1 | 0.25 | [log](../../build/verification-3-7-decisions56/final-reference_movie0_skip-native-headless.log) |
| scene_movie0_compare-native-headless | PASS | 1/1 | 199.46 | [log](../../build/verification-3-7-decisions56/final-scene_movie0_compare-native-headless.log) |
| scene_movie0_skip_compare-native-headless | FAIL | 0/1 | 2.57 | [log](../../build/verification-3-7-decisions56/final-scene_movie0_skip_compare-native-headless.log) |
| scene_movie0_compare-native-asan | PASS | 1/1 | 574.29 | [log](../../build/verification-3-7-decisions56/final-scene_movie0_compare-native-asan.log) |
| scene_movie0_skip_compare-native-asan | FAIL | 0/1 | 9.03 | [log](../../build/verification-3-7-decisions56/final-scene_movie0_skip_compare-native-asan.log) |
| scene_title_compare-native-headless | PASS | 1/1 | 100.98 | [log](../../build/verification-3-7-decisions56/final-scene_title_compare-native-headless.log) |
| reference_newgame_state-native-headless | PASS | 1/1 | 0.14 | [log](../../build/verification-3-7-decisions56/final-reference_newgame_state-native-headless.log) |
| scene_newgame_state-native-headless | PASS | 1/1 | 313.55 | [log](../../build/verification-3-7-decisions56/final-scene_newgame_state-native-headless.log) |
| reference_title_audio-native-headless | PASS | 1/1 | 0.18 | [log](../../build/verification-3-7-decisions56/final-reference_title_audio-native-headless.log) |
| scene_title_audio_compare-native-headless | PASS | 1/1 | 218.34 | [log](../../build/verification-3-7-decisions56/final-scene_title_audio_compare-native-headless.log) |
| reference_xa_op1-native-headless | PASS | 1/1 | 0.35 | [log](../../build/verification-3-7-decisions56/final-reference_xa_op1-native-headless.log) |
| scene_xa_op1_compare-native-headless | PASS | 1/1 | 110.44 | [log](../../build/verification-3-7-decisions56/final-scene_xa_op1_compare-native-headless.log) |
| make-compare | PASS | — | 0.24 | [log](../../build/verification-3-7-decisions56/final-make-compare.log) |

CTest: 147 pruebas, {'Passed': 145, 'Failed': 2}. Los únicos fallos son el VBlank de Start en nativo y ASan. Builds nativo, ASan y web correctos. Sin diagnósticos de sanitizer. `make compare`: Compiled binaries match with target binaries.

## Integridad, audio y pendientes

Todas las comparaciones del port se recapturaron, dos ejecuciones deterministas por escena cuando corresponde. Las funciones de captura del módulo movie.py conservan el mismo AST. Se verificaron la huella anterior y los 7280 archivos de movie0 y los 98 de movie0_skip. Al migrar el sello se omitieron por error los checkpoints resueltos: el adaptador invalidó movie0 e inició una recaptura innecesaria. Se dejó terminar sus dos ejecuciones; todos sus hashes coinciden con la referencia anterior. Se corrigió el sello de movie0_skip antes de su prueba y se reutilizó. Las demás referencias se reutilizaron. cache-invalidation.json y reference_validation en summary.json documentan el incidente; no cambió la evidencia de referencia.

OP1 y Start repiten idénticos metadatos, píxeles y audio entre nativo y ASan. OP1 A/V: error 87181 ≤1128796,8052269602 ciclos; Start A/V: 1003094 ≤1128792,2272727273. XA aislado: 54,42419314227058 dB ≥40, lag cero y misma duración. Registros SPU coinciden. El PCM mezclado de OP1/XA conserva SNR −inf y duración distinta; es diagnóstico no bloqueante y no demuestra paridad del mixer. Title_audio pasa contenido (744 y 612 escrituras), pero conserva diagnósticos no bloqueantes fuera de paridad: SNR mínimo −3,7066586255974574 y 8,492502978433144 dB, ambos <40, con duración distinta; regla B divergente desde VBlank 129/registro 0x000 en segmento 1 y VBlank 0/registro 0x198 en segmento 2. En segmento 1 el desplazamiento máximo es 138708 muestras; en el segundo, el contenido común conserva máximo 15 y media 2,1552287581699345 muestras. Métricas y divergencias completas en summary.json. Start no alcanza una ventana XA de cinco segundos.

El original presenta 3636 cuadros y el port también. El STR contiene 3637, pero el consumidor lee esa cabecera, activa MOVIE_COMPLETED, presenta 3636 y retorna sin enviar 3637 al MDEC. Evidencia conservada: build/verification-3-7-gates/frame-count.json y build/evidence/reference/movie0/headers-{1,2}.jsonl. No queda discrepancia de cuenta tras la corrección del dueño.

Pendiente bloqueante: retorno Start 123/122. Diferido por decisión 5: los cuatro componentes MDEC listados. Hipótesis de costes GPU/CPU/CD y latencias aún no modeladas: 3.10. La antigua excepción de reference_newgame_state a 177,92 s fue SIGTERM de una captura duplicada, sin diagnóstico ASan; evidencia en build/verification-3-7/asan-interruption.txt. No hay reruns solicitados pendientes, cambios nuevos en src/, bytes del exe en fuentes ni uso de código PCSX-Redux.

Cruces automáticos: {'source_unchanged_during_tests': True, 'spec_unchanged': True, 'head_unchanged': True, 'gpu_pad_clock_models_unchanged': True, 'movie0_native_asan_identical': True, 'movie0_skip_native_asan_identical': True, 'raw_pixel_metrics_unchanged': True, 'exactly_four_listed_pairs_accepted': True, 'movie0_all_checks_pass': True, 'reference_hashes_unchanged': True, 'only_remaining_failures_are_skip_vblank': True}.

Capturas y work dirs bajo /dw/build/evidence y /dw/build/verification-3-7-decisions56. Resumen completo de cada test, métricas y huellas: summary.json. El informe anterior está archivado en build/verification-3-7-decisions56/prior-report.md.
