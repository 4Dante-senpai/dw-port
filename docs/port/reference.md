# Emulador de referencia y captura automática

PCSX-Redux se fija en `2068784a552b598940191e8431585b7f96408b3f` (HEAD de `main` al implementar la
historia 3.1, 2026-10-17). OpenBIOS sale del submódulo `src/mips` de ese mismo commit
(`361f3f6a6f8d8bb98548900d8a231e2d2a2c1dc4`). En `tools/port/Dockerfile`, la etapa `pcsx-redux`
parte de la etapa `toolchain`, así que reutiliza su SDL3 (`/opt/sdl3/native`). A la imagen final
solo pasan el ejecutable (`/usr/local/libexec/pcsx-redux`, enlazado en `/usr/local/bin`),
`openbios.bin`, las licencias y `version.json`. La imagen final suma Xvfb y las bibliotecas
de runtime. El build termina con `xvfb-run -a pcsx-redux -version`.

La referencia con **OpenBIOS es provisional** hasta compararla con el BIOS real del dueño
(`docs/implementation-artifacts/deferred-work.md`). No hay BIOS de Sony ni datos del disco en la
imagen ni en el repositorio. PCSX-Redux es GPL-2.0-or-later y se distribuye como ejecutable
separado del port; OpenBIOS es MIT.

## Uso

Dentro de la imagen `dw-port`:

```sh
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py title
# Fijar o reemplazar el hash; solo con dos corridas idénticas:
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py title --update
python3 -I -B tools/port/reference/test_capture.py
```

Cada corrida dura unos 2-3 minutos con el intérprete. Sin `.cue`, emulador o Xvfb, el script
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

`capture.py <id> --state` genera dos `run-N.state`, con lecturas después del despacho
de cada VBlank y antes de volver al juego en modo legado; con `checkpoints`, en cada
entrada a función y en los offsets 1..`window` posteriores. Las
declaraciones y el formato están en `scenes.md`. Los símbolos globales y sus tamaños
salen de `config/us/symbols.txt`; cuando falta tamaño se usa la siguiente dirección
distinta. NULL se escribe `0`; globales `símbolo+offset`; el resto de RAM se normaliza
a `0x80000000 | offset`. El breakpoint en `rand` (`0x8009127c`) registra cada salida
al alcanzar su `ra`, junto al VBlank relativo a la marca.

OpenBIOS inicializa `s_currentSeed` a **0** (`src/mips/openbios/kernel/libcmisc.c`,
submódulo nugget `361f3f6a…`). Una sonda aislada al primer `VSync` llamó al `rand`
original 16 veces y observó:
`0, 21468, 9988, 22117, 3498, 16927, 16045, 19741, 12122, 8410, 12261, 27052,
5659, 9758, 21087, 25875` (`build/reference/seed-probe/run.log`). Son las salidas del
LCG `seed = seed * 0x41c64e6d + 0x3039`, `(seed >> 16) & 0x7fff`, con aritmética de
32 bits. El port adopta esa semilla; sigue siendo provisional respecto al BIOS del
dueño. La sonda cambia el PC solo en el emulador de referencia: no sustituye la
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
la hipótesis `BIOS_RAND_SEED 0u` (`port/psyq/libc.c:9`) es correcta para OpenBIOS, pero no para
el BIOS real.

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
