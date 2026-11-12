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
`sanitizer`; no avanzar el VBlank observado en `--hang-seconds` es `hang` y mata
el grupo del proceso. El límite inicial también cubre un bloqueo antes del primer
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
