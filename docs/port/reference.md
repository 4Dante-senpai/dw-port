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

## STR y libpress (3.7): evidencia y límites

**Nota de vigencia:** los ensayos y matrices que siguen hasta la sección de
decisiones 5/6 describen ejecuciones históricas, incluido el modelo por pasada
de d57a330. El dueño ya corrigió OP1 a 3636 en el bloque congelado. Los
resultados actuales y sus fallos residuales se registran al final.


Las escenas `movie0` y `movie0_skip` capturan todos los `PutDispEnv` de
`MOV_playMovie`, sin seleccionar un subconjunto de frames. Véase
[mdec.md](mdec.md) para el modelo, atribución y gates. En la continuación
autorizada por el dueño, frames, diffs, trazas y PCM se guardan bajo
`build/evidence/`, ignorado por Git, para persistir entre contenedores.
`build/reference/<escena>/audio.json` conserva hashes e identidad de las dos
ejecuciones de Redux; `build/port/<preset>/scenes/<escena>/audio.json` conserva
las métricas de comparación y hashes. El gate visual usa el máximo absoluto
y la media de cada canal, sin alinear ni desplazar frames.

La ejecución local del 2026-10-21 detecta una contradicción con el requisito
de 3637 frames: **las dos capturas del juego USA original presentan 3636**,
con retorno 1 y evidencia idéntica byte a byte. El port también presenta
3636. `MOV_readFrame` marca `MOVIE_COMPLETED` al prefetchear el frame con
contador 3637; el bucle muestra el frame anterior y termina. No se añade
un frame artificial ni se modifica el consumidor para satisfacer el número.
El gate de 3637 se conserva y falla explícitamente.

Con Start relativo en 120 (mantenido hasta 180), Redux retorna 0 en el
VBlank 122; el port retorna 0 en el 121. El desfase temporal queda abierto;
en esa primera implementación no se ajustaron costes de CPU, CD ni MDEC.
La historia no se considera cerrada mientras fallen estos gates.

En la implementación anterior, con la IDCT que conserva la fracción entre pasadas, los 3636 frames comparados
cumplen la media ≤0,5 por canal. Quedan cuatro frames (2318, 2587, 3439 y
3453), cada uno con un único componente azul que difiere 5/255; el máximo
permitido es 4/255. No se relaja ese límite. El peor diff queda fuera del
repo. El desfase A/V difiere 87 181 ciclos, dentro del intervalo medio de
1 128 796,81 ciclos; XA obtiene 54,42 dB (mínimo: 40). Ambos retornos
completos son 1. Las dos ejecuciones nativas repiten píxeles, tiempos y audio.

Verificación local de la implementación anterior: builds `native-headless`, `native-asan` y `web`; L1
`mdec`, `libpress`, `mdec_vectors`, carga de tablas, consumidor y runner en
nativo y sanitizers, y los mismos vectores bajo Node/WASM. `make compare`
produce «Compiled binaries match with target binaries». No se ejecuta
ningún commit, push ni publicación. Los gates STR anteriores permanecen rojos.

El PCM mezclado sigue siendo diagnóstico según la decisión 4: no cumple
40 dB y su duración difiere, aunque el flujo XA aislado sí pasa. Estos
resultados no demuestran paridad del mixer completo.

Antes de la revisión del redondeo documentada más abajo, los hashes de frames, registros SPU y PCM, y el footer temporal eran también
idénticos entre `native-headless` y `native-asan` para OP1 y Start. Los
fixtures `reference_movie0` y `reference_movie0_skip` pasaron con
comprobación de hashes e identidad del adaptador actual.

### Investigación de la cuenta OP1 (2026-10-21)

El original USA **no muestra el frame STR 3637** durante la reproducción
completa de OP1. Dos sondas de Redux, a Speed=1, registran idénticos bytes
y ciclos en `build/evidence/reference/movie0/headers-{1,2}.jsonl`:
lecturas StHEADER 1..3637, entradas MDEC 1..3636 y presentaciones 1..3636,
sin huecos ni repeticiones. Primera presentación: header 1, ciclo 754 010 018.

| Evento terminal | Header STR | Ciclo Redux | `MOVIE_COMPLETED` |
|---|---:|---:|---:|
| Presenta | 3635 | 4 856 057 611 | 0 |
| Envía al MDEC | 3636 | 4 856 069 147 | 0 |
| Lee el siguiente StHEADER | 3637 | 4 856 956 789 | 0 |
| Ejecuta el store de finalización | 3637 | 4 856 956 797 | 0 antes del store |
| Presenta por última vez | 3636 | 4 857 186 405 | 1 |
| Epílogo de `MOV_playMovie` | 3636 | 4 858 416 473 | 1 |

