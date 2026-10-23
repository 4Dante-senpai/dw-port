---
title: 'Corrección E3: ritmo de la ventana nativa y texto Shift-JIS'
type: 'bugfix'
created: '2026-10-23'
status: 'done'
baseline_commit: '9bbbd5e40a100c8106566acf4acceb3ccf409658'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-3-context.md'
  - '{project-root}/docs/implementation-artifacts/spec-3-9-preset-native-con-ventana-sdl3.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** En la prueba manual de la ventana `native` (dueño, 2026-10-23) aparecieron tres problemas.
- El juego corre unas 3 veces más rápido. El host espera un período de 59,94 Hz por cada frame del Juego, pero DW1 llama a `VSync(3)`, así que cada frame dura 3 VBlanks.
- El audio se oye entrecortado. El SPU produce 3 veces más rápido de lo que consume el dispositivo, el ring de `audio_rate` (8192 frames) se desborda y se descartan muestras.
- En la pantalla de nombres, todas las letras salen como "△". Los literales no ASCII de `src/*.c` (por ejemplo `"０１２３４"`) llegan como UTF-8 porque Clang no soporta `-fexec-charset=CP932`, el flag que usa el build PS1 en `mk/toolchain/gcc.mk`.

**Approach:**
- El ritmo de pared del host se calcula por VBlanks virtuales (`clock_vblanks()`), no por frames.
- El build del port compila una copia de cada fuente del Juego con los literales no ASCII convertidos a escapes octales CP932, sin tocar `src/`.
- Una Escena de la pantalla de nombres comparada contra la referencia cierra la corrección con evidencia.

**Decisión del dueño (2026-10-23):** durante el desarrollo el juego corre siempre a velocidad normal. Un modo acelerado opcional queda como trabajo futuro de la versión web (`deferred-work.md`), no en esta corrección.

**Decisión del dueño (2026-10-23, dithering):** PCSX-Redux no aplica dithering a los polígonos texturados con color plano y modulación, aunque la consola sí lo hace (psx-spx; DuckStation `IsDitheringEnabled`). Con ese dithering apagado en el port, toda la interfaz de `naming` coincide exacta. Se parchea la referencia (`tools/port/reference/pcsx-redux-flat-texture-dither.patch`) para que haga lo mismo que el hardware. La comparación sigue siendo exacta y las referencias existentes se vuelven a verificar con la imagen nueva.

## Boundaries & Constraints

**Always:**
- Sin cambios en `src/` ni en el reloj virtual. El Host sigue siendo el único dueño de la deriva (AD-10).
- La conversión es la misma en todos los presets (`native-headless`, `native-asan`, `native`, `web`; AD-17) y usa la misma codificación que `tools/gen_text.py` (`__c_string`: cp932 → `\ooo`).
- La conversión toca solo el interior de literales de cadena y de carácter. Los comentarios, el código y las directivas quedan iguales, y `#line` conserva la ruta original en diagnósticos, `__FILE__` y la información de depuración.
- Los hashes de las Escenas existentes (`title`, `movie0`, `newgame` por estado, etc.) no cambian y `make compare` sigue idéntico.

**Never:**
- Un modo acelerado (diferido), la calibración de costos ni la medición de 10 minutos (3.10).
- Cambiar el remuestreo o las constantes de `audio_rate`, salvo que la evidencia muestre que el ritmo correcto no alcanza.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Ventana normal | `dw1 --pack us.pack` | tiempo de pared ≈ VBlanks virtuales × 1001/60000 s | N/A |
| Atraso del host | el host queda > 4 VBlanks detrás (pausa del debugger, carga) | resincroniza sin intentar recuperar | N/A |
| Corrida de Escena | `--capture*` presente | sin espera de pared; mismo SHA que headless | N/A |
| Literal no ASCII | `"０１２３４"`, `'　'` | bytes CP932 idénticos a los del build PS1 | carácter sin CP932: el build falla con archivo:línea |
| Comentario o código con no ASCII fuera de literales | `/* ０ */` | se copia igual | N/A |
| Audio con ritmo correcto | 10 s de ventana con `SDL_AUDIODRIVER=dummy` | 0 desbordes en `audio_rate` | contadores impresos al salir |

</frozen-after-approval>

## Code Map

