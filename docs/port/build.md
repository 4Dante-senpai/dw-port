# Build del port

El build del port usa CMake + Ninja dentro de la imagen `dw-port` (`docs/port/setup.md`). El build
PS1 matching sigue siendo el `Makefile` de upstream.

## Compilar

```sh
DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c \
  'cmake --preset native-headless && cmake --build --preset native-headless'
```

Presets (`CMakePresets.json`):

| Preset | Qué es | Directorio |
|---|---|---|
| `native-headless` | i686 (`-m32`), sin SDL | `build/port/native-headless` |
| `native-asan` | `native-headless` con ASan + UBSan, runtime de ASan compartido, sin `-fsanitize=function` (`detect_stack_use_after_return=0`) | `build/port/native-asan` |

Los demás presets de AD-17 (`native`, `web`) se agregan en la historia que los usa. Tests:
`ctest --preset <preset>`.

## Qué compila

- **TUs del Juego:** la lista sale de `tools/port/units/tu_list.py us` (de `mk/version/us.mk` y
  `config/us/*.yaml`) y queda en `build/port/<preset>/tu_list_us.tsv` con la unidad (`main` u
  overlay) y el estado (`build`, o `replaced` para `src/main/_psstart.c`, el crt de CodeWarrior,
  que reemplaza `port/psyq/crt.c`).
- **Overlays (AD-6):** cada TU de un overlay se compila a través de un envoltorio generado en
  `build/port/<preset>/ovl/` que pone todas sus variables globales en la sección
  `dw1ovl_<overlay>` (y define `const` vacío para que ninguna la vuelva de solo lectura).
  `port/psyq/overlay.c` copia cada sección al arrancar y la restaura en `port_overlay_loaded`.
- **Memoria (AD-4, AD-5):** `port/psyq/ps1mem.h` (arena, scratchpad, `PS1_RAM`, `PS1_LINK`). Los
  ejecutables nativos se linkean con `-no-pie`; la ventana de OT de 16 MB empieza en el segmento
  de datos (`__data_start`), así globals, arena y stacks estáticos caen dentro.
- **Headers de texto:** `text/<dir>/<name>.yaml` → `generated/text/<dir>/<name>.h` con
  `tools/gen_text.py`, como `mk/text.mk`.
- **Include path del Juego**, en orden: `port/psyq/include` (reemplaza `include/gcc/inline_n.h`),
  `external/psyq_headers/mw_lib41/include`, `include`, `generated`.
- **Flags del Juego** (AD-17): `-std=gnu99 -fsigned-char -fwrapv -fno-strict-aliasing -fno-common
  -DDW1_PORT -DLANGUAGE_C -DVERSION_US`, sin warnings (`-w`), con `port/psyq/include/port_game.h`
  incluido a la fuerza (asserts de `char` con signo y punteros de 32 bits). El código del port compila con
  `-Wall -Wextra -Werror`.

## Correr el Juego

```sh
build/port/native-headless/dw1 --headless --frames 1   # sale con 0 al terminar el frame 0
```

- Sin Pack todavía: las funciones PsyQ que faltan son stubs generados en el build
  (`tools/port/psyq/gen_stubs.py`), que registran su primera llamada en `$DW1_TRACE`
  (`port_trace.log` por defecto) y devuelven 0 o el valor de `tools/port/psyq/stub_returns.tsv`.
  El build imprime la cobertura (`psyq: used=… implemented=… host_libc=… stubs=…`).
- Si el Juego no termina un frame en 30 s virtuales, sale con 3 (watchdog).
- El `main` del Juego se compila como `dw1_game_main`; el punto de entrada es el Host
  (`port/host/headless/main.c`), que arranca el hilo del juego (`port/psyq/crt.c`).

## Tests y métricas

```sh
DOCKER_CONTEXT=default tools/port/docker_run.sh ctest --test-dir build/port/native-headless
DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I tools/port/metrics/char_sign_diff.py
```

## L0a: preprocesado igual a upstream e inventarios

```sh
DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh
```

- Preprocesa cada TU USA (sin `DW1_PORT`, sin marcas de línea ni líneas en blanco) en el árbol
  actual y en el merge-base con `upstream/main`, en las dos configuraciones de upstream (`gcc`, y
  `mwcc` con `__MWERKS__` e `include/mwcc`), y falla nombrando el archivo que difiere (NFR-6).
  Requiere la ref `upstream/main` (`git fetch upstream`).
- Falla también si cambia la lista de TUs USA, si `config/`, `mk/` o el `Makefile` difieren de
  upstream, o si cambió un `.c` que no es TU USA (L0a no podría verificarlo).
- Corre `ps1_literals.py --check` e `isr_spins.py --check`: fallan si los TSV versionados de
  `tools/port/metrics/` no coinciden con lo que genera el código actual.
- Inventarios:
  - `ps1_literals.tsv` (AD-4): literales `0x80000000`–`0x801FFFFF` en los TUs USA e `include/`.
    Estados `traducido`, `no aplica: <motivo>` o `pendiente`.
  - `isr_spins.tsv` (AD-8): loops sin ninguna llamada, con la función que los contiene en la
    columna `kind`. Estados `port_yield` o `no aplica: <motivo>`; L0a exige cero `pendiente`.
    Límites conocidos en el docstring de `isr_spins.py`.
  - Al regenerar, el script avisa si un estado asignado a mano se pierde porque su fila cambió.
  - Para regenerar, correr el script sin `--check`; los estados de filas existentes se conservan y
    las nuevas quedan `pendiente`. `--no-pending` falla si queda alguna.
