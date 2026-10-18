# Runner de Escenas

El runner conserva dos corridas y exige capturas P6 y trazas de reloj idénticas:

```sh
python3 -I -B tools/port/scenes/run.py title --build native-headless
python3 -I -B tools/port/scenes/run.py title --build native-asan --compare
```

`--compare` exige `reference_sha256` en el TOML. Ese hash identifica el archivo PPM completo
producido por `tools/port/reference/capture.py`; la comparación de píxeles usa cada canal
`>> 3` (RGB555). La referencia local debe corresponder al hash fijado y tener las mismas
dimensiones. Se busca en `build/reference/<id>/run-1.ppm` o
`$DW1_REFERENCE_DIR/<id>/run-1.ppm`. Si falta y el hash del port difiere, falla indicando
ambos hashes y cómo capturar la referencia; si los hashes coinciden, basta esa evidencia.

Excepciones opcionales (el título no tiene ninguna):

```toml
[[exceptions]]
region = [10, 20, 30, 40]
reason = "Motivo verificado de esta diferencia"
```

La región incluye ambos extremos `[x0, y0, x1, y1]`. Debe estar dentro del frame de referencia
y tener un motivo no vacío; se valida antes de ejecutar el juego. Sin referencia local no
se pueden validar regiones. Las regiones solapadas asignan el píxel a la primera declarada.
El informe lista cada región, motivo y cantidad descontada. Diferencias fuera de ellas salen
con 1 e imprimen `N pixels differ, box x0..x1 y0..y1`. Las diferencias se resaltan en magenta
sobre negro en `diff.ppm`, junto a las capturas bajo `build/port/<preset>/scenes/<id>/`;
se elimina el diff anterior al iniciar una comparación. No se suben imágenes a Git ni a CI.

`scene_title_compare` es clase B (77 sin disco); su fixture `reference_title` genera primero
la referencia. `native-disc` ejecuta ambos. `scene_title` conserva la verificación de
determinismo sin comparar. Los tests sintéticos del runner no necesitan disco.

## Inputs y estado (historia 3.3)

`inputs_from = "frame0"` es el valor por defecto; `"mark"` cuenta desde la primera
entrada a `mark`. `inputs` es una lista ordenada de estados completos del mando:

```toml
inputs_from = "mark"
inputs = [
  {vblank = 60, buttons = [3]}, # Start
  {vblank = 68, buttons = []},
]
expect_rand = true
```

Los índices son los bits del paquete PS1 (Start 3, abajo 6, cruz 14). Cada entrada
reemplaza los botones retenidos hasta la siguiente; se reaplican después de cada
VBlank, antes de volver al juego. `PadRead` intercambia los bytes: Start es `0x800`
y cruz `0x40`. Una pulsación debe durar suficiente para que el juego la consulte;
New Game usa `VSync(3)`. El host admite `--inputs FILE --inputs-from frame0|mark`;
el runner genera ese archivo (`VBlank máscara_hex`) bajo su directorio de salida.

La decisión 5 agrega puntos de control por entrada a función, contados desde la
marca. `símbolo#n` selecciona la n-ésima entrada (sin sufijo equivale a `#1`):

```toml
checkpoints = ["runLandingScreen", "runMainMenu", "newGameScene", "randomLimit#17"]
window = 0
inputs = [
  {after = "runLandingScreen", vblank = 60, buttons = [3]},
  {after = "runLandingScreen", vblank = 68, buttons = []},
  {after = "runMainMenu", vblank = 135, buttons = [14]},
]
```

Cada lado cuenta el offset del input desde su propia llegada al punto. Se muestrea
la entrada y los offsets 1..`window` (0 por defecto), aunque las cargas de CD duren
distinto. Los puntos deben alcanzarse en el orden declarado; se permiten ventanas
solapadas. Los eventos se ordenan por punto y offset; cada evento pendiente se
aplica en su primer latch elegible, incluso si otro punto ya se alcanzó. A igualdad
de latch prevalece el último evento de la lista. El host recibe `--checkpoints FILE`
con líneas `etiqueta ventana`; los inputs pasan a `etiqueta offset máscara_hex`.
Los símbolos se validan en ambos lados antes de ejecutar. El nativo instrumenta las
entradas de funciones del juego; la referencia usa breakpoints Exec. `capture_vblank`
se conserva para el modo anterior y para dimensionar el límite de la referencia;
con puntos, la captura termina cuando se completan todas las ventanas.

```sh
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/scenes/run.py newgame --build native-headless --state
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/scenes/run.py newgame --build native-asan --state
```

