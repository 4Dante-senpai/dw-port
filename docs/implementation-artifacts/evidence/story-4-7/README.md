# Historia 4.7 — audio en el navegador

Validación del 2026-10-31 dentro de este contenedor, con Playwright 1.64.0,
Chromium y Firefox instalados en `/opt/ms-playwright`. No se tomaron capturas.
Los PCM, replay y logs de ejecución del disco permanecen fuera del repositorio
(`/tmp/dw1-web-title-audio`); aquí solo se incluyen métricas y hashes.

## Implementación

El hilo del juego produce PCM estéreo 44.100 Hz mediante el callback de la SPU.
Un AudioWorklet JavaScript consume el ring compartido y replica `audio_rate_pull`;
no carga WebAssembly. El productor espera con futex hasta 100 ms por bloque
(excepto replay, que conserva todas las muestras). La capacidad es 8192 y la corrección máxima 2 %. La banda web es 1536
frames para absorber lotes de varios VBlanks; la nativa conserva 384. El target web C/JS es 6144 (139,3 ms), frente a los
2208 nativos, que no se modifican. La corrida piloto aislada con target 2208
registró 1 underrun al minuto y 3 a los dos minutos, con 0 overflows y 0
resincronizaciones. Se añaden medidas atómicas del intervalo de entrega y nivel
previo de la cola para documentar la calibración; ningún contador se filtra.

La shell conecta un GainNode, guarda volumen en PortSettings v3 y migra v2
conservando propiedades adicionales. Pausa, visibilidad y fin suspenden el contexto.
Al iniciar o reanudar, el productor espera una confirmación desde el callback de
render del worklet, además de `AudioContext.resume()`: el estado `running` puede
adelantarse a la primera entrega real. Una regresión previa de Chromium detectó
4243 frames perdidos en esas transiciones con el límite de 100 ms; el handshake
evita liberar el productor mientras el dispositivo aún no procesa audio.
Firefox también puede tardar más de 100 ms. Los fallos de audio muestran un aviso y permiten
jugar sin productor activo.

La fixture nativa ejecuta las declaraciones originales de `title_audio.toml`
mediante un helper compartido con `run_audio`, sin Redux ni tarjetas. Registra PCM
completo, cambios de input efectivamente aplicados y VBlank absoluto final.
El navegador reproduce estos inputs ignorando los controles en vivo; el tap copia
las muestras consumidas antes de remuestrear o aplicar ganancia.

## Comandos y resultados

- `cmake --build --preset native-headless`: correcto.
- `cmake --build --preset web`: correcto.
- `make -j2 && make compare`: binarios matching idénticos.
- `ctest --preset web -R web_audio_rate_parity --output-on-failure`: correcto;
  1080 bloques a 44.1/48/96 kHz, todas las muestras, cursores, contadores y estado
  del remuestreador idénticos, con wraparound, starvation y recuperación.
- `ctest --preset web -R '^web_audio$' --output-on-failure`: correcto entre
  Chromium y Firefox (246,71 s en `final47-web.log`). Rampa completa sin pérdidas, ganancia
  medida exactamente 0,5, mute sin detener consumo, pausa de 30 s, visibilidad,
  migración/persistencia/protección de settings, fallos de inicialización,
  contrapresión acotada tras suspender el dispositivo y suspensión al finalizar.
  `audio-{chromium,firefox}.json` conserva contadores sin filtrar, ambos en cero
  para las corridas de rampa, pausa y visibilidad.
- `ctest --preset web -R '^(web_shell|web_input|web_audio)$'`: shell e input
  pasaron en ambos navegadores; audio detectó el problema de disponibilidad del
  dispositivo descrito arriba. Tras corregirlo, la suite de audio completa pasó.
- `DW1_DISC_DIR=/dw/disks/us python3 -I -B tools/port/scenes/pcm.py --pack build/port/web/us.pack`:
  captura nativa correcta, 12.842.405 frames, VBlank final 17476.
  SHA-256 PCM `70ae581c607a99df57a281f8d67447efe498101c3d7956178946b4aa18849ef5`.
- `ctest --preset native-headless --output-on-failure`: cinco comparaciones
  bloqueadas por fixtures privadas: `scene_title_audio_compare` y
  `scene_xa_op1_compare` sin evidencia vigente, `scene_movie0_compare` y
  `scene_movie0_skip_compare` con manifests incompletos, y
  `scene_card_continue_compare` sin la tarjeta externa requerida.
  No se regeneran esas referencias en esta historia.

## L4: diez minutos

