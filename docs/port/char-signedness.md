# Signedness de `char` en el build del port

Fecha: 2026-10-11. Historia 2.1. Verifica el supuesto de AD-17.

## Resultado

**`char` es con signo** en el binario original. El build del port compila el Juego con
`-fsigned-char` (el supuesto inicial de AD-17, `-funsigned-char`, era incorrecto).

## Evidencia

- `isAsciiEncoded(const char *value)` (`src/main/sjis.c`) hace `(*value >> 7) == 0`. En el asm
  original (`asm/us/main/sjis.s`, `0x800F18A4`) la carga es `lb` (con extensión de signo) seguida
  de `sra 7`. Con `char` sin signo CodeWarrior emitiría `lbu`.
- \`convertValueToDigits\` (\`src/main/utils.c\`, rama USA) hace \`buf[i] - '0'\` sobre \`char buf[8]\`
  y guarda el resultado como \`int32_t\`. En el asm original (\`asm/us/main/utils.s\`, \`0x800E5628\`)
  la carga es \`lb $v0, 0x28($v0)\`, con signo.
- Concuerda con el default de CodeWarrior (`#pragma unsigned_char` apagado) [dato recordado, no
  verificado contra la documentación; la evidencia que manda es el asm].
- `mk/toolchain/mwcc.mk` no pasa ninguna opción de signedness al compilador.

## TUs cuyo código depende del signo

Salida de `tools/port/metrics/char_sign_diff.py` (compila cada TU con los dos flags y compara los
objetos completos). El build lo fija con `port/psyq/include/port_game.h`, incluido a la fuerza en
cada TU del Juego: `_Static_assert((char)-1 < 0)`.

```
tus_checked=119 tus_differ=14
src/btl/battle_effect.c
src/btl/battle_hud.c
src/main/file_queue.c
src/main/font.c
src/main/main_menu.c
src/main/map.c
src/main/map_object.c
src/main/model.c
src/main/sjis.c
src/main/sound.c
src/main/utils.c
src/shop/script_menu.c
src/std/std_effect.c
src/vs/vs_effect.c
```

En los demás TUs el flag no cambia el código generado.