El retorno al llamador ocurre en 4 858 416 486, valor 1. `MOV_readFrame`
compara `header->frameCount >= movie->frameCount` (`src/mov/mov.c:196`);
el binario original hace `lw` del header en `0x80010528`, compara en
`0x80010534` y escribe la bandera en `0x80010544`. La tabla original en
`0x8001094c` contiene inicio 1 y límite 3637. `MOV_playMovie` envía el buffer
actual al MDEC (`0x80010748`), expande el siguiente frame (`0x80010800`),
presenta el actual (`0x8001089c`) y recién entonces consulta la bandera
(`0x800108b4..0x800108bc`). El frame 3637 se expande por VLC, pero nunca
se envía al MDEC ni se presenta. El C conserva ese mismo orden.

El archivo contiene **3719 headers de frame distintos, consecutivos 1..3719**,
todos con sus chunks completos. El 3637 sí existe: que no se muestre no
proviene de un archivo truncado. El STR extraído tiene SHA-256
`a172fa0eb6ec5ecf703d43275bbc99ee20f0070a36b2b9974ba57a85d54288a5`,
igual al manifest del disco. Evidencia adicional en
`build/verification-3-7-gates/{str-headers,str-selected-headers,original-movie-table,frame-count}.json`
y `MOV-original.asm`. La sonda se conserva en ese directorio y no modifica
el adaptador versionado ni el código original. La recaptura fue necesaria
porque se perdieron los archivos de `/tmp`; los 3636 hashes de referencia
de cada ejecución y el footer temporal coinciden con los registrados
anteriormente (`reference-preservation.json`).

No se cambia el consumidor: no hay un bug del port en esta cuenta. El
requisito congelado de 3637 presentaciones continúa fallando explícitamente.

### Start: causa del retorno en 121 frente a 122

Dos ejecuciones de cada sonda producen trazas idénticas. Se conservan en
`build/verification-3-7-gates/skip-timing-port-{1,2}.json` y
`build/evidence/reference/movie0_skip/timing-{1,2}.jsonl`. La sonda Redux
observa instrucciones del asm USA; no usa código fuente del emulador.

| Evento | Port: ciclo / VBlank / iteración | Original: ciclo / VBlank / iteración |
|---|---|---|
| Primer `DecDCTin` | 114 034 187 / 9 / 1 | 752 416 057 / 29 / 1 |
| Primera presentación | 115 269 201 / 12 / 1 | 754 010 018 / 32 / 1 |
| Última espera de flip: entrada | 176 858 689 / 120 / 56 | 803 214 070 / 119 / 45 |
| Última espera de flip: salida | 176 858 689 / 120 / 56 | 803 214 099 / 119 / 45 |
| `PadRead` devuelve Start (0x800) | 176 859 242 / 121 / 56 | 803 688 388 / 120 / 45 |
| `CdControlB(CdlPause)`: entrada | 176 859 370 / 121 / 56 | 803 689 630 / 120 / 45 |
| `CdControlB(CdlPause)`: salida | 176 859 434 / 121 / 56 | 804 930 242 / 122 / 45 |
| Retorno al llamador, valor 0 | 177 089 898 / 121 / 56 | 804 931 349 / 122 / 45 |

La aplicación del input no explica por sí sola el retorno: **el original
ya ve Start en el VBlank 120**. A continuación bloquea **1 240 612 ciclos**
en `CdControlB`, cruzando los VBlanks 121 y 122. El port ejecuta `CdlPause`
sin latencia de comando y consume sólo **64 ciclos**. Esa hipótesis previa
está explícita en `port/psyq/libcd.c`: `CdControl/B/F` y `CdSync` completan
inmediatamente. En el original se observan los callsites `0x800108fc` y
`0x80010904`; `ClearImage` vuelve en 804 931 078 y `DrawSync` en 804 931 323.
La diferencia posterior a `PadRead` está delimitada a la espera CD.

También difiere la fase del bucle: antes del primer MDEC ya hay una ventaja
de 20 VBlanks en el port. Al llegar Start, el port está en la iteración 56
y el original en la 45; el último `PadRead` del port ocurre un VBlank más
tarde. Los tiempos de CPU/CD del arranque y de la expansión VLC no están
calibrados. La traza no atribuye cada ciclo de esa divergencia inicial a
un componente; sí demuestra que precede al primer MDEC. En la última
iteración del port, el MDEC termina en 175 955 777 (VBlank 119), mientras
`MOV_decodeFrame` termina en 176 858 689 (120). La espera de flip consume
cero ciclos. Aumentar el coste MDEC no modelaría la espera de pausa ausente.

Se mantiene `CLOCK_MDEC_MACROBLOCK_CYCLES=1024` como hipótesis sin calibrar
para 3.10. No se ajusta ningún coste CD/CPU para forzar el resultado. El
gate Start **sigue fallando 121 frente a 122**: encontrar la causa no
constituye corregir la temporización de comandos CD.