- `port/host/sdl/video.c:30-48` `plat_native_present` -- hoy suma 1001/60000 s por llamada (una por frame, desde `port_frame_done` en `port/psyq/crt.c:125-129`). Debe sumar `clock_vblanks()` menos el último valor (`port/psyq/clock.h:38`), con el mismo acumulador entero exacto y la resincronización de 4 VBlanks. No esperar cuando hay captura de Escena (`capture_config`, `crt.c`). Si hace falta, se expone un predicado.
- `port/host/sdl/audio.c`, `port/host/audio_rate.h:6-10` -- `overflows`/`underruns` ya existen. Imprimirlos en stderr al cerrar (`sdl_audio_quit`) para tener evidencia; no cambiar el algoritmo.
- `CMakeLists.txt:42-66` -- arma `DW1_GAME_SRC`: el main va directo y los overlays pasan por `dw1_overlay_wrapper` (`#include "${src}"`, línea 36). Pasar todas las fuentes por un `add_custom_command` que genere `${CMAKE_BINARY_DIR}/sjis/<rel>`, con dependencias en la fuente y en el script. El wrapper incluye la copia y el main compila la copia. Ajustar `set_source_files_properties(... src/main/main.c ...)` (línea 145) a la ruta nueva. El filtro `/src/mov/.*[.]o$` (línea 484) sigue funcionando si se conserva `src/<dir>/` en la ruta.
- `tools/gen_text.py:56-70` -- la lógica de escape que hay que reproducir (no importar el módulo: usa nombres privados y PyYAML).
- Archivos US que hoy tienen literales no ASCII (todo `src/` se convierte igual): `src/main/main_menu.c`, `src/main/battle_ui.c`, `src/shop/script_menu.c`, `src/trn/trn_hud.c`, `src/trn2/trn2_hud.c`, `src/std/std_main.c`, `src/vs/vs_{select,demo,intro,combat}.c`. Ninguno tiene `#include "..."` relativo a su directorio.
- `tools/port/scenes/newgame.toml` -- inputs con `after` por punto de control. `setupNameSelectorBox` llega 868 VBlanks después de `runLandingScreen`. Con `checkpoints`, la captura ocurre al completar el último punto más su `window` (`port/psyq/scene.c:165-171`, `crt.c:84-87`). La captura headless del orquestador (dos Cross después de abrir la caja) muestra la grilla y el nombre con "△".
- `tools/port/reference/capture.py` -- captura de referencia (PCSX-Redux; clase B, la corre el orquestador en el PC del dueño, porque Codex no tiene Docker).

## Tasks & Acceptance

**Execution:**
- [x] `tools/port/sjis_literals.py` + `tools/port/test_sjis_literals.py` -- conversor (`entrada salida`) que escribe `#line 1 "<ruta original>"` y escapa a CP932 los caracteres no ASCII dentro de `"..."`/`'...'`. Respeta escapes existentes (`\"`, `\\`), comentarios `//` y `/* */`, y falla con archivo:línea si un carácter no tiene CP932. El test cubre literal, carácter, comentario, escape y un error, y compara con `__c_string` de `gen_text.py` para el mismo texto.
- [x] `CMakeLists.txt` -- copias convertidas para todas las fuentes del Juego, wrapper de overlays, propiedad de `main.c`, test L1 `sjis_literals` en los presets nativos.
- [x] `port/host/sdl/video.c` (y `crt.c`/`plat.h` si hace falta el predicado de captura) -- ritmo por VBlank virtual y sin espera en corridas de Escena.
- [x] `port/host/sdl/audio.c` -- contadores en stderr al salir.
- [x] `CMakeLists.txt` test `sdl_pacing` (preset `native`, offscreen/dummy) -- corre ~10 s virtuales y verifica que la pared quede entre 0,95 y 1,10 del tiempo virtual (`ciclo / CLOCK_HZ` de la línea `reached frame`) y que `overflows=0`. Puede ser un script de Python bajo `tools/port/`.
- [x] `tools/port/scenes/naming.toml` -- Escena de la pantalla de nombres: inputs de `newgame` hasta `setupNameSelectorBox` más dos Cross. La captura muestra la grilla y dos letras en el nombre. Lleva `reference_sha256` de la referencia, o excepciones por región con motivo si el modelo 3D no da exacto. Agregarla a CTest clase B como las otras Escenas comparadas.
- [x] `docs/port/src-changes.md` o `docs/port/setup.md` -- una línea sobre la conversión CP932 del build del port.
- [x] `docs/implementation-artifacts/deferred-work.md` -- registrar el modo acelerado opcional de la web (decisión del dueño).

