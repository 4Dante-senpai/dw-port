# Playtest automático (Historia 6.9)

El reloj virtual conserva el control del juego. El digest observa cada VBlank desde
el primer retorno de VSync (frame 0), después de aplicar el mando; no llama APIs
PsyQ temporizadas ni escribe VRAM. La cadena SHA-256 incluye el digest anterior,
el hash del display RGB555 (dimensiones y estado encendido incluidos), VBlank,
máscara aplicada, contador y último valor de `rand`, y overlay. Los enteros se
serializan explícitamente little-endian. No incluye direcciones nativas.

`--digest FILE --digest-every N` escribe `VBlank overlay display_sha256 digest` cada
N VBlanks (60 por defecto) y el último VBlank al terminar. `off` identifica un
display apagado. El intervalo cambia la frecuencia de escritura, no la cadena.
Los cambios de overlay también se registran en stderr para no perder visitas
entre dos muestras del informe. Un display congelado o apagado produce avisos;
no implica por sí solo un fallo del juego.

## Grabar y reproducir

Nativo con ventana:

```sh
build/port/native-release/dw1 --pack build/port/native-release/us.pack \
  --record-inputs build/session.replay.txt --digest build/session.digest \
  --vblanks 30000
python3 -I -B tools/port/playtest/run.py --build native-headless \
  --inputs build/session.replay.txt --vblanks 30000 --output build/port/playtest/session
```

El formato compartido es una línea por cambio: `<VBlank> <máscara hex de 4 dígitos>`,
con VBlanks crecientes y bits PS1 wire-order (Start = `0008`, Cross = `4000`).
`--record-inputs` reutiliza el escritor de inputs resueltos y cuenta desde frame 0.
La opción histórica `--resolved-inputs` conserva sus posiciones absolutas para las
Escenas que ya la usan. Los inputs inválidos se rechazan antes de arrancar, con 2.

En la Shell normal se graban inputs y digest. «Descargar inputs» descarga una
instantánea coherente; el nombre contiene el límite de VBlanks y digest con los
que reproducirla. La instantánea se actualiza cada 60 VBlanks y al finalizar.
Para las pruebas web: `?pack=1&frames=0&replay=playtest&vblanks=N&digest=1&record-inputs=1`.
La página expone `Module.playtestDigest` al terminar y `Module.playtestSnapshot`
(`vblank`, `digest`, `inputs`) durante la corrida. La grabación vive en el hilo
del juego; el navegador solo descarga la instantánea publicada.

## Bot y comparación entre builds

```sh
python3 -I -B tools/port/playtest/bot.py --seed 42 --vblanks 30000 \
  --prefix tools/port/scenes/newgame.toml --output build/bot.inputs.txt
python3 -I -B tools/port/playtest/run.py --seed 42 --vblanks 30000 \
  --prefix tools/port/scenes/newgame.toml \
  --build native-headless-release --build native-asan --output build/port/playtest/comparison
```

Los pesos, duraciones y botones son constantes nombradas en `bot.py`. Una semilla
y el mismo prefijo producen exactamente los mismos bytes; no hay navegación,
objetivos ni lectura de memoria del juego. Para los TOML con inputs relativos a
símbolos, el host de Escenas resuelve primero el prefijo fijo a una grabación de
frame 0. Ese prefijo se conserva en la carpeta privada de la corrida; la fase
aleatoria solo empieza después de terminarlo. También se aceptan prefijos `.txt`
con inputs de frame 0. La pasada `--smoke` usa New Game y al menos 600 VBlanks
posteriores al prefijo, aunque su duración supere el límite mínimo pedido.

El runner escribe `report.json` con tiempos de pared, horas virtuales
(`VBlanks / 60.0087172527 / 3600`), overlays visitados, digest, semilla y avisos.
Con varios builds exige el mismo digest final y límite. Si difieren,
`report.json` y la salida del runner indican el primer intervalo de muestras distinto.
`--smoke` agrega el headless Debug u optimizado que falte cuando ambos están
construidos. `web_playtest` compara también con el optimizado y ASan disponibles.
`summary.json` contiene
solo estado, conteos, horas, overlays, semillas y hashes: es el único informe del
playtest publicable como artifact de CI. Los directorios privados conservan
inputs, stdout/stderr y las cards. No publicar estos directorios completos.

