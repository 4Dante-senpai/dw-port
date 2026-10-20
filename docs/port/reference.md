# Emulador de referencia y captura automática

PCSX-Redux se fija en `2068784a552b598940191e8431585b7f96408b3f` (HEAD de `main` al implementar la
historia 3.1, 2026-10-17). OpenBIOS sale del submódulo `src/mips` de ese mismo commit
(`361f3f6a6f8d8bb98548900d8a231e2d2a2c1dc4`). En `tools/port/Dockerfile`, la etapa `pcsx-redux`
parte de la etapa `toolchain`, así que reutiliza su SDL3 (`/opt/sdl3/native`). A la imagen final
solo pasan el ejecutable (`/usr/local/libexec/pcsx-redux`, enlazado en `/usr/local/bin`),
`openbios.bin`, las licencias y `version.json`. La imagen final suma Xvfb y las bibliotecas
de runtime. El build termina con `xvfb-run -a pcsx-redux -version`.

La comparación local con SCPH-1001 del dueño derivó la semilla de arranque `0x24040001`.
El port la adopta y el Dockerfile parchea OpenBIOS con ese valor en una capa posterior
al build del emulador, recompilando solo OpenBIOS. El build falla con un mensaje explícito
si no queda la declaración esperada de `s_currentSeed`. Con ese parche, OpenBIOS reproduce el
estado, los `rand` y el frame de New Game de SCPH-1001 (ver "Resultado"). No hay BIOS de Sony ni
datos del disco en la imagen ni en el repositorio. PCSX-Redux es GPL-2.0-or-later y se distribuye como ejecutable
separado del port; OpenBIOS es MIT.

## Uso

Dentro de la imagen `dw-port`:

```sh
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py title
# Fijar o reemplazar el hash; solo con dos corridas idénticas:
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py title --update
# Reutilizar una referencia de estado válida:
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py newgame \
  --state --reuse --max-vblanks 40000 --timeout 1800
python3 -I -B tools/port/reference/test_capture.py
```

Las dos pasadas se ejecutan en paralelo, cada una con su directorio portable y Xvfb.
Los displays iniciales son 199 y 299, con búsqueda automática si están ocupados. Se
conservan intérprete y debugger; el timeout sigue siendo por pasada. Si una falla,
se espera a la otra antes de salir y el error identifica su log; no queda sello válido.
Cada corrida del título dura unos 2-3 minutos con el intérprete. Sin `.cue`, emulador o Xvfb, el script
sale con 77 (`skipped`); ante un error o una diferencia, con 1. Las capturas `run-N.ppm`, los logs
y `work-N/` (display crudo, progreso, configuración portable y tarjetas vacías) quedan en
`build/reference/<id>/`, o bajo `DW1_REFERENCE_DIR` si se lo define fuera del repo (`--out`).
`--timeout` fija los segundos por corrida (por defecto, 600) y `--max-vblanks` el límite de
VBlanks desde el arranque (por defecto, 10000).

## Punto de captura e inputs

Las direcciones salen de `config/us/symbols.txt`.

- **Frame 0** (AD-15): retorno de la primera llamada a `VSync` del juego. Un breakpoint Exec en
  `VSync` lee `ra` y pone otro breakpoint en esa dirección. Es el mismo criterio que
  `port_frame_done` del port. `main` no llama a `VSync` directamente (0 `jal` a `0x80091CA8` en
  `0x800EE800..0x800EEBDC`), así que "llamado desde `main`" se interpreta como "desde el programa
  del juego". Antes de `main` no hay llamadas a `VSync`.
- **Marca**: primera entrada al símbolo `mark` del `.toml`. Si llega antes del frame 0, el script
  falla.
- **Captura**: en el listener `GPU::Vsync` (después de despachar el VBlank y antes de volver al
  juego), cuando `VBlanks − marca = capture_vblank`. `PCSX.GPU.takeScreenShot()` devuelve el
  rectángulo de display según GP1.
