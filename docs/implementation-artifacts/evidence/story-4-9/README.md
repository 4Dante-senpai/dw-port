# Evidencia de la historia 4.9 — 2026-11-02

Implementación basada en la spec completa y sus dos archivos `context`, leídos
antes de cambiar código. Builds y pruebas ejecutados dentro del contenedor, sin
commits, push, capturas de pantalla ni publicación.

## Verificación

- `cmake --build --preset native-headless -j2`: correcto.
- `cmake --build --preset web -j2`: correcto; genera y estampa `sw.js`.
- `ctest --preset native-headless -R pack --output-on-failure`: `pack` y
  `dw1_foreign_pack` pasan; fixture con disco omitido sin la variable de entorno.
- `web_sw_stamp`, `testing_deploy` y `testing_context`: pasan; cubren cambios de
  contenido, idempotencia, metadata opcional y contexto de deploy con `sw.js`.
- Prueba dirigida de activación diferida en Chromium, tres pasadas de puntero,
  tecla, arrastre y Juego: 12/12, sin instrumentación ni reintentos automáticos.
- La pasada final de `web_offline` por CTest pasa los 40 casos de Chromium/Firefox.
- `DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset web -R '^web_offline_real$'
  -FA disc_pack --output-on-failure`: pasa en ambos navegadores usando el fixture
  ya construido y verificado. Servidor detenido y contexto offline; Juego supera
  600 VBlanks, presenta frames, no avisa de audio y conserva el hash canónico tras
  aplicar una versión instalada durante la partida.

Las 20 recargas offline de cada navegador dan máximos de **352 ms en Chromium** y
**340 ms en Firefox**, con aislamiento, sin espera del Lock, Pack listo, sin error y
«Jugar» habilitado. Los JSON incluyen cada iteración; el límite sigue siendo 5 s.
La migración v0 produce bytes idénticos a v1, también offline; corrupción, cuota,
versión posterior y reemplazo interrumpido están cubiertos. Actualizar conserva
Pack, tarjetas y opciones.

## Observaciones

La primera pasada general encontró un bloqueo de activación diferida en Chromium.
Cambiar únicamente el orden de `registration.update()` no bastó. La corrección
consume los cuerpos de metadata y deja terminar esas lecturas antes de pedir la
activación, sin condicionar «Jugar». Las pruebas finales usan el worker real, sin
instrumentación ni esperas artificiales. La causa interna de Chromium permanece
como hipótesis en `../../deferred-work.md`; no se atribuye al incidente histórico
de Firefox del pipeline 141.

No cambian el formato vigente del Pack, la base `dw1` v2, `dwStore`, los flush de
`beforeunload`/`pagehide`, el código del juego, el backend ni CI. No se construyó el
binario matching de PS1: no hubo cambios en sus fuentes.

## Regresión general y límites de ejecución

La pasada `ctest --preset web -j2` se superpuso por error con la regresión de
disco/título en este contenedor de **2,5 CPU**. Pasaron todos los checks generales
salvo `web_audio`: Firefox observó tres resincronizaciones donde esperaba cero.
En la regresión real pasaron ocho de nueve checks, incluyendo disco, tarjetas,
Pack canónico y comparación de audio nativo/web; `web_scene_title` tuvo siete de
ocho casos correctos y falló el arranque normal de Chromium por audio. Los logs
originales y el contexto textual se conservan; no hubo capturas de pantalla.

Ejecutar `web_audio` solo, con los mismos límites y sin cambiar código ni
aserciones, pasó sus 32 casos en 246,42 s. Se registró la cuota y el consumo
del cgroup. La contención es una hipótesis apoyada por esta comparación; no se
presenta como causa probada ni se borra el resultado concurrente. No se activaron
reintentos automáticos ni se relajaron umbrales.

La comprobación aislada del arranque normal del título pasó en Chromium y Firefox
(2/2, 4,7 min), incluyendo audio y Start. Los JSON `title-play-*` y el log
`title-isolated.log` registran esa ejecución. Las verificaciones funcionales quedan
cubiertas, pero la pasada general concurrente no se declara verde: mantiene los
dos fallos descritos y su causa pendiente. En contenedores con esta cuota, correr
las suites sensibles al reloj en serie.

## Verificación final tras la revisión

Pasada completa en serie en el PC remoto, con disco, sobre el árbol final y sin
reintentos: `remote49-web.log` da **57/57 sin saltos en 2950 s**, incluidos
`web_offline`, `web_offline_real`, `web_audio`, `web_scene_title` y
`web_scene_title_audio`. `remote49-native.log` da 3/3 con `-R pack` (`pack_disc`
se salta allí porque esa pasada no define el disco; corre en la de web). Sin `Target crashed`.
Los dos fallos de la pasada concurrente no reaparecieron; su causa sigue sin demostrar.

Las 20 recargas offline de esta pasada dan máximos de **86 ms en Chromium** y
**107 ms en Firefox**. `web_offline_real` arranca offline antes y después de
actualizar, con 660/622 VBlanks en Chromium y 618/604 en Firefox, y mantiene el
SHA-256 canónico. No hay cambios en `src/`, `include/` ni `.gitlab-ci.yml`, así que
`make compare` no aplica.
