# Historia 4.10: checks del build web

Verificación local del 2026-11-04, dentro del contenedor, sin commits ni capturas.
Se cargaron completos los dos archivos de `context` antes de implementar.
Entorno: Emscripten 6.0.10, Node 24.19.0, CMake 3.28.3, Playwright 1.64.0;
cuota de CPU `250000 / 100000` (2,5 CPU). Los builds y tests se ejecutan aquí,
según la instrucción de esta sesión; no se usó el runner remoto.

## Fixture de temporización de `av_stubs`

`av_stubs` instala ahora una tabla sintética de direcciones de reverberación:
solo el modo 3 tiene una dirección no nula (`0xf6f8`, en unidades de 8 bytes).
Su área mide 18.496 bytes: 18 transferencias de 1.024 bytes y una de 64.
Esto conserva la aserción Redux de **117.167 ciclos**, sin cargar datos del disco:
`38 + 19 * 6043 + 18496 / 8 = 117167`. No cambia el código de producción.

Se compilaron solo `test_av_stubs`, `test_libspu`, `test_libsnd` y `test_spu` y se
ejecutó una vez:

```sh
ctest --preset native-headless -R '^(av_stubs|libspu|libsnd|spu)$' --output-on-failure
```

Pasaron `spu` (0,37 s), `libspu` (0,01 s), `libsnd` (0,13 s), `av_stubs`
(0,05 s) y su fixture automática `pack` (0,01 s): **5/5, cero fallos**, 0,58 s
en total. Salida y detalles: `av-stubs-review.txt` y
`av-stubs-review-details.txt`. Son todos los tests nativos con llamadas directas
a `SpuClearReverbWorkArea` o la familia `SpuSetReverb`.

`ctest --preset web -R '^web_settings$' --output-on-failure` se ejecutó una sola
vez: **1/1 Passed, 157,08 s**, con **38/38 casos** de Chromium y Firefox pasados.
Se conservan tanto el resumen (`web-settings-review.txt`) como la salida completa
de los casos (`web-settings-review-details.txt`). No se modificaron tests web,
aserciones, reintentos ni timeouts. El fallo de 159 s informado por el dueño no se
reprodujo; su caso y causa siguen sin identificar y se registran en
`docs/implementation-artifacts/deferred-work.md`, sin afirmar que sea ajeno al diff.

## Corrección de la verificación completa del dueño

Esta corrida reemplaza el cierre anterior para los tests SPU y la auditoría de
**cuatro ámbitos**, incluyendo `deploy` sin excepciones y la regla de huecos cero.

El fallo del mapa era una sobrescritura, no una incompatibilidad del lector:
`fnptr_inventory` reutilizaba las opciones de enlace de `dw1`, incluido
`-Wl,--Map=/dw/build/port/web/dw1.map`. El archivo inválido contenía entradas de
`/dw/build/port/web/fnptr-audit/*.o`; su `.rodata` medía 22.668 bytes, frente a
22.716 en el WASM de producción. También provocaba la aserción de igualdad de
tamaños de segmentos en el test contaminado (línea 244 del Python embebido).
Ahora el enlace de auditoría escribe su propio `fnptr-audit/dw1.map`. Se regeneró
el mapa de producción retirando el mapa sobrescrito y enlazando `dw1` otra vez.
El lector conserva todas sus validaciones; la aserción del test contaminado
conserva la igualdad y ahora informa segmento y ambos tamaños si falla.

El test L1 `spu` dependía de los presets retail que ya se cargan desde el Pack.
Ahora instala un preset inventado mediante `sound_set_table`; el oráculo Python
usa las mismas entradas sintéticas y mantiene sus ecuaciones independientes del
renderer. No se copió el preset retail al fixture ni se adoptó el hash del fallo.
`spu-synthetic-oracle-review.txt` conserva los hashes calculados por el oráculo:
`synthetic_reverb` = `d10bb845f8027667c919be6213debfddac6acfaa2b9b5f5d8c5903a174721c00`,
`eon_on` = `e27e9475e824fbe09c5124325e534fb192c732a075c34922157528c3e163d1d0`.
`eon_off` conserva su hash anterior. Siguen activos la protección de RAM fuera
del área de reverberación, la cola no nula con EON y la cola nula sin EON.
Los datos retail siguen reservados a las pruebas con disco de clase B.