- **Inputs**: `{ vblank = k, buttons = [bits 0..15] }`, con `k` contado desde el frame 0 por defecto,
  o desde la marca con `inputs_from = "mark"`, y
  estrictamente creciente. Cada entrada reemplaza el estado anterior. El estado se aplica en cada
  latch de VBlank con `PCSX.SIO0.slots[1].pads[1].setOverride/clearOverride`. Con `inputs = []`
  no se presiona nada. El adaptador prueba los 16 overrides con `getButton` al arrancar,
  y los libera antes de ejecutar el juego. Start no vacío se verificó además en el juego:
  `FIRST_SCREEN_PRESSED_START` cambia en marca+61 tanto en referencia como en el port.

Para el determinismo, la corrida usa intérprete, GPU software, RAM retail, SPU `dummy` con
NullSync, `Speed=0` y configuración portable propia. La conversión a P6 es la misma de
`gpu_read_display`: RGB555 little-endian sin el bit 15, cada canal `(c << 3) | (c >> 2)`;
RGB24 pasa tal cual. Se rechazan las capturas de 16 colores o menos. Si dos corridas dan hashes
distintos, el script falla, incluso con `--update`. Si un hash fijado no coincide, también falla
y no toca el `.toml`.

No se captura audio (historia 3.5). La comparación del port se ejecuta con
`tools/port/scenes/run.py title --build native-headless --compare` (ver `scenes.md`).

## Escena `title` (OpenBIOS)

| Dato | Valor |
|---|---|
| Frame 0 | VBlank 822 desde el arranque |
| Marca `runLandingScreen` | VBlank 8642 |
| Captura | VBlank 8702, 320×240, 256 colores |
| SHA-256 (2 corridas) | `561323665a33d833c89339030f6a8c60e4a395add2cff467d1d47e92dd46260d` |

Resultado de la historia 3.2: `native-headless` y `native-asan` producen el mismo SHA-256 de la tabla,
con 0 píxeles RGB555 distintos y sin excepciones. Se conserva el hash de referencia original. El título web también coincide con el nativo
en Chromium y Firefox (`web_scene_title`, 4 casos).

La inferencia en sala limpia comparó numéricamente `TITLE256.TIM` con la PPM, sin leer
SoftGPU: truncar UV daba 10.674 diferencias; redondear solo al más cercano dejaba diferencias
en x=128. Las intersecciones de aristas calculadas en 16.16 (división truncada), seguidas de
interpolación horizontal y redondeo UV al más cercano, reproducen también esa columna:
el triángulo superior toma el texel 128; el inferior toma 127 salvo intersecciones exactas
(filas múltiplos de 15). Esto incluye la fila 210, que distingue la división por fila de
acumular pasos de arista truncados. Un test sintético verifica las 256×240 muestras con un
oráculo por columnas, independiente de las ecuaciones del renderer. Los SHA de VRAM
existentes siguen iguales. No se cambia el color interpolado: el título usa color constante;
la paridad general de primitivas sigue en `deferred-work.md`.

## APIs Lua verificadas (SHA fijado)

`PCSX.addBreakpoint(addr, 'Exec', 4, cause, fn)` (los wrappers deben retenerse),
`PCSX.getRegisters().GPR.n.ra`, `PCSX.Events.createEventListener('GPU::Vsync', fn)` (despacha
entre bloques de CPU), `PCSX.GPU.takeScreenShot()` (bpp 0 = 16, 1 = 24), `tostring(shot.data)`,
`PCSX.SIO0.slots[1].pads[1].setOverride/clearOverride(bit)` y `PCSX.quit(code)` se
probaron en ejecución real bajo Xvfb, incluidos los inputs no vacíos de la historia 3.3.

`PCSX.getMemPtr()` devuelve un `uint8_t*` de RAM (dirección PS1 enmascarada con
`0x1fffff`); sus lecturas little-endian se verificaron con las trazas reales de 15
variables. `PCSX.getRegisters().GPR.n.v0` en el breakpoint de retorno permite leer
`rand`; `ra` identifica ese retorno y los wrappers `:enable()`/`:disable()` evitan
contar ejecuciones ajenas. Estas APIs se comprobaron en las fuentes del SHA fijado
(`src/core/pcsxffi.lua`, `pcsxlua.cc`, `pad.cc`) y mediante sondas en ejecución.

## Estado y semilla de rand

