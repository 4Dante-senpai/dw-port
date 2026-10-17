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
- **Inputs**: `{ vblank = k, buttons = [bits 0..15] }`, con `k` contado desde el frame 0 y
  estrictamente creciente. Cada entrada reemplaza el estado anterior. El estado se aplica en cada
  latch de VBlank con `PCSX.SIO0.slots[1].pads[1].setOverride/clearOverride`. Con `inputs = []`
  no se presiona nada; los inputs no vacíos se validan en la historia 3.3.

Para el determinismo, la corrida usa intérprete, GPU software, RAM retail, SPU `dummy` con
NullSync, `Speed=0` y configuración portable propia. La conversión a P6 es la misma de
`gpu_read_display`: RGB555 little-endian sin el bit 15, cada canal `(c << 3) | (c >> 2)`;
RGB24 pasa tal cual. Se rechazan las capturas de 16 colores o menos. Si dos corridas dan hashes
distintos, el script falla, incluso con `--update`. Si un hash fijado no coincide, también falla
y no toca el `.toml`.

No se captura audio (historia 3.5) ni RAM (3.3), y la comparación contra el port queda para 3.2.

## Escena `title` (OpenBIOS)

| Dato | Valor |
|---|---|
| Frame 0 | VBlank 822 desde el arranque |
| Marca `runLandingScreen` | VBlank 8642 |
| Captura | VBlank 8702, 320×240, 256 colores |
| SHA-256 (2 corridas) | `561323665a33d833c89339030f6a8c60e4a395add2cff467d1d47e92dd46260d` |

Dato para la historia 3.2: la captura del port (`native-headless`) tiene las mismas dimensiones y
la misma cantidad de colores, pero difiere en 10.674 píxeles, todos dentro de la caja x 1..128,
y 0..217 (los rayos animados del lado izquierdo).

## APIs Lua verificadas (SHA fijado)

`PCSX.addBreakpoint(addr, 'Exec', 4, cause, fn)` (los wrappers deben retenerse),
`PCSX.getRegisters().GPR.n.ra`, `PCSX.Events.createEventListener('GPU::Vsync', fn)` (despacha
entre bloques de CPU), `PCSX.GPU.takeScreenShot()` (bpp 0 = 16, 1 = 24), `tostring(shot.data)`,
`PCSX.SIO0.slots[1].pads[1].setOverride/clearOverride(bit)` y `PCSX.quit(code)`. Todas se
probaron en ejecución real bajo Xvfb, salvo los overrides con botones durante el juego (historia
3.3).

## Repetición con BIOS del dueño

Guardar el BIOS fuera del repositorio y correr:

```sh
DW1_DISC_DIR=/private/disc python3 -I -B tools/port/reference/capture.py title \
  --bios /private/bios/owner.bin --out build/reference-owner/title
```

El script hace dos corridas e imprime el hash junto al de OpenBIOS. No escribe `title.toml`
(`--bios --update` se rechaza). El resultado se registra acá: si coinciden o qué difiere.

Resultado: **pendiente** (lo corre el dueño).