Se compilaron solo los objetivos necesarios y se ejecutaron exactamente estos
tests, dentro del contenedor, sin saltos ni reintentos:

```sh
ctest --preset web -R '^spu$'
ctest --preset native-headless -R '^spu$'
DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset web -R '^(pack_disc|web_data_leak|web_data_leak_contaminated)$'
```

| Test | Resultado | Tiempo |
| --- | --- | ---: |
| `spu` web | 1/1, `SPU L1: OK` | 0,19 s |
| `spu` nativo | 1/1, `SPU L1: OK` | 0,35 s |
| `pack_disc` | Passed | 33,79 s |
| `web_data_leak` | Passed | 101,64 s |
| `web_data_leak_contaminated` | Passed | 33,71 s |

Los tres tests con disco terminaron con **100 % pasados, 0 fallos**, en 169,14 s.
`pack_disc` confirmó el SHA-256 canónico
`c01e5c3680ffdf50d0342f16ec978c6d107a33529688dae6a817bd63947f0cf6`.
Las salidas están en `spu-{web,native}-review.txt`, `disc-review.txt` y
`disc-review-details.txt` (este último conserva la salida de los cuatro ámbitos).

| Ámbito | Archivos | Ventanas no triviales | Cubiertas | Fugas |
| --- | ---: | ---: | ---: | ---: |
| `wasm` | 1 | 5.847.855 | 1.071.455 | 0 |
| `deploy` | 8 | 1.017.454 | 0 | 0 |
| `port` | 54 | 1.087.047 | 0 | 0 |
| `tree` | 1.193 | 8.514.016 | 2.322 | 0 |

El mapa válido atribuye 757.487 bytes en 19 segmentos, 733.423 a objetos del juego.
La contaminación detecta la ventana del objeto temporal en el offset 4.596 y
1.296 ventanas del WASM al cambiar el dueño de un rango en el mapa temporal
(primera en 1.657.171); no se cambia el WASM ni se debilitan las aserciones.
El SHA-256 de `dw1.wasm` permanece
`146c992b6391b2f6957fe7692ca4c063ed096c7e0d1ba4ec74e8590dcb01aa88`.
Sin cambios de código del juego, umbrales ni reglas de cobertura; sin pruebas
más amplias, capturas, commits o push.

## Cierre: atribución por mapa del linker

La nueva regla del dueño resuelve las **622 ventanas pendientes**: el check real
termina con cero fugas en los tres ámbitos. Este apartado reemplaza el estado
pendiente de las corridas anteriores conservadas más abajo.

`dw1` genera `dw1.map` con `-Wl,--Map` como producto del enlace. El wrapper pasa
el mapa al detector C. Emscripten desplaza los offsets del archivo después de
`wasm-ld`, por lo que se relacionan los segmentos pasivos por orden y tamaño
exacto, y se traducen los offsets de entrada respecto de cada segmento. En DATA
solo se exceptúan los objetos de la lista `dw1_game_objs` y los huecos sin objeto;
los rangos de otros objetos quedan sin cubrir incluso si coinciden con una
sub-ventana de excepción. Fuera de DATA se conserva la cobertura de 16 bytes.
Un mapa ausente, incompatible o con rangos inválidos produce salida 2 sin aplicar
otra heurística. Las filas internas del linker se tratan como no exentas.

Verificación final de esta continuación, dentro del contenedor:

| Evidencia | Resultado |
| --- | --- |
| `synthetic-map.txt` | `ctest --preset native-headless -R data_leak -V`: 1/1 |
| `data-leak-map.txt` | `pack_disc`, `web_data_leak`, `web_data_leak_contaminated`: 3/3, sin saltos, 167,27 s |
| `size-check-map.txt` | `web_size_check*`: 3/3; Brotli WASM 1.556.918 B, primera carga 1.841.517 B |
| `repro-check-map.txt` | Segundo build limpio; ambos SHA-256 coinciden para `dw1.wasm` y `dw1_pack.wasm` |
| `web-boot-map.txt` | `web_boot`: 14/14 casos Playwright en Chromium y Firefox, incluido el aserto negativo de red |
| `wasm-map.txt` | SHA-256 de `dw1.wasm` idéntico antes y después de agregar el mapa |

| Ámbito | Archivos | Ventanas no triviales | Cubiertas | Fugas |
| --- | ---: | ---: | ---: | ---: |
| `dw1.wasm` | 1 | 5.847.855 | 1.072.298 | 0 |
| Objetos fuera de `dw1_game_objs` | 54 | 1.085.045 | 0 | 0 |
| Árbol versionado existente | 1.148 | 8.275.918 | 2.287 | 0 |

El mapa contiene 19 segmentos serializados y atribuye 757.487 bytes, de los cuales
733.423 proceden de objetos del juego. El test contaminado detecta tanto la ventana
agregada al objeto temporal (offset 4.596) como la atribución de un rango del WASM
a un objeto de `port/`: 1.296 ventanas detectadas, primera en el offset 1.657.171.
El segundo caso usa el WASM real sin modificarlo y una copia temporal del mapa.
No se guardan ventanas del disco en la evidencia.

El sintético conserva los casos anteriores y agrega dos segmentos con offsets
de archivo desplazados respecto del mapa, huecos sin objeto, un solo byte de un
objeto no exento, atribución que prevalece sobre la cobertura de sub-ventanas,
la cobertura fuera de DATA y rechazo de mapas vacíos, segmentos de tamaño
incorrecto, rangos fuera de segmento o superpuestos, dueño inválido, mapa ausente
y WASM inválido o truncado.

Comandos ejecutados:

```sh
cmake --preset web
cmake --build --preset web -j 2
cmake --build --preset native-headless --target dw1-leak-check -j 2
ctest --preset native-headless -R data_leak -V
DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset web -V -R 'pack_disc|web_data_leak'
ctest --preset web -V -R '^web_size_check'
CMAKE_BUILD_PARALLEL_LEVEL=2 tools/port/ci/repro_check.sh build/port/web
ctest --preset web -V -R '^web_boot$'
```

SHA-256 del juego antes y después:
`146c992b6391b2f6957fe7692ca4c063ed096c7e0d1ba4ec74e8590dcb01aa88`.
No se repitieron `web_long` ni las suites de audio, según la instrucción del dueño;
se conserva su evidencia anterior. La evidencia histórica de `long` corresponde
a la corrida anterior al traslado de las tablas de audio, como ya se documentó.
No hubo cambios en `src/`, `include/`, `port/backend/`, `port/web/dev/index.html`
ni `port/web/sw.js`; `git diff --check` pasa. Sin commits, push, capturas ni runner
remoto. No quedan fallos conocidos de esta continuación. Compatibilidad deliberada:
si cambia el formato de mapa de `wasm-ld` o la estructura de segmentos admitida,
el auditor devuelve error y requiere adaptar su lector.

## Continuación anterior: decisiones del dueño del 2026-11-04

Se implementó la cobertura por sub-ventanas de **16 bytes**, manteniendo la
búsqueda de fugas en ventanas de **64 bytes**, el umbral de cinco valores y la
confirmación por comparación de bytes. El caso sintético ahora cubre también
excepciones de exactamente 16 bytes, fragmentos de 15 bytes que no cubren nada
y una ventana de 63 bytes que deja el último byte sin cubrir.