`--reuse` exige `--state` y rechaza `--update`. Después de comprobar disco, emulador,
Xvfb y BIOS, compara `state.sha256` con la huella actual y exige `run-1.state`,
`run-2.state` y `run-1.ppm`. Si hay `reference_sha256`, comprueba además el SHA-256
de esa PPM. Si todo coincide, imprime `reference_<id>: cached` y termina sin lanzar
el emulador ni borrar archivos. Una huella distinta, archivos faltantes o un pin
distinto provocan una captura completa; el sello se escribe solo tras validar
determinismo, trazas y pin. Sin `--reuse`, se recaptura siempre.

El fixture CTest `reference_newgame_state` usa `--reuse`. En CI, únicamente
`native-disc` y `native-asan-disc` guardan `build/reference/` en la caché local del
runner privado `dw1-data`, con `when: always` y clave derivada de `.gitlab-ci.yml`.
Esa clave incluye el digest de la imagen que fija PCSX-Redux/OpenBIOS; la huella
determina la validez de cada captura. Las referencias no se publican como artifacts.
Una caché parcial sin sello válido se recaptura.

Verificación local (2026-10-18): New Game pasa de 1560,271 s secuenciales (26 min)
a 871,147 s en paralelo (14 min 31 s), un 44,2 % menos de espera bajo la carga de
esta ejecución. Las dos trazas, ambos frames y el sello coinciden byte a byte con
la captura secuencial; el frame conserva `48322f1c…e508`. Con la referencia válida, CTest
`reference_newgame_state` termina en 0,13 s e imprime `reference_newgame: cached`,
sin modificar los logs del emulador. Las mediciones usan este contenedor y el disco
del dueño; los detalles y la comparación de tiempos están en Implementation Notes
de `docs/implementation-artifacts/spec-e3-capturas-de-referencia-mas-rapidas.md`.

`capture.py <id> --state` genera dos `run-N.state`, con lecturas después del despacho
de cada VBlank y antes de volver al juego en modo legado; con `checkpoints`, en cada
entrada a función y en los offsets 1..`window` posteriores. Las
declaraciones y el formato están en `scenes.md`. Los símbolos globales y sus tamaños
salen de `config/us/symbols.txt`; cuando falta tamaño se usa la siguiente dirección
distinta. NULL se escribe `0`; globales `símbolo+offset`; el resto de RAM se normaliza
a `0x80000000 | offset`. El breakpoint en `rand` (`0x8009127c`) registra cada salida
al alcanzar su `ra`, junto al VBlank relativo a la marca.

OpenBIOS upstream inicializa `s_currentSeed` a **0** (`src/mips/openbios/kernel/libcmisc.c`,
submódulo nugget `361f3f6a…`). Una sonda aislada al primer `VSync` llamó al `rand`
original 16 veces y observó:
`0, 21468, 9988, 22117, 3498, 16927, 16045, 19741, 12122, 8410, 12261, 27052,
5659, 9758, 21087, 25875` (`build/reference/seed-probe/run.log`). Son las salidas del
LCG `seed = seed * 0x41c64e6d + 0x3039`, `(seed >> 16) & 0x7fff`, con aritmética de
32 bits. Esta medición corresponde al OpenBIOS sin parche; el port ahora adopta
`0x24040001`, con primeras salidas `20346, 17442, 3669, 9391, 11839`. El LCG y el
conteo de llamadas se conservan. La sonda cambia el PC solo en el emulador de referencia: no sustituye la
exigencia de 16 llamadas producidas por la escena con inputs.

La captura nueva no requiere hash fijado de imagen; compara determinismo de ambas
corridas y estado. `run.py --state` informa el diff de imagen sin exigir igualdad.
Los puntos se cuentan desde la marca, admiten `símbolo#n` y tienen un origen de
inputs independiente en cada lado. Los VBlanks se conservan para diagnóstico; se
comparan punto/offset y valores ordenados de `rand`. Las cargas de CD todavía
producen tiempos distintos (3.10). Los breakpoints de entrada, ocurrencias, ventanas
e inputs se verifican además ejecutando el adaptador Lua real con una API sintética
en LuaJIT de PCSX-Redux. Las dos capturas completas de New Game coinciden en
nueve puntos de control,
15 variables y 16 llamadas naturales a `rand`; los runners nativo y ASan pasan
dos veces contra ellas. No se leyó código de SoftGPU para implementar este soporte.