### Ensayo acotado de color y evidencia persistente

La tabla antes/después por canal de los cuatro frames y el ensayo de seis
variantes están en [mdec.md](mdec.md). Se conserva el modelo psx-spx con
redondeo por pasada. El piso de color elimina los máximos de 5 en esa
selección, pero incumple la media azul de 2318 (0,5068229166666667 > 0,5).
Ninguna variante ensayada satisface ambos límites; no se relajan.

El guard del host y el runner permiten evidencia bajo el árbol ignorado
`build/`, verificando rutas resueltas. Siguen rechazando rutas a fuentes,
incluidos alias y symlinks desde `build` hacia `src`. El test
`scene_audio_runtime` verifica aceptación en `build`, aceptación mediante
alias hacia ese árbol y rechazo de fuentes; pasó en nativo y ASan.
La primera ejecución completa se detuvo por la discrepancia de guards;
se conserva como fallo de infraestructura previo en
`build/verification-3-7-gates/scene_movie0_compare-native-pathguard-failure.log`.

Las referencias existentes de título y estado newgame se reutilizan desde
`build/evidence/reference/`. Las capturas de audio de título y XA que estaban en `/tmp`
ya no existían: se recapturaron y validaron en `build`, dos ejecuciones por
escena. No se recapturó por el cambio de redondeo. La especificación completa conserva su SHA-256
`472669cf9557c9aebb87d9ed146427c5874af25dea890f3a5fef0ef03e3029f2`.

### Comparación completa con redondeo por pasada

La comparación nativa completa descubre **26 frames y 30 componentes** fuera
del máximo de 4/255: 29 azules y uno rojo. El peor error es **6/255** en el
frame 2142; los demás componentes listados difieren 5/255. Las medias por
canal permanecen dentro de 0,5. La selección rápida no predijo este resultado:
aunque dos de sus cuatro frames mejoran, el resultado completo empeora
respecto de los cuatro frames a 5/255 de la IDCT anterior. Se conserva el
modelo por pasada ya incluido en el WIP, después del único intento adicional
autorizado; no se presenta como una mejora global ni se continúa ajustando.

Lista completa de residuos, coordenadas desde cero y valores enteros 0..255:

| Frame | Canal | Píxel (x,y) | Port | Referencia | Error |
|---|---|---|---:|---:|---:|
| 478 | B | (224,48) | 115 | 110 | 5 |
| 510 | B | (128,176) | 82 | 77 | 5 |
| 1873 | B | (144,112) | 53 | 48 | 5 |
| 1881 | B | (192,64) | 97 | 92 | 5 |
| 1881 | B | (144,112) | 51 | 46 | 5 |
| 1883 | B | (144,112) | 53 | 48 | 5 |
| 1886 | B | (144,112) | 53 | 48 | 5 |
| 1889 | B | (144,112) | 53 | 48 | 5 |
| 2076 | B | (208,128) | 158 | 153 | 5 |
| 2142 | B | (240,128) | 101 | 95 | 6 |
| 2142 | B | (240,129) | 101 | 96 | 5 |
| 2294 | B | (160,96) | 169 | 164 | 5 |
| 2318 | B | (144,80) | 181 | 176 | 5 |
| 2351 | R | (176,80) | 229 | 224 | 5 |
| 2577 | B | (64,152) | 33 | 28 | 5 |
| 2613 | B | (176,96) | 48 | 43 | 5 |
| 2617 | B | (16,112) | 92 | 87 | 5 |
| 2691 | B | (240,32) | 23 | 18 | 5 |
| 2766 | B | (192,112) | 72 | 67 | 5 |
| 2805 | B | (145,160) | 95 | 90 | 5 |
| 2987 | B | (16,176) | 75 | 70 | 5 |
| 3204 | B | (161,144) | 6 | 1 | 5 |
| 3204 | B | (161,145) | 7 | 2 | 5 |
| 3439 | B | (176,96) | 24 | 19 | 5 |
| 3445 | B | (176,142) | 46 | 41 | 5 |
| 3452 | B | (128,112) | 10 | 5 | 5 |
| 3467 | B | (176,96) | 16 | 11 | 5 |
| 3467 | B | (96,112) | 120 | 115 | 5 |
| 3476 | B | (128,112) | 45 | 40 | 5 |
| 3489 | B | (176,144) | 76 | 71 | 5 |

Evidencia estructurada: `build/verification-3-7-gates/full-residual-pixels.json`.
El diff de peor frame es `build/evidence/port/native-headless/movie0/diff/worst-frame-2142.ppm`.
La cuenta permanece en 3636/3636 frente al requisito congelado de 3637.
A/V conserva error de 87 181 ciclos frente a 1 128 796,805; XA obtiene
54,424193 dB frente a 40. El retorno completo es 1 en ambos lados.