Una señal/salida no cero es `crash`; `PORT_ASSERT` es `assert`; ASan/UBSan es
`sanitizer`; no avanzar el VBlank observado en `--hang-seconds` es `hang`: el runner manda
SIGQUIT (el binario escribe su volcado y sale con 131), espera hasta 5 s (`QUIT_GRACE`) y mata el
grupo con SIGKILL si sigue vivo. El límite inicial también cubre un bloqueo antes del primer
digest. Salida 2 es `usage`, no un fallo del juego. Un éxito sin digest final al
límite pedido falla. Los mismos detectores se aplican al resolver el prefijo.

Cada fallo imprime su `reproduce.sh`. Ejecutarlo restaura la card de entrada y
repite el comando, inputs y límite exactos. El informe conserva el último
VBlank observado y la cola de stderr; con la frecuencia por defecto ese VBlank
puede preceder al punto de la caída hasta 59 VBlanks. El ejecutable y Pack deben
seguir disponibles en sus rutas originales. La semilla y el hash de inputs
permiten identificar el caso sin publicar la sesión.

## Sesiones, cards y soak

```sh
python3 -I -B tools/port/playtest/run.py --soak --jobs 4 \
  --seed 10 --seed 11 --prefix tools/port/scenes/newgame.toml \
  --session /private/sessions/world.replay.txt --card /private/cards/start.mcd \
  --build native-headless-release --build native-asan --vblanks 540079 \
  --output build/port/playtest/manual-soak
# Desde el host que dispone del contexto Docker del homelab:
tools/port/playtest/soak.sh --seed 10 --seed 11
```

`--inputs` reproduce una sesión exacta. `--session` la usa como prefijo antes del
bot. Repetir `--seed`, `--prefix`, `--session` y `--card` crea su producto cartesiano;
los builds de cada caso corren sobre los mismos inputs y su propia copia de la
card. Omitir `--card` arranca sin tarjeta. Nunca se necesita una card externa y
ninguna original se abre para escritura, tampoco al resolver prefijos.

Medición local de la historia 6.11 (contenedor con cuota de 2,5 CPU, builds en
secuencia, semilla 1 e inputs del smoke): a 29.435 VBlanks, Debug dio
112,22 VBlanks/s y optimizado 261,04 VBlanks/s (2,33×), con digest idéntico.
Esta muestra corta no alcanza el umbral de 2,5× exigido para el soak.
Evidencia privada: `build/port/verification-6-11/benchmark-short/report.json`.
Una extensión a 60.000 VBlanks abortó en ambos builds a los 34.200 por pasar
30 segundos virtuales sin frames; no sirve para cerrar la aceptación del soak.
Las reproducciones están en `build/port/verification-6-11/benchmark-soak/`.

`soak.sh` construye `native-headless-release` (`-O2 -g`, con aserciones e
instrumentación) y `native-asan` mediante `remote_run.sh` y deja informes en
`build/port/playtest/soak/<fecha UTC>/`. Usa cuatro casos en paralelo, cuatro
semillas por defecto y 540.079 VBlanks (2,5 horas virtuales) por caso/build:
10 horas por build, 20 entre ambos. La ejecución remota y su fallback son los de
`remote_run.sh`. No hay job de soak en CI ni cron instalado por esta historia.
Las sesiones reales, cards y capturas permanecen fuera de Git, en `build/` o
almacenamiento privado. La cobertura de los 16 overlays corresponde a 6.10.