## Repetición con BIOS del dueño

Guardar el BIOS fuera del repositorio y correr:

```sh
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py title \
  --bios /private/bios/owner.bin --out build/reference-owner/title
```

El script hace dos corridas e imprime el hash junto al de OpenBIOS. No escribe `title.toml`
(`--bios --update` se rechaza). El resultado se registra acá: si coinciden o qué difiere.

`docker_run.sh` monta solo el repo, y `--bios` rechaza rutas dentro de él. Con el BIOS y el
disco en `game_rom/` (ignorado por git), montar ese directorio aparte en `/private` y usar
el daemon local (el contexto por defecto de esta máquina es remoto):

```sh
docker --context default run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD":/dw -v dw_venv:/dw/.venv -v "$PWD/game_rom":/private:ro -w /dw \
  -e DW1_DISC_DIR=/private/extracted dw-port:latest \
  python3 -I -B tools/port/reference/capture.py newgame --state --max-vblanks 40000 \
  --timeout 1800 --bios /private/SCPH1001.BIN --out build/reference-owner/newgame
```

`newgame` necesita los mismos `--state --max-vblanks 40000 --timeout 1800` que usa
`run.py`: con el límite por defecto (10000 VBlanks desde el arranque) se corta, porque el
BIOS real tarda más en arrancar (frame 0 en el VBlank 1320).

### Resultado (2026-10-18, SCPH-1001)

BIOS del dueño: SCPH-1001 (NTSC-U), MD5 `924e392ed05558ffdb115408c263dccf`, SHA-256
`71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3`. Ambas escenas dieron dos
corridas idénticas entre sí.

- **`title`: coincide.** `56132366…260d`, el mismo hash que OpenBIOS y que `title.toml`.
- **`newgame`: el estado coincide, `rand` no.** Las 15 variables de los 9 puntos de control
  tienen los mismos valores que con OpenBIOS. Difieren:
  - Los VBlanks de los puntos a partir de `setupNameSelectorBox#2` (−90, +120, +120, +385
    respecto de OpenBIOS). Se conservan solo para diagnóstico y no se comparan (ver arriba).
  - Los 16 valores de `rand` (primeros: `11315, 12212, 7201, 23373`) y, en consecuencia, el
    frame capturado en `randomLimit#17` (`48322f1c…e508` contra `8557c88c…ba79`).

**Causa: semilla inicial del BIOS.** Las 16 salidas del BIOS real son consecutivas del mismo
LCG. Invirtiendo el LCG, la semilla queda exactamente **206 llamadas** antes de la primera
salida natural (la misma cantidad que con OpenBIOS, cuyas salidas son contiguas desde la
semilla 0 en la posición 206). Esa semilla es `0x24040001`. El bit 31 no influye en las
salidas, así que `0xA4040001` es equivalente. Con SCPH-1001 el juego no arranca con semilla 0:
la semilla 0 era correcta para OpenBIOS sin parche, pero no para el BIOS real.

**Decisión adoptada (2026-10-18):** semilla del BIOS real en el port y OpenBIOS
parcheado, sin depender del BIOS propietario al publicar. La identidad por defecto
del sello de estado es el SHA-256 de `/usr/local/share/pcsx-redux/openbios.bin`
(fallback `openbios` solo si falta el archivo). Así, reconstruir OpenBIOS invalida
las referencias viejas, incluso si la definición de la escena no cambió.

**Verificación con OpenBIOS parcheado (2026-10-18):** imagen local reconstruida, `openbios.bin`
SHA-256 `4ddea14132380f1aa6c303e45b417e1ead628d5c8c78ad678dcf2f9404832189`.

- `title`: `56132366…260d`, sin cambios.
- `newgame --state`: dos corridas idénticas byte a byte. Ignorando solo la columna de VBlank,
  la traza es idéntica a la de SCPH-1001 (`build/reference-owner/newgame/run-1.state`): 15
  variables en los 9 puntos y los 16 `rand` (`11315, 12212, 7201, 23373, …`). El frame también
  coincide: `48322f1c…e508`, igual que con el BIOS del dueño. Los VBlanks siguen difiriendo
  (arranque y tiempos de CD del BIOS real); no se comparan.