Se eliminaron `spu_reverb.inc` y `libsnd_pitch.inc`. `sound_tables.py` genera
solo metadatos desde `config/us/main.yaml` y `symbols.txt`. `sound_tables.c`
carga `_spu_rev_startaddr`, `_spu_rev_param` y `_svm_ptable` desde el EXE del Pack
antes del arranque nativo o web. Las lecturas usan little-endian; cada registro
de reverberación ocupa 68 bytes (cabecera de 4 bytes y 32 parámetros de 16 bits).
Los tests sin disco usan una tabla de tono calculada y registros de reverberación
sintéticos dispersos; no incorporan una copia de las tablas del EXE.

El wrapper del detector omite archivos versionados eliminados en el árbol de
trabajo, para poder auditar la eliminación de los dos `.inc` antes de un commit.
No se agregaron excepciones a los objetos de `port/`.

`unit-followup.txt`: pasaron `data_leak_check_synthetic`, `libspu`, `libsnd`,
`sound_tables` y su fixture `pack`. El nuevo test de carga comprueba EXE ausente
y corto, rechazo de entradas inválidas y puesta a cero de todas las tablas ante
error. El resultado de los tests L1 de audio se conserva tanto en nativo como
en WASM/Node.

`disc-followup.txt` registra la nueva corrida con disco, **sin `web_long`**.
`data-leak-followup-final.txt` repite la auditoría después de reconstruir los
fixtures sintéticos finales y conserva el fallo (salida 1). Tiene estos resultados:

| Ámbito | Archivos | Ventanas no triviales | Cubiertas | Fugas |
| --- | ---: | ---: | ---: | ---: |
| `dw1.wasm` | 1 | 5.847.855 | 1.028.718 | 622 |
| Objetos fuera de `dw1_game_objs` | 54 | 1.085.045 | 0 | 0 |
| Árbol versionado existente | 1.148 | 8.273.841 | 2.287 | 0 |

El caso contaminado detectó la ventana agregada en el offset 4.596 del temporal
y pasó. Las coincidencias de los objetos PsyQ desaparecieron. La cobertura de
16 bytes reduce las coincidencias pendientes del WASM de 2.016 a 622, pero no
las elimina: el check conserva su fallo, sin modificar el criterio de la spec.
`independent-oracle-16.txt` confirma por búsqueda directa cuatro ventanas del
primer grupo: están en el Pack y dejan el mismo byte sin cobertura (offset
1.717.842). No se atribuye una causa al resto de las 622 coincidencias sin
verificarla.

`web_audio` pasó los 32 casos en Chromium y Firefox, incluido el caso de
resincronización que falló antes. `native-pcm-fixture-followup.json` coincide
exactamente con `story-4-7/native-pcm-fixture.json`: 12.842.405 frames,
SHA-256 `70ae581c607a99df57a281f8d67447efe498101c3d7956178946b4aa18849ef5`,
replay y VBlank final sin cambios.
`title-audio-{chromium,firefox}-followup.json`: ambos navegadores cubren los
51.369.620 bytes nativos con primera diferencia -1, cero underruns y cero
overflows. `audio-{chromium,firefox}-followup.json` conserva los contadores del
caso de audio sintético.

La corrida con disco terminó con 7/8 tests CTest pasados, 1.096,70 s; falló solo
`web_data_leak`. No hubo saltos ni reintentos. La reconstrucción final cambió
solo los fixtures de test: `wasm-followup.txt` confirma el mismo SHA-256 de
`dw1.wasm` antes y después, por lo que las pruebas completas de audio siguen
correspondiendo al programa final. `web-unit-followup.txt` vuelve a comprobar
los dos tests L1 con esos fixtures sintéticos finales.
`sound-tables-disc-followup.txt` confirma los tres hashes cargados contra el BIN,
con una extracción independiente que verifica primero el SHA-256 del EXE frente
al Manifest. Se ejecutó el mismo script registrado como `sound_tables_disc`,
usando el Pack web validado por `pack_disc`:

```sh
DW1_DISC_DIR=/dw/game_rom/extracted sh tools/port/psyq/test_sound_tables_disc.sh /dw/build/port/native-headless/dw1 /dw /dw/build/port/web
```