Medición local (2026-11-11, build Debug, contenedor con otras pruebas en curso):
29.435 VBlanks en 408,69 s, **72,02 VBlanks/s de pared**, con digest por VBlank.
ASan midió 62,73 VBlanks/s en otra pasada de los mismos inputs. No son medidas del
homelab. Se ajusta la meta nocturna
a **20 horas virtuales combinadas**, con cuatro casos paralelos: si ambos builds
mantuvieran sus velocidades medidas, cada caso ocuparía unas 4,48 horas de pared, más el
prefijo. Reservar ocho horas y revisar los tiempos de `report.json` en la primera
noche del homelab antes de aumentar la meta. Con más prefijos o cards, la duración
crece con su producto cartesiano. La programación nocturna queda al orquestador.

## Verificación

```sh
cmake --build --preset native-headless
ctest --preset native-headless
ctest --preset native-asan
DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset native-headless -R '^playtest_smoke$'
DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B port/web/tests/run.py playtest
```

L1 cubre el digest, reloj/VRAM invariantes, roundtrip de una sesión en vivo sintética
con origen no cero, CLI inválida, determinismo del bot y todos los detectores con
procesos falsos. Las pruebas con disco salen con 77 si falta `DW1_DISC_DIR`.
Chromium y Firefox comparan el mismo replay con el nativo y descargan una grabación
web para reproducirla de nuevo en nativo. No se usan screenshots de Playwright.

Para comprobar capturas y trazas de una Escena con el observador activo, ejecutar
el runner habitual con `--digest` y comparar sus hashes con la ejecución sin él.
No se necesita referencia de emulador para el playtest.

### Resultado local y pendiente de aceptación (2026-11-11)

Los builds headless, ASan/UBSan, SDL y web compilan. Pasan las 13 pruebas Python
del runner/grabador, el test C del digest y las pruebas SDL afectadas. El
preprocesado de los 120 TUs no cambia y `make compare` conserva el binario matching.
El título conserva exactamente su captura (`56132366…46260d`) y su traza de reloj
(`68c73bbd…02068`) con y sin digest. Ambos navegadores pasan la grabación de teclado
en vivo durante 600 VBlanks y su replay nativo con el mismo digest. También pasan
la comparación corta de 600 VBlanks del replay fijo, con descarga y reproducción
nativa (`build/port/playtest-browsers-short.log`).

**Primer hallazgo del playtest (corregido el 2026-11-11, `83601d8`).** Con semilla 1,
29.435 VBlanks e inputs SHA-256
`22ebf45b72da1b64bb3e90e1529e0d15f91d2ab93598e7bf3830c3e856ba6ab3`, headless y ASan
divergían desde el VBlank 8.640: 36 píxeles en la caja `(190,148)-(192,159)`, también
con el observador desactivado. Compilar el Juego con `-ftrivial-auto-var-init` (ceros o
patrón) igualaba nativo y ASan; una bisección por archivo con ese flag aislado en un
lanzador del compilador señaló `src/main/script_textbox.c`. En USA, `drawString2` suma
`adv` después de un espacio de ancho completo (`0x4081`) sin asignarlo; como primer glifo
de una llamada lee un valor sin inicializar. El bloque `DW1_PORT` lo inicializa en `0xc`
(`docs/port/src-changes.md`). Después de la corrección, `native-headless`, `native-asan`,
Chromium y Firefox terminan con el mismo digest `7614dc25…4a5cee` en esos 29.435 VBlanks
(`web_playtest` en `docker-server`, 1.677 s).

Para un caso parecido: si un flag de inicialización de pila iguala los builds, bisectar
con un lanzador que agregue `-ftrivial-auto-var-init=zero` solo a la mitad de los
archivos, borrando los objetos que cambian de grupo, y revisar las advertencias
`-Wconditional-uninitialized` del archivo que quede.

