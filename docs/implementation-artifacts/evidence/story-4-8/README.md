# Evidencia de la historia 4.8

Los `.mcd`, Pack, replays,
PPM y PCM permanecen fuera de los archivos versionados; este directorio contiene
únicamente resultados, hashes y estados de la interfaz.

## Tarjetas y escenas

`native-fixture.json` registra las capturas nativas de `card_newgame`,
`card_continue` y el control con tarjeta nueva. El frame del control es diferente
al de Continue con datos. El Pack se reconstruyó del `.cue/.bin` en la verificación final.

`cards-real-chromium.json` y `cards-real-firefox.json` registran la igualdad byte
a byte entre la tarjeta guardada en web y la nativa, el frame de Continue después
de cerrar/reabrir, y la importación/exportación y Continue en otro contexto.
Las capturas usan el VBlank efectivo de los checkpoints nativos, sin portar el
motor de Escenas al navegador.

## Resultados

- Builds `native-headless` y `web`: aprobados dentro del contenedor.
- CTest nativo sin disco: 60 aprobados, 25 omitidos, cero fallos.
- Comprobación final de tarjetas nativas: `card`, `card_host` y `card_cli`
  aprobados después del último build.
- Primera pasada completa web: 42 aprobados, 8 omitidos y 4 checks con fallos
  descritos abajo. Tras las correcciones y sin otras suites activas,
  `ctest --preset web --rerun-failed --output-on-failure` aprobó los cuatro:
  `card`, `web_boot` (8 casos), `web_input` (20) y `web_cards` (26).
- `cards-reloads-<browser>.json`: 20 recargas por navegador; máximos de 98 ms
  en Chromium y 169 ms en Firefox, conservando estado de instancia, Pack,
  mensaje y habilitación de «Jugar». El JSON anterior a aislar el fixture se
  conserva también para Firefox.
- Título: 8 casos aprobados; los cuatro JSON `title-*.json` registran frames
  idénticos a nativo, con y sin el guard de almacenamiento.
- Audio de título: 2 casos aprobados; los JSON `title-audio-*.json` registran
  51.369.620 bytes comparados sin diferencias y cero desbordamientos. Las
  estadísticas de reproducción se conservan, incluidos los underruns observados
  durante la ejecución con carga; esta prueba compara el PCM crudo.
- Última ejecución de `cards_real` con el Pack existente: 2 casos aprobados en
  54 s, uno por navegador, sobre la implementación final.

Los logs completos permanecen en `build/port/`: `build48-native.log`,
`build48-web-final.log`, `build48-native-final.log`, `card48-native-final.log`,
`ctest48-native-clean.log`, `ctest48-web.log`,
`ctest48-final-checks.log`, `native-cards48.log`, `title48-web.log`,
`title48-audio-web.log` y `cards48-real-verified.log`.

## Incidencias investigadas

- El cierre real de página podía abortar el commit automático de IndexedDB antes
  del siguiente turno del event loop. Se conserva la conexión preparada y se
  llama `tx.commit()` explícitamente dentro del evento; la prueba navega realmente
  fuera de la página cinco veces por navegador con todos los avisos retenidos.
- El helper de fixtures confundía mensajes de progreso con finalización al sembrar
  un Pack con su SHA-256. Ahora espera el mensaje final antes de terminar el worker.
- La primera pasada completa de web encontró el antiguo aserto de tarjeta ausente
  del test unitario; se retiró junto con la rama que anulaba `card_image_attach`.
- Durante esa misma pasada se reconstruyó WASM mientras `web_boot` lo servía.
  Firefox recibió un archivo incompleto (`expected magic word`). La comprobación
  afectada se repitió sobre artefactos fijos; no se agregaron reintentos de Firefox.
- La primera pasada nativa encontró referencias de audio incompletas y una tarjeta
  privada faltante en los artefactos preexistentes. La pasada completa sin disco
  apartó temporalmente el Pack generado y lo restauró al terminar: 85 tests,
  60 aprobados, 25 omitidos por disco y cero fallos.
- `web_input` detectó una resincronización en Firefox (VBlank 42, retraso de
  77.221.687 ns) mientras coincidían varias suites. `resource-contention.json`
  conserva el límite de 2,5 CPU y los contadores de throttling del contenedor.
  La carga es una hipótesis para ese fallo; no se cambiaron el reloj ni los
  umbrales del test, ni se configuraron reintentos automáticos.
- Una importación pendiente se espera antes de adjuntar las tarjetas. Una prueba
  retiene la lectura del archivo mientras comienza el arranque y verifica que la
  imagen adjuntada contiene los bytes importados.
- Los dos casos con Pack sintético encontraron el sidecar SHA-256 del Pack real
  preexistente y mostraron «Pack desactualizado» en ambos navegadores. Clase A
  ahora responde 404 al endpoint opcional de ese sidecar, igual que las pruebas
  existentes de la Shell, para comprobar sus propios datos sintéticos sin depender
  de los artefactos de clase B.

## Verificación final en el PC remoto (20 CPU, con disco)

Sobre el árbol con los parches de revisión:

- `ctest --preset web`: 54/54 en 2757 s, sin omisiones (`build/port/final48-web.log`). Incluye `web_cards`, `native_cards`, `web_cards_real`, `web_disc_real`, `web_scene_title`, `web_scene_title_audio` y `web_input`.
- `ctest --preset native-headless -j6`: 85/85 en 6781 s, sin omisiones.
- `web_cards` además corrió 3 veces seguidas: 26/26 en cada una. Tras los parches de revisión, 26/26 en el contenedor local y en la pasada final.
- `cards_redux`, manual: 2/2 (`verified-*.json`). Web carga el `.mcd` de Redux y da el frame nativo; Redux carga un `.mcd` guardado y exportado por web, con estado idéntico tras `loadSavegame` al de la tarjeta nativa. Corrió antes de los parches de revisión, que no tocan ese recorrido salvo el texto del aviso.
- Recargas: 20 por navegador, máximos de 80 ms en Chromium y 155 ms en Firefox, con `#instance` oculto en todas.
- `make compare` no corrió: sin cambios en `src/`, `include/` ni `.gitlab-ci.yml`.

Fallos intermitentes de Firefox: ni `#play` deshabilitado ni `Target crashed` aparecieron en estas corridas, así que siguen sin causa identificada. La resincronización de `web_input` no se repitió con 20 CPU; sigue siendo hipótesis que venga de la carga.

## Desvío medido

Con el envío forzado solo en `pagehide`, Chromium no persistió el `put` de 128 KB (entrada nula) y Firefox sí. Por eso `flush` corre también en `beforeunload`. Causa sin verificar. Sin `beforeunload` (proceso matado), Chromium puede perder una escritura cuyo aviso aún no se atendió, y queda el estado anterior, válido.