- Suites con disco: `native-headless` 49/49 y `native-asan` 47/47 sin omisiones;
  `ctest_no_skip.sh` con el filtro de `native-disc` 12/12 en ambos presets. `make compare` OK.

Logs locales (no versionados): `build/e3-reference-title.log`, `build/e3-reference-newgame.log`,
`build/e3-ctest-native-headless-disc.log`, `build/e3-ctest-native-asan-disc.log` y
`build/e3-ctest-*-noskip.log`. En CI, la imagen se publica con el job `images` de la rama de la
historia y su digest se fija en `.gitlab-ci.yml`.

Sin verificar con el BIOS real: el contenido de RAM[0] que usa la búsqueda de Jijimon
(`src-changes.md`, historia 3.3).

Sonda de `clearMapDigimon` en el original: ocho pares de breakpoints en
`800B64D0`/`800B64D4` observaron `v1=0` y RAM[0x34]=0 antes y después
de cada escritura (`build/reference/null-write-check/run-1.log`). Confirma la
traducción del acceso a RAM baja usada bajo `DW1_PORT`.

El despachador de eventos del SHA fijado (`src/core/eventslua.cc:createListener`)
conserva en su coroutine las tablas del registro y los resultados de cada callback.
Una captura larga agotó esa pila en el VBlank 21819 (`clearOverride` fue la primera
llamada C que lo detectó); desactivar JIT no cambió el fallo. El adaptador renueva
el listener después de 4096 VBlanks, desde el breakpoint de entrada a `VSync`,
cuando el callback anterior ya retornó. Usa `listener:remove()` y conserva el nuevo
handle; no modifica el emulador ni cambia el punto de muestreo. La prueba del
adaptador cruza 5000 eventos y verifica esa renovación; la prueba FFI atraviesa
30000 eventos reales y ocho listeners, emitiendo `ExecutionFlow::Run` a través
del mismo `createListener` para evitar emular 30000 frames. Invoca la renovación
entre eventos, como el breakpoint del juego. Las dos capturas largas renovadas llegaron al VBlank 29323 con 16 llamadas naturales
a `rand`, coincidentes con las dos corridas de los runners nativo y ASan.

Las pruebas sin disco incluyen un programa MIPS sintético que atraviesa los
breakpoints FFI reales de entrada/retorno de `rand` dieciséis veces, además del
test de callbacks del adaptador y la comprobación de los dieciséis bits de pad.

## Captura de audio de la historia 3.5

La implementación se detalla en [libsnd.md](libsnd.md) y la evidencia de paridad
en [audio-parity.md](audio-parity.md). El fallo histórico del sink SDL `disk`
(WAV diferentes con registros idénticos) motivó el tap PCM descrito abajo;
ese sink ya no genera la referencia. No hay un SNR del port aprobado.

```sh
DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B tools/port/reference/capture.py title_audio --audio --out /tmp/dw1-audio-reference/title_audio --max-vblanks 40000 --timeout 1800
DW1_DISC_DIR=/dw/game_rom/extracted DW1_AUDIO_DIR=/tmp/dw1-audio-port python3 -I -B tools/port/scenes/run.py title_audio --build native-headless --compare-audio
```

El primer comando exige dos capturas idénticas antes de guardar el sello; el
segundo devuelve 77 si falta el sello de referencia. Un sello existente con
escena obsoleta o archivos ausentes/alterados falla y exige recaptura.
`--reuse` solo reutiliza
una captura cuyo fingerprint siga vigente. `--out` o
`DW1_REFERENCE_DIR` eligen el directorio externo de referencia; el valor por
defecto para audio es `/tmp/dw1-audio-reference/title_audio`. `DW1_AUDIO_DIR`
elige la raíz externa del audio nativo (por defecto `/tmp/dw1-audio-port`).
Audio, trazas y archivos de trabajo quedan fuera del checkout, incluso si el
directorio configurado contiene un enlace simbólico hacia él.

### Gate vigente: decisión 4 del dueño, 2026-10-19

