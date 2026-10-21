# MDEC y libpress (historia 3.7)

`port/backend/mdec.c` adapta el decodificador de `Wyrelade/DW2-PC`
(`backend/mdec.c`, CC0 1.0). El orden zigzag, la cuantización y los comandos siguen la documentación de
[psx-spx](https://psx-spx.consoledev.net/macroblockdecodermdec/).
La IDCT usa las 13 bits superiores de la escala cargada y acumula en 64 bits.
Cada una de sus dos pasadas transpuestas calcula
`floor((sum + 4095) / 8192)`: las mitades exactas se redondean hacia abajo,
incluidos los valores negativos. Ya no conserva la fracción de la primera
pasada hasta el final. La descuantización AC calcula
`floor((signed10 * qt[k] * q_scale + 4) / 8)` y satura a 11 bits;
DC no aplica `q_scale/8`, y `q_scale=0` usa el doble del coeficiente sin zigzag.
Las divisiones negativas descartan fracción hacia menos infinito explícitamente.

YCbCr usa los factores decimales de psx-spx (1,402; −0,3437; −0,7143;
1,772), representados como racionales con denominador 10000, y redondea
antes de saturar a −128..127 y convertir el signo. **Hipótesis:** el redondeo
de color es al más cercano, con mitades hacia abajo. psx-spx califica su
IDCT de aproximada y declara desconocida la precisión fija de YUV→RGB;
también deja como posibilidad el límite de 9 bits en color. No permite
deducir un redondeo exacto del hardware. No se añade ese límite especulativo
ni se afirma exactitud bit a bit. La precisión se mide contra Redux.
No se incorpora código de decodificadores GPL ni instrucciones MIPS ejecutadas
por el port. La tabla zigzag describe un algoritmo; no procede del ejecutable.

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
como **hipótesis sin calibrar**. El dueño autorizó investigar y calibrar sólo
ese coste tras resolver el gate visual; el residual visual impide llegar a
ese paso en esta continuación. No se atribuye todavía el VBlank 121 frente
a 122 a ese coste sin una traza causal. `MOV_waitForFlip` cede 1024 ciclos por iteración
sólo bajo `DW1_PORT`; el build PS1 conserva su bucle original.

## Verificación y evidencia

`mdec_vectors.py` calcula independientemente los productos `Sᵀ A` y `T S`,
redondeando ambos, mediante matrices de Python y tablas sintéticas asimétricas;
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
No se modifica el bloque congelado de la especificación ni sus tolerancias.
Los gates exigen máximo por componente ≤4 y media de **cada canal** ≤0,5,
3637 frames para OP1 completo, retorno 1/0, mismo VBlank de salida con Start,
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

### Revisión del redondeo (2026-10-21)

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
