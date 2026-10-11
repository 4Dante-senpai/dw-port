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

Los demás presets de AD-17 (`native`, `native-asan`, `web`) se agregan en la historia que los usa.

## Qué compila

- **TUs del Juego:** la lista sale de `tools/port/units/tu_list.py us` (de `mk/version/us.mk` y
  `config/us/*.yaml`) y queda en `build/port/<preset>/tu_list_us.tsv` con la unidad (`main` u
  overlay) y el estado (`build`, o `replaced` para `src/main/_psstart.c`, el crt de CodeWarrior,
  que reemplaza `port/psyq/crt.c`).
- **Headers de texto:** `text/<dir>/<name>.yaml` → `generated/text/<dir>/<name>.h` con
  `tools/gen_text.py`, como `mk/text.mk`.
- **Include path del Juego**, en orden: `port/psyq/include` (reemplaza `include/gcc/inline_n.h`),
  `external/psyq_headers/mw_lib41/include`, `include`, `generated`.
- **Flags del Juego** (AD-17): `-std=gnu99 -fsigned-char -fwrapv -fno-strict-aliasing -fno-common
  -DDW1_PORT -DLANGUAGE_C -DVERSION_US`, sin warnings (`-w`), con `port/psyq/include/port_game.h`
  incluido a la fuerza (asserts de `char` con signo y punteros de 32 bits). El código del port compila con
  `-Wall -Wextra -Werror`.

## Tests y métricas

```sh
DOCKER_CONTEXT=default tools/port/docker_run.sh ctest --test-dir build/port/native-headless
DOCKER_CONTEXT=default tools/port/docker_run.sh python3 -I tools/port/metrics/char_sign_diff.py
```