### Verificación final secuencial (2026-10-21)

Se compilaron las tres variantes con la observación opcional de tiempos.
Todas las pruebas pesadas se ejecutaron de una en una, con capturas, work
dirs y temporales bajo `/dw/build`. Referencias reutilizadas: OP1 completo,
Start, título y estado inicial; las de audio de título y XA se recuperaron
por pérdida de sus archivos externos anteriores.

| Prueba / preset | Resultado | Segundos |
|---|---|---:|
| `L1-native-headless` | PASS 51/51 | 198.19 |
| `L1-native-asan` | PASS 49/49 | 209.29 |
| `L1-web` | PASS 34/34 | 531.74 |
| `reference_movie0-native-headless` | PASS | 13.44 |
| `reference_movie0_skip-native-headless` | PASS | 0.31 |
| `scene_movie0_compare-native-headless` | FAIL | 202.96 |
| `scene_movie0_skip_compare-native-headless` | FAIL | 2.66 |
| `scene_movie0_compare-native-asan` | FAIL | 587.08 |
| `scene_movie0_skip_compare-native-asan` | FAIL | 9.19 |
| `scene_title_compare-native-headless` | PASS | 99.84 |
| `reference_newgame_state-native-headless` | PASS | 0.18 |
| `scene_newgame_state-native-headless` | PASS | 311.30 |
| `reference_title_audio-native-headless` | PASS | 811.09 |
| `scene_title_audio_compare-native-headless` | PASS | 216.03 |
| `reference_xa_op1-native-headless` | PASS | 390.07 |
| `scene_xa_op1_compare-native-headless` | PASS | 109.24 |

Total: **147 pruebas CTest, 143 aprobadas y cuatro fallidas**. Las cuatro
fallidas son las comparaciones OP1 y Start en nativo y ASan; no hay fallos
de sanitizer. No quedan pruebas solicitadas pendientes. Los fallos visuales,
la cuenta congelada y la temporización de Start mantienen abierta la historia.
`make compare` vuelve a producir **Compiled binaries match with target binaries**.

La comparación completa independiente obtiene máximos RGB **5/4/6**, medias
globales **0,2546425716790429 / 0,2006998375618812 / 0,2903838388396131**,
y mayor media de frame por canal **0,3861328125 / 0,2901692708333333 /
0,46954427083333333**. No hay frames fuera de la media de 0,5. Los 30
componentes fuera del máximo se enumeran arriba. Los cuatro hashes del
harness rápido coinciden con los de la captura completa. Los informes de
OP1 y Start, incluidos todos sus hashes y tiempos, son idénticos entre
nativo y ASan. Cada runner verificó además dos ejecuciones deterministas.

A/V OP1: error **87 181 ≤ 1 128 796,805227 ciclos**; XA aislado:
**54,42419314227058 dB ≥ 40**, lag cero y sin duración distinta. En Start,
A/V pasa **1 003 094 ≤ 1 128 792,227273 ciclos**; su prefijo XA no contiene
una ventana de 5 s y sigue siendo diagnóstico. El PCM mezclado no pasa
40 dB: OP1/XA da −inf y duración distinta; título da −3,6973754407027624 y
8,492502978433144 dB. Conforme a la decisión 4, esas métricas no bloquean
los tests de contenido y no acreditan paridad del mixer.

No se cambió la especificación ni sus tolerancias ni se creó ningún commit.
No hay cambios adicionales en `src/`; los observadores del port no avanzan
el reloj virtual. La IDCT conservada ya estaba en el WIP y no se modifica
tras el ensayo acotado. El coste MDEC de 1024 ciclos sigue como hipótesis
para 3.10; no se ajusta para ocultar la espera CD ausente.

Informe y nombres/resultados individuales, incluidos los 134 casos L1:
[`report.md`](../../build/verification-3-7/report.md) y
[`summary.json`](../../build/verification-3-7/summary.json). Logs por trabajo:
`build/verification-3-7/final-<trabajo>.log`. Evidencia adicional bajo
`build/verification-3-7-gates/`: `full-channel-metrics.json`,
`full-before-after.json`, `full-residual-pixels.json`, `final-crosschecks.json`,
`matching-compare-final.log` y `build-{native,asan,web}-timing.log`.

La excepción histórica de `reference_newgame_state` a los 177,92 s no fue
un fallo ASan: el agente había enviado SIGTERM a una captura duplicada que
compartía directorio de trabajo. No se alcanzó el timeout ni hubo diagnóstico
del sanitizer. La recaptura secuencial anterior y sus comparaciones pasaron;
en esta batería se reutilizó esa referencia validada. Registro conservado en
`build/verification-3-7/asan-interruption.txt` e informe anterior archivado.

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
