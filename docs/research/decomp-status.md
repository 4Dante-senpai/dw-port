# Estado de la descompilación (USA, SLUS_010.32)

Medido el 2026-10-11 sobre `upstream/main` @ `09c174c` con
`tools/port/metrics/decomp_status.py us`, después de `make regenerate`.

## Método

Modo sin compilador matching: no hay `make objdiff` ni reporte de objdiff. El conteo sale de la
salida de splat:

- Cada subsegmento de código de `config/us/*.yaml` es `c` (compilado desde `src/`) o `asm`
  (ensamblado desde `asm/us/`).
- Para los subsegmentos `c`, splat escribe el desensamblado original de cada función en
  `asm/us/<unidad>/matchings/<archivo>/<función>.s` con su tamaño original
  (`nonmatching NAME, 0xSIZE`). Una función cuenta como "en C" si además está definida en el
  `.c` correspondiente (no basta un prototipo).
- Los subsegmentos `asm` cuentan sus `glabel`.

En `src/` no queda ningún `INCLUDE_ASM` (0 ocurrencias): upstream no usa ese mecanismo; lo
pendiente se expresa como subsegmentos `asm` en los YAML.

## Resultado

| Unidad | Código | Funciones en C | Bytes en C | Funciones en asm | Bytes en asm | % C (bytes) |
|---|---|---:|---:|---:|---:|---:|
| btl | game | 388 | 114872 | 0 | 0 | 100.00 |
| dget | game | 13 | 5260 | 0 | 0 | 100.00 |
| doo2 | game | 18 | 6720 | 0 | 0 | 100.00 |
| dooa | game | 28 | 18580 | 0 | 0 | 100.00 |
| eab | game | 24 | 5788 | 0 | 0 | 100.00 |
| endi | game | 16 | 3416 | 0 | 0 | 100.00 |
| evl | game | 31 | 16040 | 0 | 0 | 100.00 |
| fish | game | 110 | 41248 | 0 | 0 | 100.00 |
| kar | game | 55 | 29392 | 0 | 0 | 100.00 |
| main | game | 987 | 330812 | 0 | 0 | 100.00 |
| main | psyq | 0 | 0 | 612 | 144348 | 0.00 |
| mov | game | 8 | 2316 | 0 | 0 | 100.00 |
| murd | game | 15 | 5896 | 0 | 0 | 100.00 |
| shop | game | 65 | 24084 | 0 | 0 | 100.00 |
| std | game | 456 | 142404 | 0 | 0 | 100.00 |
| trn | game | 45 | 26392 | 0 | 0 | 100.00 |
| trn2 | game | 40 | 20720 | 0 | 0 | 100.00 |
| vs | game | 442 | 137860 | 0 | 0 | 100.00 |
| **total** | game | 2741 | 931800 | 0 | 0 | 100.00 |
| **total** | psyq | 0 | 0 | 612 | 144348 | 0.00 |

Unidades: `main` = ejecutable `SLUS_010.32`; el resto son overlays `*_REL.BIN`. `vs` incluye
`vs_intro`, `vs_combat` y `vs_select`, que en USA están linkeados dentro de `main`
(`config/us/main.yaml`, líneas 125-127); `shop` (`script_menu`) también está dentro de `main`.

## Lectura

- **Código del juego: 2741 funciones, 931.800 bytes, 100 % en C.** No hay funciones del juego
  pendientes de descompilar en la versión USA.
- **Lo único en asm son las librerías PsyQ** (612 funciones, 144.348 bytes): `libapi`, `libc`,
  `libetc`, `libgpu`, `libgs`, `libgte`, `libcd`, `libspu`, `libsnd`, `libpress`, `libds`,
  `libmcrd`. El port no las compila: las reemplaza por `port/psyq/` (ver `psyq-usage.md`).
- Upstream valida `make compare` en CI para las 6 versiones en cada push
  (`.github/workflows/build.yaml`), así que el C de USA es matching según upstream. Aquí no se pudo
  verificar localmente (sin `cc_mips.dll`).
- Verificación adicional sin CodeWarrior: el build no-matching de upstream con GCC
  (`make TOOLCHAIN=gcc`, `mipsel-linux-gnu-gcc`, `-Werror` no aplica en ese toolchain) compila y
  linkea **todo** el juego: `build/us/psx/gcc/SLUS_010.32` (614.400 bytes) más los 15
  `*_REL.BIN`. Es la primera evidencia de que el C compila con un compilador distinto de
  CodeWarrior.
- Asm a mano dentro de C: solo `src/main/_psstart.c` (`__start`, crt0), que el port reemplaza
  por su propio arranque.

## Consecuencia para el port

La estrategia "híbrida con `port/recomp/`" (traducción mecánica MIPS→C) no es necesaria para el
código del juego en USA. La épica continua de descompilación se reduce a: seguir `upstream/main`,
mejorar nombres/tipos donde el port lo necesite, y documentar en `docs/port/non-matching.md`
cualquier función que el port reescriba.