La decisión 4 sustituye el gate de la decisión 3. Se compara la lista ordenada
`(registro, valor)` de escrituras de voz (`registro < 0x180`), incluidas
las duplicadas. El tramo 1 sigue siendo completo:
`runLandingScreen` → `unloadNewGameScene`.

Para el tramo 2, `checkpoint_after = { SsSeqPlay = "gameLoop" }` ignora las
llamadas anteriores. `config/us/symbols.txt` fija `SsSeqPlay` en `0x800CD73C`;
`asm/us/main/psyq/libsnd.s` muestra su llamada a `Snd_SetPlayMode`.
La misma entrada arma ambos capturadores: ExecBP en Redux e instrumentación
de función en el port, antes del cuerpo. `seqPlay` llama previamente a
`SsSeqSetVol` (`asm/us/main/sound.s`, `0x800C5E30`).

`anchor = "note_on"` difiere el origen efectivo hasta una nota nueva. El
algoritmo compartido por `capture.lua` y `spu.c` es:

1. Desde la primera escritura de voz, acumular un flush, guardando la primera
   escritura por voz y las voces con escritura de dirección (`voz*0x10+6`).
2. En KON bajo (`0x188`) o alto (`0x18a`), intersectar sus bits no nulos con
   esas voces. La primera intersección confirma una nota nueva. El origen es
   la primera escritura del bundle de esa voz (la más temprana si hay varias),
   conservando pitch, volumen y ADSR anteriores al KON. Dirección cero también
   es una dirección válida. Volumen/pitch aislados nunca confirman el ancla.
3. Sin coincidencia al terminar KON alto, descartar el candidato. El buffer
   admite 256 escrituras; desbordarlo o cerrar sin nota confirmada falla.
   Después de confirmar, conservar todas las escrituras hasta el cierre.

Los ciclos y VBlanks se rebajan respecto de esa primera escritura, no respecto
del KON ni de `SsSeqPlay`. Redux selecciona PCM por índices absolutos del tap;
el port elimina del WAV provisional exactamente los frames anteriores al
nuevo origen, preservando la fase absoluta del mixer y su callback de salida.
Las pruebas sintéticas ejecutan ambos detectores con recálculos heredados,
KON cero, dirección/KON de voces distintas, voces bajas/altas y un bundle
que atraviesa una muestra; verifican el origen y el recorte del WAV nativo.

`stop_on` conserva las 21 entradas públicas de `libsnd` usadas por el juego.
La siguiente entrada cierra el tramo **antes** de ejecutar la llamada, incluso
si todavía no se confirmó el ancla. El tick usa rutinas internas (`snd_*`,
`vm_*`) y no cierra el tramo. Si la dirección también abre un tramo, se
procesa primero el cierre; Redux usa un solo breakpoint por dirección.
No se saltan llamadas del juego para obtener una ventana mayor.

La traza añade `# end <muestras> <motivo>`, con muestras
`floor((ciclo_fin − ciclo_ancla)/768)`. El tramo musical exige cierre
`event:<símbolo>` de `stop_on` y al menos **396900 muestras (9 s) en cada lado**.
Se comparan todas las escrituras de voz con muestra relativa en
`[0, min(fin_port, fin_referencia))`. No hay búsqueda de prefijos, reordenamiento,
ajuste de ganancia ni eliminación de duplicadas. El reporte muestra la ventana
común y cuántas escrituras quedan después de ella en cada lado. Una diferencia
dentro de la ventana informa el índice y los dos registros/valores, tiempos y
VBlanks; también falla una escritura faltante o adicional.

El TOML y la huella del adaptador invalidan la referencia anterior: exige
recaptura. Las dos pasadas de audio se ejecutan **en serie**, con un solo
emulador activo, y deben producir trazas y PCM idénticos byte a byte. Los
archivos siguen fuera del checkout. El reporte conserva regla B por VBlank,
desfases y SNR por ventanas de cinco segundos sobre los tramos capturados;
son diagnósticos no bloqueantes hasta 3.10. Se mantienen integridad de archivos,
límites de captura y detección de errores del proceso/sanitizadores.