`size-check-followup.txt`: los tres tests de tamaño pasan. `dw1.wasm` ocupa
1.556.918 bytes con Brotli y la primera carga 1.841.517 bytes, dentro de
4.000.000 y 6.000.000 bytes respectivamente. Los dos casos negativos siguen
usando 1.000 bytes.

`repro-check-followup.txt`: el segundo build limpio en `web-repro` da los mismos
SHA-256 para `dw1.wasm` y `dw1_pack.wasm`. Se usó el mismo árbol modificado, sin
crear commits, con `CMAKE_BUILD_PARALLEL_LEVEL=2`.

Comandos de la continuación (todos dentro de este contenedor):

```sh
cmake --build --preset native-headless -j 2
cmake --build --preset web -j 2
ctest --preset native-headless -V -R '^(libspu|libsnd|sound_tables|data_leak_check_synthetic)$'
DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset web --output-on-failure -V -R '^(web_data_leak|web_data_leak_contaminated|native_title_audio|web_scene_title_audio|web_audio|libspu|libsnd)$'
# Tras ajustar y reconstruir los fixtures sintéticos, sin cambiar el WASM del juego:
ctest --preset web -V -R '^(libspu|libsnd)$'
DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/data_leak_check build/port/web /dw
ctest --preset web -V -R '^web_size_check'
CMAKE_BUILD_PARALLEL_LEVEL=2 tools/port/ci/repro_check.sh build/port/web
```

`git diff --check` pasa. Sin cambios en `src/`, `include/`, `port/backend/`,
`port/web/dev/index.html` ni `port/web/sw.js`. No se ejecutó `web_long`, no se
hicieron commits ni capturas. **La historia sigue pendiente de aceptación por
las 622 ventanas sin cobertura del WASM.** La implementación de las dos reglas
nuevas y las pruebas de audio están completas; queda resolver esas coincidencias
con el criterio aprobado, sin ampliar excepciones ni reducir la cobertura.

Los apartados siguientes conservan los resultados de la corrida anterior;
no describen el build después de mover las tablas. Los JSON de `long` se
conservan como evidencia histórica y no se volvieron a generar.

## Tamaño y reproducibilidad

- `size-check.txt`: salida positiva de `web_size_check`, Brotli calidad 11.
  WASM: 1.556.415 / 4.000.000 bytes; primera carga: 1.840.966 / 6.000.000.
  Incluye `version.txt` (39 bytes). Los dos límites negativos de 1000 bytes pasan.
  También se comprobó que un archivo ausente devuelve 1 y nombra su ruta.
- `repro-check.txt`: los dos SHA-256 coinciden para cada WASM tras reconstruir en
  `build/port/web-repro`. Ambos builds usaron el mismo árbol de trabajo modificado.
  No se creó un commit para la prueba.

La primera comparación falló: el WASM contenía 119 rutas `./build/port/web/` frente
a 119 rutas `./build/port/web-repro/`. Clang aplica el último prefijo coincidente;
el prefijo de fuentes eclipsaba al del build. CMake ahora aplica el de fuentes
primero y el del build después. La comparación final terminó con salida 0, sin
necesitar `SOURCE_DATE_EPOCH`. El script también rechaza comparar el directorio
consigo mismo, incluso a través del alias `/envs/demian/dw-port`.

## Red y detector

El test negativo observa dos peticiones a `http://127.0.0.2:9/`, de página y
worker, incluso cuando comparten URL. Otros casos comprueban un AudioWorklet
fallido y un módulo con una dependencia externa cargada de verdad. Los navegadores
de test permiten el puerto 9 para que la petición alcance la capa de red.

La hipótesis de eventos completos de contexto no se cumple en Chromium: ni el
módulo de AudioWorklet ni sus imports aparecieron en `context.on('request')` o en
`Network.requestWillBeSent`. El helper usa además `Fetch` de CDP del navegador,
continúa cada petición sin modificarla y combina los contadores por URL para no
duplicar eventos ni perder solicitudes repetidas. Firefox usa eventos de contexto.
Las seis pruebas dirigidas pasaron (`network-negative.txt`); el `web_boot` completo
también pasó con la versión final que comprueba dos peticiones a la misma URL.
`title-network.txt`: los cuatro casos de captura de título pasaron en Chromium y
Firefox (almacenamiento normal y envenenado), incluyendo el aserto de red `[]`.