**Acceptance Criteria:**
- Given el build `native-headless`, when `run.py naming --build native-headless --compare`, then pasa contra la referencia y la grilla muestra letras (no "△").
- Given `sdl_pacing`, when corre, then el tiempo de pared queda dentro de [0,95, 1,10] del virtual y hay 0 desbordes.
- Given los cambios, when corren `ctest` en `native-headless`, `native-asan`, `native` y `web`, más las Escenas clase B, then todo queda verde con los mismos hashes y `make compare` idéntico.
- Given la ventana en el PC del dueño, when juega hasta salir de la casa de Jijimon, then la velocidad y el audio son normales y los nombres se leen (verificación manual del dueño; no bloquea el cierre).

## Implementation Notes

- Las 119 unidades C del Juego se compilan desde copias convertidas en los cuatro presets. Los includes relativos conservan el directorio de la fuente original; el wrapper de overlay depende de su copia generada.
- `sdl_pacing` usa el host SDL, `VSync(3)` y SPU reales con un main sintético sin disco. También verifica que una captura solicitada omita la espera antes de alcanzar su marca.
- `naming` tiene dos capturas idénticas tanto en referencia (`794b48cebbb26a7dad6c1e2fdccd7dfe86554b874145331f92cb9a65db76229f`) como en headless (`30b9811a801b1e5f419ec4996d8eeba0668951f6ddea06e9a1aee411daa946e9`). La comparación RGB555 todavía difiere en 1602 píxeles, incluidos píxeles de la interfaz. Las diferencias de un nivel de color son compatibles con dithering; la ruta de polígonos planos del rasterizador software de PCSX-Redux omite ese paso. Falta resolver y verificar la paridad; no se agregaron excepciones para ocultar diferencias en el texto ni se alteró la GPU del port para acomodar el oráculo. Esta aceptación sigue abierta.
- Orquestador: Codex se cortó por el límite de 90 min durante ASan y no llegó a informar. Un diagnóstico temporal (sin dithering en polígonos texturados planos del port, no commiteado) dejó toda la interfaz exacta. Por decisión del dueño se agregó `tools/port/reference/pcsx-redux-flat-texture-dither.patch`, una capa del `Dockerfile` y su entrada en `.dockerignore`: Redux dibuja esos polígonos por el camino Gouraud con color único cuando hay dithering y modulación. Con la imagen nueva, la referencia de `title` no cambió (`561323…`) y `naming` pasó a `83623016…`; la interfaz coincide exacta. Quedan 663 px del modelo 3D de Jijimon (hasta 12 niveles, no dithering), declarados como excepción de región y diferidos en `deferred-work.md`.
- `naming` termina con un solo Cross después de abrir la caja (nombre "A_"), no con dos: alcanza para mostrar la grilla y una letra elegida.
- El ritmo por VBlank no alcanzaba para el audio: con el juego real, `sdl_frame0` daba `overflows=142443` en el arranque y `dw1 --frames 1200` sumaba otras 80612 muestras perdidas. Mientras el Juego carga sin presentar frames, el reloj virtual avanza más rápido que la pared y el SPU llena el anillo de 186 ms. Ahora `produce` (`port/host/sdl/audio.c`) espera a que el dispositivo drene cuando el nivel supera `AUDIO_RATE_TARGET + AUDIO_RATE_DEADBAND`, con un tope de 100 ms por si el dispositivo se detiene, y no espera en corridas de Escena. No cambian el remuestreo ni las constantes. El primer `present` solo fija el ancla, porque el boot ya lo ritmó el audio; si no, el arranque esperaba 3,3 s de más. `sdl_frame0` falla si aparece `overflows=[1-9]`.

## Spec Change Log

## Review Triage Log