La corrida válida es la final, sobre el código con los parches de la revisión, en serie, en el PC del
dueño con 20 CPU y con la calibración 6144/1536. Los contadores y umbrales no se relajaron; `long.spec`
además exige ahora que `written` cubra al menos el 99 % de lo esperado.

| Corrida final | VBlanks / esperados | Written | Underruns | Overflows | Resync | Archivo |
|---|---|---:|---:|---:|---:|---|
| Chromium | 36055 / 36054,5 | 26.496.576 | 0 | 0 | 0 | `final47-long-chromium.json` |
| Firefox 1 | 36057 / 36057,1 | 26.498.046 | 0 | 0 | 0 | `final47-long-firefox1.json` |
| Firefox 2 | 36056 / 36056,0 | 26.497.310 | 0 | 0 | 0 | `final47-long-firefox2.json` |

Historial, todas anteriores a los parches de la revisión y con `-O2`. Las corridas anteriores a `-O2`
quedaron en `long-firefox-first-attempt.json` y `calibration.json`; ninguna cuenta como evidencia.

| Corrida | Entorno | Underruns | Overflows | Resync | Resultado |
|---|---|---:|---:|---:|---|
| Chromium | local, 2,5 CPU | 0 | 0 | 0 | pasa (`long-chromium.json`) |
| Firefox 1 | local, 2,5 CPU | 0 | 0 | 0 | pasa (`long-firefox.json`) |
| Firefox 2 | local, 2,5 CPU | 0 | 0 | 1 | **falla** (`verify47-long-firefox-repeat2.log`) |
| Firefox 3 | PC del dueño, 20 CPU | 0 | 0 | 0 | pasa (`long-firefox-remote1.json`) |
| Firefox 4 | PC del dueño, 20 CPU | 0 | 0 | 0 | pasa (`long-firefox-remote2.json`) |

Firefox 2 se atrasó 70,8 ms en el VBlank 7574, contra un límite de 66,7 ms. En las corridas
diagnósticas de Firefox, 6 de 7 resincronizaciones cayeron entre los VBlanks 7348 y 7955, donde
el juego sube de unos 30 a unos 60 fps. Lectura: con 2,5 CPU para juego, Firefox, WebGL software
y Xvfb, ese tramo queda sin margen; con 20 CPU no ocurre en las cuatro corridas hechas. Es la
explicación que mejor encaja, sin ser una causa probada. La aceptación final en hardware de
referencia es de la historia 5.6, y correr esta prueba con el tope de 2,5 CPU en Firefox puede fallar.

El bloqueo artificial de dos segundos posterior a la ventana de diez minutos da 1 underrun en
Firefox (`after_block_underruns`) y una resincronización de unos 1,9 s en el log; ambos quedan fuera
de la ventana medida y no alteran sus contadores.

El objetivo web de 6144 frames (139,3 ms) triplica el nativo (50 ms): el piloto con 2208 dio
3 underruns en dos minutos. Es latencia audible respecto de la imagen. No se midió la sincronía
con video; queda para la historia 5.6.

## Paridad PCM y señal

`web_scene_title_audio` compara el PCM crudo del worklet con los 51.369.620 bytes nativos
(12.842.405 frames, 4 min 51 s). En la corrida final coincide byte a byte en Chromium y Firefox, sin
primera diferencia, con 0 underruns y 0 overflows (`title-audio-{chromium,firefox}.json`). La corrida
previa de Firefox contó 1 underrun, que no pierde muestras porque el modo replay no descarta.

Con el Pack real, «Jugar» llega al título con contexto `running`, salida no nula y contadores en
cero en ambos navegadores (`title-play-{chromium,firefox}.json`, `final47-disc.log`).

## Regresiones finales

Sobre el código final:

- `ctest --preset web`: 51/51, 6 saltadas sin disco (`final47-web.log`).
- `DW1_DISC_DIR=... ctest --preset web -R "pack_disc|native_title_capture|native_title_audio|web_scene_title"`:
  5/5 (`final47-disc.log`), incluido `web_scene_title`.

Sin repetir tras los parches de la revisión, que solo tocaron `port/host/web/`, `port/web/` y sus
pruebas:

- `ctest --preset native-headless`: 85/85 sin disco, con las clases B saltadas
  (`verify47-native-verification.log`), corrida después del último cambio de código nativo.
- `make compare`: binarios idénticos (`verify47-matching.log`); `src/` no cambia en esta historia.
- Las cinco comparaciones nativas de la sección anterior necesitan referencias privadas ausentes en
  este entorno; quedan a cargo de `native-disc` en CI.