La evidencia privada de esta sesión está en `build/port/playtest/cross/`,
`build/port/playtest/divergence/`, `build/port/playtest/scene-purity/` y los logs
`build/port/playtest-*.log`. Los `reproduce.sh` de ambos builds repiten el caso.
Las suites disponibles pasan (89 casos headless y 87 ASan, incluidos sus skips).
Las suites completas inicialmente fallaron en cinco comparaciones anteriores que
requieren referencias privadas o card ausentes: `scene_title_audio_compare`,
`scene_xa_op1_compare`, `scene_movie0_compare`, `scene_movie0_skip_compare` y
`scene_card_continue_compare`. Se excluyeron en la repetición; el preprocesado se
verificó por separado. No se ejecutó ni programó la tanda larga en el homelab.

## Soak y triage de la Historia 6.10

Primer soak completo en el homelab (2026-11-16, 4 semillas × 540.079 VBlanks, `native-headless-release`
y `native-asan`, 5,25 horas virtuales ejecutadas antes de los fallos): 7 de 8 corridas fallaron y los digests
coincidieron entre builds hasta el punto de fallo. Los overlays visitados fueron solo `MOV` (7) y
`SHOP` (11): el bot no sale de File City sin guía, así que la cobertura de los 16 overlays sigue abierta.
Los fallos (división por cero de `tickHealingParticles` y lectura fuera del arreglo del gráfico de
evolución) están en `docs/port/triage.md` con sus tests de regresión.

El supuesto cuelgue del VBlank 34.200 era una pausa del juego: `handlePause` espera Start sin llamar
a `VSync`, y el watchdog de 30 s virtuales solo miraba frames presentados. Ahora `PadRead` cuenta como
progreso del watchdog (el plazo no cambia). Un test L1 (`watchdog_pad_poll`) y un test clase B
(`playtest_pause_34200`) lo cubren.

Los tests clase B de regresión usan `tools/port/playtest/check_replay.py`: reproducen un archivo de
inputs sintético versionado (`tools/port/playtest/regress/*.inputs.txt`) y comparan el digest final con
`*.digest`.

Segundo soak (2026-11-16, tras las correcciones de `docs/port/triage.md`; `tools/port/playtest/soak.sh`,
4 semillas × 540.079 VBlanks, `native-headless-release` y `native-asan`): 8 corridas, 0 fallos, 20,00 horas
virtuales, digest final igual entre builds en las 4 semillas. Pared por corrida: 2.660 a 2.884 s en
release y 4.945 a 5.530 s en ASan. Overlays visitados: `MOV` (7), `TRN` (10, semilla 3 desde el VBlank
213.847) y `SHOP` (11). Faltan los otros 13: el bot sin guía no sale de File City y no llegó a `BTL`.
Informe privado: `build/port/playtest/soak/20261116T020359Z/`.

## Estado observable (Historia 6.12)

Las globales que lee el canal de estado y su tipo (todas en `include/dw/`, salvo `MAIN_STATE` y
`CURRENT_MENU`, que se declaran con `extern` en `src/main/*.c`): `CURRENT_SCREEN_ID` u16,
`GAME_STATE` s8, `MAIN_STATE` s8, `IS_SCRIPT_PAUSED` s32, `CURRENT_MENU` s32,
`BTL_COMMAND_MENU_ACTIVE` s8, la posición (`posData->location`) del tamer y del compañero, vida y MP
actuales del compañero, `PARTNER_PARA.condition`, `MONEY`, `YEAR`, `DAY`, `HOUR`, `MINUTE` y
`PLAYTIME_HOURS/MINUTES/FRAMES`.

`GAME_STATE` (`include/dw/btl.h:112`), verificado en `src/`:

| Valor | Significado | Cita |
|---|---|---|
| 0 | overworld | `tamer.c:369`, `:860`; `std_setup.c:473` |
| 1 | combate interactivo | `tamer.c:849`; `battle_setup.c:332`; `command_menu.c:201-235` |
| 2 | combate no interactivo (hipótesis hasta verlo en una captura) | `btl.c:521`; `battle_ui.c:341`; `battle_main.c:773` |
| 3 | carga de datos de combate (hipótesis hasta verlo en una captura) | `btl.c:409` |
| 4 | pelea de torneo | `std_main.c:4040` |
| 5 | torneo fuera de la pelea | `std_setup.c:263`, `:418`, `:593`; `std_main.c:3957`, `:4536`; `std_hud.c:1381` |