`state_vars.txt` declara `símbolo tipo[cuenta]` (cuenta opcional, positiva), con
comentarios `#`. Tipos: `u8 s8 u16 s16 u32 s32 ptr`; no se admiten variables del BIOS.
Antes de ejecutar se comprueban los símbolos en `config/us/symbols.txt` y en el ELF
nativo, además del tamaño de almacenamiento nativo. Hay 15 variables, incluida
`SCRIPT_STATE_PTR ptr`. El host recibe `--state-vars FILE --state-trace FILE`.

Las trazas contienen `k state símbolo[i] valor` en cada VBlank 1..checkpoint y
`k rand índice valor` por llamada desde la marca (índice desde cero). Enteros en
decimal; punteros NULL `0`, arena `0x80000000 | offset`, globales `símbolo+offset`.
El punto de lectura es el observer posterior al despacho del VBlank, antes del
retorno al juego, igual que la captura. Con puntos, las filas son
`punto offset VBlank state símbolo[i] valor`, incluido el offset 0 en la entrada;
el VBlank se cuenta desde la marca y se conserva para el diagnóstico. Se comparan
por punto/offset/variable y por secuencia de valores de `rand`, sin exigir tiempos
iguales entre port y referencia. Los errores incluyen el punto y el VBlank de cada
lado. Las dos ejecuciones de cada lado deben seguir siendo deterministas.

La referencia de estado se genera automáticamente si falta o cambia la huella
de la escena, variables, símbolos, adaptador o runner, con identidad del BIOS
(`openbios` o SHA-256 del BIOS externo). El sello se escribe tras validar toda
la captura, incluido el hash fijado de imagen. Las dos trazas de referencia deben
ser idénticas byte a byte, incluidos sus VBlanks. Los errores conservan las trazas
y nombran la primera variable/VBlank o el índice de llamada a `rand` distinto.
`expect_rand = true` exige al menos 16 llamadas. El diff RGB555 de esta escena es
informativo: sus píxeles no tienen que coincidir. El título conserva su comparación
estricta. Archivos generados solo en `build/` o `$DW1_REFERENCE_DIR`.

`scene_newgame_state` y su fixture `reference_newgame_state` son clase B y forman
parte de `native-disc`. La secuencia recorre título → menú → New Game → dos
selectores de nombre → diálogos de introducción → primer mapa. Start confirma los
nombres y Cross avanza los diálogos; los inputs de cada tramo parten de su propio
punto de control. `randomLimit#17` termina la captura antes de la siguiente llamada
a `rand`, después de 16 llamadas naturales desde la marca.

La corrida nativa directa alcanza ese punto en marca+13518. Las primeras salidas
son `14136, 3074, 29727, 27909`; la decimosexta es `28128`. La inicialización del
juego consume `rand` antes de la marca: no se reinicia la semilla al alcanzarla.
Los VBlanks de referencia pueden diferir por las cargas de CD (historia 3.10).
La comparación directa nativo/ASan coincide en los nueve puntos, las 15 variables
y los 16 valores de `rand`, sin errores de sanitizadores. Las dos capturas completas
de referencia coinciden también en esos valores:
`randomLimit#17` llega en marca+20680 (captura en VBlank 29323 desde el arranque).
Las dos capturas de referencia tienen SHA-256
`8557c88ccf5ea4711a314a27261027c45bbc20b602bdd3b0cef914827265ba79`.
Los runners nativo y ASan pasan dos veces con capturas, relojes y trazas
idénticos, también entre builds. El diff informativo contra la referencia es
de 403/76800 píxeles RGB555 (rectángulo 87..217, 46..121).
El SHA-256 de la captura del port es
`b4118ec55b68fcc8610a5323aa467563921cc0e091dcf48231fbaed78c416daf`.

Los desbordes y el acceso NULL que detenían la candidata anterior se corrigieron
con evidencia del asm y sondas de la referencia; los detalles están en
`src-changes.md`. El build PS1 conserva su resultado idéntico (`make compare`).
La regresión de título pasa con una referencia recién capturada y cero píxeles
RGB555 distintos. Las pruebas de preprocesado pasan en ambos presets después de
preservar exactamente las líneas del build PS1.


Verificación en el contenedor: CTest completo sin disco, cero fallos en ambos
presets (49 y 47 tests, con 8 pruebas de disco omitidas en cada uno); las pruebas
de escenas con disco pasan las siete sin omisiones, usando las referencias recién
generadas. Se verificaron además el título con ASan, `gte_tables_disc` y
`dw1_overlay` en ambos builds. Las dos referencias de New Game y sus dos corridas
por build coinciden en estado y `rand`. Evidencia: `build/ctest-scenes-final.log`,
`build/scene-newgame-asan-verified.log`, `build/reference-newgame-renewed.log`,
`build/ctest-native-complete.log` y `build/ctest-asan-complete.log`.