Motivo: la decisión 3 empezaba con recálculos correctos de voces heredadas
(`0x078e`/`0x0143`) y exigía diez segundos donde el primer `SsUtKeyOnV` deja
9,277256/9,522290 s desde `SsSeqPlay`. El dueño aprobó el origen de nota nueva,
nueve segundos y la ventana común, conservando el cierre. La sincronización y
el estado heredado se difieren a 3.10. Resultados históricos y nueva evidencia
en [audio-parity.md](audio-parity.md).

`capture.lua` registra escrituras SH/SW a `0x1F801C00..0x1F801DFF`, tomando el
valor de `rt` tras decodificar la instrucción y verificar su dirección efectiva.
Usa `PCSX.getCPUCycles()` y reinicia las columnas de tiempo en cada checkpoint.
El modo audio no toma capturas de imagen. Requiere el parche versionado
`tools/port/reference/pcsx-redux-pcm-tap.patch`, aplicado por la etapa
`pcsx-redux` del Dockerfile sobre el SHA fijado. Usa el sink `dummy`,
`UseNullSync=true`, `Speed=1`, gaussiana (`Interp=2`) y `KeyOnDelay=6`.
`DW1_REDUX_PCM_TAP` activa la captura PCM16LE estéreo a 44,1 kHz antes de
`dropVoiceFrames`/`feedStreamData`; sin esa variable, el tap está desactivado.
El adaptador exige su cabecera y el mensaje de disponibilidad del emulador;
un binario anterior falla en vez de producir una referencia aceptada.

El archivo intermedio empieza por los ocho bytes `DW1PCM1\n`. Cada bloque
contiene un índice absoluto de muestra de 64 bits y un número de frames de
32 bits, ambos little-endian, seguidos de los frames estéreo PCM16LE. Los
índices permiten detectar huecos, solapamientos y reinicios del reloj dentro
del tramo. La conversión selecciona las muestras desde `inicio/768 + 1`
hasta `fin/768 + 1` (extremo final excluido, divisiones enteras), sin añadir
silencio, ajustar la ganancia ni resamplear. Se dejan cuatro VBlanks tras
cerrar el último tramo para que el mixer asíncrono drene su salida; quedan
fuera de los intervalos comparados.

### Integridad de la evidencia de audio (revisión 2026-10-19)

La comparación rechaza footers ausentes y causas de cierre distintas entre
port y referencia: `checkpoint` debe coincidir con `checkpoint`, y
`event:<nombre>` con el mismo evento. Un tramo sin música requiere su cierre
por checkpoint. Cada WAV válido debe contener los frames del intervalo de
su traza, con tolerancia de ±1 por redondeo; un prefijo PCM válido pero corto
también falla.

El fingerprint incluye las direcciones resueltas de `VSync`, el mark, todos
los checkpoints/anclas y `stop_on`, además de hashes de escena, adaptador,
comparador, parche PCM, BIOS y ejecutable Redux. El sello conserva la ruta de
la BIOS usada. Antes de ejecutar el port, el runner recalcula esa identidad
con la BIOS y el Redux instalados; cambios de símbolos, BIOS, emulador o
adaptador obligan a recapturar, aunque los archivos de evidencia sigan intactos.


## XA de OP1 (3.6)

`reference_xa_op1` y `scene_xa_op1_compare` añaden una ventana única del
inicio al retorno de `MOV_playMovie(0)`, con audio y todas las escrituras SPU.
`finish` igual a `start` expresa retorno de función únicamente en la escena XA.
Los símbolos del overlay se resuelven desde `symbols_mov.txt`, sin modificar
el mapa de símbolos principal. El parche de Redux incluye ahora DW1_REDUX_XA_TAP
antes de voldiv en FeedXA, además del tap del mezclador. Los ficheros crudos y WAV
se guardan fuera del repo; las dos capturas deben ser idénticas byte a byte.

El gate XA exige SNR >=40 dB del tramo completo tras correlación y registros/valores
SPU idénticos en su orden completo, sin recortar a la duración común de la música.
PCM mezclado, regla B y tiempos son diagnósticos. No se compara video MDEC.
Ver [xa.md](xa.md) para el modelo, atribución, hipótesis y límites.
La reconstrucción de la imagen usa `tools/port/docker_build.sh`; el job images
incluye cambios del parche entre sus triggers, pero publicar la imagen requiere
la autorización indicada en la especificación.