`NPCEntityTick` (`overworld.c:1143-1157`) lo confirma: 0 campo, 1 a 3 combate, 4 y 5 torneo.

`MAIN_STATE` (`main.c:3222`):

| Valor | Significado | Cita |
|---|---|---|
| 0 | partida nueva | `main.c:3467` |
| 1 | continuar un save de la máquina de guardado | `main.c:3483`, `main_menu.c:5277`, `partner.c:362` |
| 2 | continuar después del final | `main.c:3502`, `main_menu.c:5322` |
| 3 | fin del juego, sale de `gameLoop` (lo asigna el script) | `main.c:3550`; `script_interp.c:554` |

Al cargar un save se restaura desde el archivo (`main_menu.c:5059`).

## Canal de estado (Historia 6.12)

Solo en el host nativo (no en web). Dos flags de `dw1`; los dos fds los abre el bot (por ejemplo con
`pass_fds`) y deben estar abiertos:

- `--state-fd N`: un registro de texto por VBlank desde el frame 0, con `fflush`. Observación: los
  inputs siguen viniendo de `--inputs`. El estado no cambia con el canal: el mismo digest con o sin él.
- `--state-fd N --input-fd M`: lockstep. Cuando el registro dice `want=1` el binario espera una línea
  `<máscara> <hold>` por el fd M. Inválido junto a `--inputs`, `--checkpoints` o sin `--state-fd`.

Formato (ASCII, `\n`). La primera línea es el encabezado; el bot parsea por nombre de campo:

```
# dw1-state 1 k overlay screen gs ms paused menu btl_menu tx ty tz px py pz hp mp cond money year day hour minute play_h play_m play_f frame want
```

Los campos son decimales con signo según el tipo de la global, salvo `cond` (`0x%08x`). `k` es el VBlank
desde el frame 0 (el de `--record-inputs` e `--inputs-from frame0`), `overlay` el último overlay
cargado, `tx ty tz` / `px py pz` la posición del tamer / compañero (0 si no tiene `posData`),
`frame` los frames presentados. Un global ausente da 0. Los campos nuevos se agregan al final; la
versión sube solo si cambia un significado.

Respuesta (solo cuando `want=1`): máscara hexadecimal de 1 a 4 dígitos (orden wire de `--inputs`) y
`hold` decimal de 1 a 1.000.000. La máscara se aplica en `k` y dura `hold` VBlanks; el próximo
`want=1` llega en `k+hold`. `--record-inputs` graba lo resuelto, así que su replay con
`--inputs --inputs-from frame0` da el mismo digest.

- En lockstep un bot que nunca responde cuelga el juego: el binario no tiene timeout de espera de la respuesta, y el runner debe detectar el cuelgue por la falta de registros nuevos.
- Respuesta inválida: `scene: invalid input channel reply`, salida 2.
- El bot cierra el fd de input, o el lector del fd de estado se cierra (`SIGPIPE` se ignora): parada
  limpia con `port: input channel closed at VBlank k`, digest final escrito, salida 0.

Cliente: `tools/port/playtest/channel.py`. Pruebas: L1 en `test_playtest.py` (runtime sintético) y
clase B `playtest_channel` (`test_channel.py`: prefijo `newgame` más 3000 VBlanks con semilla por el
canal, replay sin canal con el mismo digest).

## Diagnóstico al morir (Historia 6.13)

`dw1 --death-file FILE` (solo nativo; no en web) abre `FILE` (0600) al arrancar; vacío, repetido o no
escribible es uso (salida 2). Al morir escribe un solo volcado (el primero gana) y no cambia ni el
comportamiento ni los códigos de salida: un crash muere por la misma señal, el watchdog sale con 3 y ASan
conserva su salida. Sin el flag todo queda como antes (el texto `dw1: signal N at address P; backtrace:` de
stderr se conserva). Una corrida sana deja el archivo vacío. El runner siempre lo pasa (`work/death.txt`,
también en `reproduce.sh`).