| # | Capa | Hallazgo | Veredicto | Ruta | Evidencia |
|---|---|---|---|---|---|
| 1 | blind/edge/verif | los literales de `#define` quedan en UTF-8 y un carácter sin CP932 no falla | medium | patch | El estado `directive` no entraba a literales. Hoy no hay casos en `src/`, pero un merge de upstream rompería el texto sin avisar. Ahora convierte las cadenas de directivas salvo el nombre de `#include`, y deja los apóstrofos (`#error can't`); el test cubre ambos casos. |
| 2 | blind/edge | `"\uFF10"` no se normaliza | low | rechazado | No hay escapes `\u` en `src/` y el arreglo agrega una rama de decodificación para un caso que el decomp no usa. |
| 3 | blind/edge | delimitadores de comentario partidos por `\` desincronizan el análisis | low | rechazado | Construcción patológica sin casos en `src/`; arreglarla exige empalmar líneas antes de tokenizar. |
| 4 | edge | comentario de bloque en una directiva que sigue en otra línea | low | rechazado | Mismo caso patológico que 3, sin ocurrencias. |
| 5 | edge | prefijos `L`/`u`/`U` | low | rechazado | El decomp no tiene literales anchos (CodeWarrior PS1); agregar el rechazo es una rama nueva. |
| 6 | edge | error de escritura con traceback | low | rechazado | CMake vuelve a generar la salida en la siguiente compilación; no queda estado persistente roto. |
| 7 | blind/verif | la resincronización tras una pausa no tiene prueba | low | rechazado | Lógica de 3 líneas que solo afecta a la depuración; probarla exige inyectar retrasos de pared (igual que el hallazgo 8 de la 3.9). |
| 8 | verif | `sdl_pacing` solo usa `VSync(3)`; un `elapsed` constante pasaría | medium | patch | El main sintético ahora alterna `VSync(0)`, `VSync(2)` y `VSync(3)`; un 3 fijo daría una relación de pared ≈ 1,5. |
| 9 | blind/verif | `sdl_pacing` no exige underruns = 0 | medium | patch | El reclamo del dueño era el audio entrecortado. Ahora exige `overflows=0 underruns=0`; pasó 3 veces seguidas (relación 1,0143). |
| 10 | blind | el parche de Redux no tiene vectores independientes | low | rechazado | No hay hardware para generar vectores. Hay acuerdo cruzado: el port, que sigue psx-spx/DuckStation, coincide exacto en la interfaz, y la referencia de `title` no cambió. |
| 11 | orquestador | el juego real desborda el audio durante las cargas aunque `sdl_pacing` dé 0 | high | patch | `dw1 --frames 1200`: antes `overflows=223055`, pared 25,69 s; después `overflows=0 underruns=0`, pared 25,52 s contra 25,51 s virtuales. `sdl_frame0` ahora exige 0 desbordes. |

## Design Notes

- **Todas las fuentes se convierten, no solo las 10 de hoy:** así un literal nuevo que traiga un merge de upstream no rompe el texto en silencio, y el costo es una copia por archivo.
- **Sin espera en Escenas:** la captura no depende del reloj de pared (AD-8), y una Escena larga en `native` tardaría minutos reales sin aportar nada.

## Verification

**Commands:**
- `tools/port/remote_run.sh sh -c 'for p in native-headless native-asan native web; do cmake --preset $p && cmake --build --preset $p && ctest --preset $p || exit 1; done'` -- expected: verde.
- `DW1_DISC_DIR=/dw/game_rom/extracted tools/port/ci/ctest_no_skip.sh --preset native-headless -R scene` -- expected: Escenas (incluida `naming`) sin omisiones.
- `make compare` en `dw-build` -- expected: `Compiled binaries match with target binaries`.

**Evidencia final (2026-10-23, daemon remoto, corridas seriales con `ctest_no_skip.sh` y disco):**
- `native` 15/15, `native-headless` 79/79, `native-asan` 77/77, `web` 38/38; sin omisiones ni fallos. `scene_naming_compare` pasa en headless y en `native` contra la referencia parcheada (`83623016…`, 663 px del modelo 3D en excepción de región).
- `sjis_literals`: 6 pruebas correctas. El ejecutable contiene `82 4f 82 50 82 51 82 52 82 53 00` para `MAIN_D_80130738` (`０１２３４`).
- `sdl_pacing`: pared 10,138 s / virtual 10,010 s = 1,0128; `overflows=0 underruns=0`; captura sin espera de pared en 0,655 s.
- Juego real con `SDL_AUDIODRIVER=dummy`: a 30/120/600/1200 frames, `overflows=0 underruns=0`; en el frame 1199, 25,52 s de pared contra 25,51 s virtuales.
- Las referencias `title`, audio del título, XA, `movie0` y `movie0_skip` conservan sus hashes con la imagen nueva de Redux.
- `make compare`: `Compiled binaries match with target binaries` (sin cambios en `src/`).