`synthetic.txt` verifica rellenos de cero y cuatro valores, el límite de cinco
valores, cobertura que cruza dos excepciones, cobertura parcial, excepciones
triviales y archivos ausentes. Incluye dos ventanas distintas con el mismo hash
rodante (diferencia polinómica de 2^64): se confirma la comparación de bytes tanto
para las excepciones como para la pertenencia al Pack.

`data-leak-initial.txt` conserva la primera auditoría real. Encontró 2.016 ventanas
no cubiertas en `dw1.wasm` y 406 en los 52 objetos de `port/` de esa corrida; el
árbol versionado pasó con cero fugas. El wrapper final incluye también los objetos
generados fuera de `dw1_game_objs`, conforme al Code Map. No se modificaron datos,
excepciones ni el umbral para ocultar esas coincidencias. El resumen final por
archivo incluye ruta, offset, hash y cantidad; nunca el contenido coincidente.

## Suite completa sin disco

`web-ctest.txt`: 64 tests CTest, 51 pasados, 12 saltados y 1 fallido; 1.309,80 s.
Los nuevos tests de clase B se saltaron con 77 y los tres de tamaño pasaron.
El único fallo fue `web_audio`, Chromium, `audio.spec.cjs:82`: una resincronización
frente al límite de cero. Los otros 31 casos de audio pasaron. No se reintentó,
no se alteró el aserto y no se atribuye una causa a la cuota de CPU sin evidencia.

## Ritmo con disco

`web_long` pasó en Chromium y Firefox, en serie (1.246,05 s de test completo).
Los archivos `long-chromium.json` y `long-firefox.json` conservan todas las muestras.

| Navegador | Segundos | VBlanks | Desvío | Resincronizaciones | Red externa | Underruns / overflows |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Chromium | 601,39913 | 36.091 | 0,0050 % | 0 | `[]` | 0 / 0 |
| Firefox | 601,3387 | 36.086 | 0,0012 % | 0 | `[]` | 0 / 0 |

También pasaron los límites de memoria, presentación y supervivencia al bloqueo
del hilo principal. No se cambió ningún umbral ni se reintentó la suite.

## Auditoría final con disco

`disc-ctest.txt`: `pack_disc`, `web_long` y `web_data_leak_contaminated` pasaron;
`web_data_leak` falló. La fixture confirmó el SHA-256 canónico del Pack. El caso
contaminado detectó el archivo temporal y su offset, sin conservar los bytes.

| Ámbito | Archivos | Ventanas no triviales | Cubiertas | Fugas |
| --- | ---: | ---: | ---: | ---: |
| `dw1.wasm` | 1 | 5.845.577 | 951.861 | 2.016 |
| Objetos fuera de `dw1_game_objs` | 53 | 1.073.963 | 0 | 406 |
| Árbol versionado | 1.150 | 8.274.620 | 1.023 | 0 |

Las 406 coincidencias de objetos corresponden a `libsnd.c.o` (338 ventanas) y
`libspu.c.o` (68). Los offsets y hashes figuran en el log; no se afirma que sean
copias intencionales. Son coincidencias no exentas según el criterio de la spec.
No se cambiaron los archivos excluidos del alcance para corregirlas.

`independent-oracle.txt`: una comprobación independiente con búsquedas directas de
bytes confirmó cuatro ventanas del WASM en el Pack y su cobertura incompleta por
los objetos del juego (1 a 3 bytes no cubiertos). El procedimiento solo conservó
rutas, offsets, cantidades y SHA-256; no guardó ni imprimió las ventanas.

La corrida anterior quedó pendiente por esas coincidencias y por el fallo de
audio de la suite completa. La continuación de arriba informa el estado actual;
no se declara la historia terminada con checks fallando.