Disparadores (`reason`): SIGSEGV/SIGBUS/SIGILL/SIGFPE (`segv|bus|ill|fpe`, solo sin ASan), SIGABRT
(`abort`, cubre `PORT_ASSERT`), callback de muerte de ASan (`sanitizer`, cubre ASan y UBSan no recuperable),
watchdog (`watchdog`, antes de `exit(3)`) y SIGQUIT (`quit`; si llega a otro hilo se reenvía al hilo del juego).

```
dw1-death 1
reason segv
signal 11                  (0 si no aplica; SIGQUIT = 3)
fault_address 0x00000000
vblank 1234
# dw1-state 1 k overlay screen ...     (encabezado y registro del canal de estado, want = 0)
1234 3 ...
input 1200 0010            (hasta 32 cambios de máscara, del más viejo al más nuevo)
pc 0x0804a1b0              (solo en señales: pc interrumpido, exacto; el runner no le resta 1)
frame 0x0804a1b2           (direcciones crudas del hilo del juego)
```

No se vuelcan bytes del disco, del Pack ni de la RAM: solo las globales del canal de estado. El volcado es
best-effort desde contexto de señal (usa stdio abierto de antemano).

Firma `<tipo>:<f1>|<f2>|<f3>` (`run.signature`): `asan-<kind>` (de `ERROR: AddressSanitizer: <kind>`),
`ubsan-<check>` (de `SUMMARY: UndefinedBehaviorSanitizer`, `ubsan-undefined-behavior` si falta), `assert`
(`PORT_ASSERT` o `reason abort`), `watchdog`, `hang`, `crash-sig<N>`, `exit-<N>` (salida no cero sin volcado)
en ese orden de prioridad. Las funciones son los 3 primeros frames del binario simbolizados con
`llvm-symbolizer-18 --obj=<dw1>` (dirección − 1; el binario no es PIE) tras quitar frames del volcado
(`port_death_*`, `death.c`), `watchdog` y lo que no resuelve (libc, runtime de sanitizers). Sin frames la
firma es solo el tipo. Sin simbolizador, `death.symbolized` es `false` y la firma usa las 3 primeras
direcciones crudas (sin filtrar, así que agrupa mal).

`report.json` (privado) gana por corrida con volcado `death: {reason, signal, fault_address, vblank, state,
inputs, frames: [{address, function, file, line}], symbolized}` y `signature` para toda corrida fallida salvo
`usage`. `summary.json` gana solo `signatures: [{signature, count, seeds}]` (ordenado por firma; `count` son
corridas): ni estado, ni inputs, ni rutas, ni frames. UBSan corre con `report_error_type=1:print_stacktrace=1`.

Pruebas: L1 en `test_playtest.py` (runtime sintético con `DEATH_MODE=segv|assert|watchdog|spin`, procesos
falsos para firma, filtro, SIGQUIT ignorado y agregación) y clase B `playtest_death` (`test_death.py`).

## Contrato con el soak del homelab

Lo extienden las historias 6.13 a 6.16.

- Invocación vigente, sin cambios:
  `run.py --soak --build native-headless-release --build native-asan --prefix tools/port/scenes/newgame.toml
  --vblanks 540079 --jobs 14 --seed N... --output DIR` (o `tools/port/playtest/soak.sh`).
- Flags nuevos, todos opcionales: `dw1 --death-file FILE` (el runner ya lo usa; no hay flag nuevo en `run.py`).
- Claves nuevas de JSON: `report.json` por corrida fallida, `death` (si hay volcado) y `signature`;
  `summary.json`, `signatures: [{signature, count, seeds}]`. Las claves existentes no cambian.
- Cambios de comportamiento: un cuelgue recibe SIGQUIT y 5 s de gracia antes de SIGKILL (antes SIGKILL
  directo); `UBSAN_OPTIONS` se completa con `report_error_type=1:print_stacktrace=1`. Los digests no cambian.
