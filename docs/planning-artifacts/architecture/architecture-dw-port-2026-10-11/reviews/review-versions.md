# Revisión de versiones y realidad técnica — Architecture Spine DW1 Web Port

- **Fecha:** 2026-10-11
- **Lente:** cada decisión comprometida fue investigada en la web o contrastada con el proyecto, no
  afirmada de memoria (versiones actuales, que cada tecnología exista y encaje, y los defaults de
  los que depende).
- **Objeto:** `ARCHITECTURE-SPINE.md` (no se editó) + `.memlog.md`, `tools/port/Dockerfile`,
  `docs/research/web-constraints.md`, `docs/port/setup.md`, spikes en `tools/port/env_check/`.
- **Método:** corrí comandos en la imagen `dw-port:latest` local (`DOCKER_CONTEXT=default`),
  consulté `git ls-remote` / npm para las últimas versiones y @mdn/browser-compat-data 8.1.5
  (timestamp 2026-10-08) para el soporte en navegadores.

## Veredicto

Casi todo el stack existe y la mayoría de los supuestos se sostienen. Pero hay dos decisiones
comprometidas que la realidad ya contradice: (1) la condición que AD-7 pone para reevaluar
JSPI ya se cumplió hace casi tres meses, y (2) el build native i686 de AD-3 no se puede hacer
con la imagen fijada. Hay que corregir ambas antes de aprobar.

## Hallazgos

### V-1 [ALTA] AD-7: el disparador de JSPI ya se cumplió; la justificación de `PROXY_TO_PTHREAD` quedó vieja

- El spine dice "JSPI se reevalúa cuando Firefox estable lo active por defecto", y el memlog dice
  "Firefox bug 2044809 fixed for 153 (stable date unconfirmed)".
- **Realidad:** Firefox 153 salió el **2026-07-21**, y sus notas para desarrolladores dicen
  textualmente "JavaScript Promise Integration (JS-PI) is now enabled". BCD lista JSPI en
  Chrome 137 y Firefox 153, sin flags. Hoy Firefox estable es la 157 y la 158 sale el 2026-10-13.
  Emscripten 6.0.8 (2026-08-20) sacó JSPI de experimental.
  - https://developer.mozilla.org/en-US/docs/Mozilla/Firefox/Releases/153
  - https://bugzilla.mozilla.org/show_bug.cgi?id=2044809
  - https://whattrainisitnow.com/calendar/
  - https://raw.githubusercontent.com/emscripten-core/emscripten/main/ChangeLog.md (entrada 6.0.8)
- `web-constraints.md` §2 justifica la recomendación con que `PROXY_TO_PTHREAD` "es el único que
  hoy está disponible en ambos navegadores". Esa premisa ya es falsa.
- **No invalida** necesariamente `PROXY_TO_PTHREAD`. Sigue teniendo ventajas propias: el hilo
  principal queda libre, `FileSystemSyncAccessHandle` solo existe en workers, y el ring de audio
  usa SAB. Pero el ADR tiene que volver a decidirse con datos actuales.
- **Fix:** reescribir la regla de AD-7 y su justificación, comparando JSPI y
  `PROXY_TO_PTHREAD` con el soporte real de hoy. Hay que tener en cuenta que JSPI en el hilo
  principal no resuelve el acceso síncrono a OPFS (ver V-3) ni el `VSync` a 59,94 Hz sin bloquear
  la UI. Quitar el disparador ya cumplido, o reemplazarlo por un criterio medible (por ejemplo,
  costo de `COOP/COEP` en el deploy). Corregir la línea `(version)` del memlog.

### V-2 [ALTA] AD-3 / Stack: la imagen `dw-port` no puede compilar native i686

- El Stack dice "Clang 18.1.3 + `gcc-multilib` (i686)", y el memlog dice "Spike: clang -m32 +
  gcc-multilib on ubuntu noble builds and runs".
- **Medido en `dw-port:latest`:** `dpkg -l | grep -c multilib` da `0`. Ni `tools/port/Dockerfile`
  ni el `Dockerfile` de upstream instalan `gcc-multilib`. Corriendo
  `tools/port/env_check/overlay_sections/run.sh`, la parte wasm da OK en -O0/-O2, pero la parte
  native-i686 falla con `bits/libc-header-start.h: file not found`. El chequeo de "SKIP" del
  script no lo detecta, porque compilar `/dev/null` no necesita headers. El spike de `-m32` se
  hizo fuera de la imagen fijada y no es reproducible con ella. Eso va contra NFR-7 y contra
  "toolchain solo de la imagen `dw-port`" de AD-17.
- Además, el SDL3 nativo de la imagen (`/opt/sdl3/native`) es **x86_64**. El preset `native` de
  AD-17 (i686 con ventana SDL3) necesita un SDL3 i686, y para eso hacen falta las dev libs
  `:i386` por multiarch (X11/Wayland/ALSA/Pulse) o un SDL3 i686 con menos backends. Ni el spine ni
  el Dockerfile lo contemplan.
- **Fix:** agregar al Dockerfile `gcc-multilib` (o `libc6-dev-i386` + `lib32gcc-*-dev`) y un
  build de SDL3 con `-DCMAKE_C_FLAGS=-m32` (con `dpkg --add-architecture i386` y las dev libs
  `:i386` que hagan falta). Arreglar el chequeo de `run.sh` para que pruebe un `#include
  <stdio.h>`. Recién entonces marcar AD-3 como verificado. Si el costo del SDL3 i686 es alto,
  registrarlo como trade-off de AD-3: `native-headless` i686 sin SDL es barato, y el `native` con
  ventana es el caro.

### V-3 [MEDIA] AD-13: supuestos de OPFS que no se verificaron (modelo async y locks en Firefox)

- El soporte existe: `createSyncAccessHandle` está en Chrome 102 y Firefox 111, solo en dedicated
  workers (BCD; https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileHandle/createSyncAccessHandle).
- Hay dos detalles que el spine no recoge:
  1. `createSyncAccessHandle()` **devuelve una Promise**. El hilo del juego bajo
     `PROXY_TO_PTHREAD` bloquea y no vuelve al event loop, así que no puede esperar esa promesa
     en medio de un `CdRead`. El handle tiene que obtenerse antes de que arranque `main`, o desde
     otro hilo (como hace el backend OPFS de WasmFS).
  2. La opción `mode` (`read-only` / `readwrite-unsafe`) existe **solo en Chrome 121+**: Firefox
     y Safari no la soportan (BCD). En Firefox el handle es exclusivo, así que si hay una segunda
     pestaña, o si el Worker del Pack sigue con su handle abierto, se obtiene
     `NoModificationAllowedError`.
- **Fix:** agregar a AD-13 que (a) el handle del Pack se abre una sola vez, de forma asíncrona,
  antes de entrar a `main` (o con un hilo auxiliar), (b) el Worker del Pack cierra su handle
  antes de que lo abra el hilo del juego, y (c) se maneja con `plat_fatal` el caso de una segunda
  pestaña (o se impide con Web Locks).

### V-4 [MEDIA] Emulador de referencia: flags confirmados, pero no hay versión ni fidelidad de OpenBIOS

- Los flags existen: `-no-ui`/`-cli` (implican `-stdout` y `-lua_stdout`), `-dofile`, `-exec`,
  `-iso`, `-run`, `-bios`, `-testmode`, `-softgpu`
  (https://pcsx-redux.consoledev.net/cli_flags/).
- Pero el repo `grumpycoders/pcsx-redux` **no publica tags** (`git ls-remote --tags` vacío), y el
  spine no dice cómo fijar la versión. La doc tampoco dice si `-no-ui` evita el contexto OpenGL,
  y `setup.md` supone que necesita Xvfb.
- OpenBIOS es un BIOS reimplementado: usarlo como **oráculo** para frames, trazas SPU y tiempos
  de CD (AD-8, AD-15) mete divergencias que no vienen de DW1 (arranque, eventos, memcard por
  `libapi`).
- **Fix:** en el Stack, fijar PCSX-Redux por commit SHA. Agregar a AD-15 un supuesto explícito:
  las referencias se generan con OpenBIOS y se contrastan al menos una vez con BIOS real (fuera
  del repo) en M3, o se documenta qué Escenas dependen del BIOS.

### V-5 [BAJA] Versiones fijadas atrasadas o sin confirmar

| Ítem | Spine | Última verificada hoy | Fuente | Acción |
|---|---|---|---|---|
| Emscripten | 6.0.10 | 6.0.12 (2026-10-08) | `git ls-remote` emsdk; ChangeLog | Aceptable. 6.0.11/6.0.12 no tocan los flags usados. Subir en la próxima historia de toolchain. |
| SDL3 | 3.4.16 | 3.4.18 | `git ls-remote` libsdl-org/SDL | Aceptable (patch). |
| Playwright | 1.61 [ASSUMPTION] | **1.64.0** | https://registry.npmjs.org/playwright/latest | Actualizar a 1.64 o dejar "última al crear L3". Además, el Firefox de Playwright es un build propio: L3 no prueba Firefox estable. |
| Node (emsdk) | 24.19.0 | — | medido en la imagen | OK. Node 24 sigue con JSPI detrás de flag: los spikes de JSPI en node no representan a los navegadores. |
| Imagen base | Ubuntu 24.04.5 | — | medido | `ubuntu:noble` sigue sin digest. AD-16 pide la imagen "fijada por digest", y eso todavía no existe. |
| CMake / Ninja / Clang / Python | 3.28.3 / 1.11.1 / 18.1.3 / 3.12.3 | — | medido en la imagen | OK. |

### V-6 [BAJA] Defaults de navegador de los que dependen AD-13 y AD-7

- **IndexedDB:** la transacción es atómica: una Memory card de 128 KB en un solo `put` nunca queda
  a medias. Pero desde Chrome 121 la durabilidad por defecto es `relaxed`: un corte de luz puede
  perder el último save completo, aunque nunca lo corrompe
  (https://developer.chrome.com/blog/indexeddb-durability-mode-now-defaults-to-relaxed/).
  **Fix:** usar `{durability: 'strict'}` en la transacción de Memory card (Chrome 83+,
  Firefox 126+, según BCD).
- **COOP/COEP:** `require-corp` es correcto. Como alternativa existe `credentialless` (Chrome 96,
  Firefox 119; no Safari). No hace falta cambiar nada; conviene mencionarlo si en el futuro se
  sirven recursos de terceros.
- **Gamepad:** `navigator.getGamepads()` existe solo en `Navigator`, no en `WorkerNavigator`
  (BCD). El supuesto del spine (muestreo en el hilo principal) es **correcto**.
- **OffscreenCanvas + WebGL2 en workers:** Chrome 69 y Firefox 105 (BCD). Las dos opciones del
  spike diferido de AD-9 son viables.
- **AudioWorklet + SAB:** AudioWorklet está en Chrome 66 y Firefox 76. Con aislamiento
  cross-origin, el SAB se puede pasar por `port.postMessage`. OK. Emscripten trae
  `-sAUDIO_WORKLET` (Wasm Audio Worklets) como alternativa al worklet propio. No hace falta,
  pero el spine no lo evaluó.
- **Wasm EH / longjmp:** `-fwasm-exceptions` + `-sSUPPORT_LONGJMP=wasm` siguen soportados en 6.x
  (la entrada 6.0.7 del ChangeLog cambia cómo se linkea el runtime EH). Piso de navegador:
  exception-handling Chrome 95 / Firefox 100, exnref Chrome 137 / Firefox 131. OK para "Chrome y
  Firefox actuales". Se midió en node, no en navegador: agregarlo a L3a.
- **`-sINITIAL_MEMORY` sin growth:** no hay cambios en 6.x que lo afecten (6.0.12 solo toca
  `IMPORTED_MEMORY`). Con pthreads, la memoria fija es lo recomendado. OK.

## Confirmado contra el proyecto

- **wasm-ld `__start_`/`__stop_` para `dw1ovl_<ovl>`:** lo **re-ejecuté** en `dw-port`:
  `wasm -O0: section=356 bytes ptr=4 OK`, `wasm -O2: ... OK`. Se sostiene (no repetí el caso LTO).
- **Versiones del toolchain:** coinciden con la imagen: `emcc 6.0.10`, `node v24.19.0`,
  `clang 18.1.3`, `cmake 3.28.3`, `ninja 1.11.1`, `Python 3.12.3`, `Ubuntu 24.04.5 LTS`.
- **Flags de Emscripten de AD-7:** `-pthread`, no el `-sUSE_PTHREADS` deprecado en 6.0.1. Medidos
  por `web_models/run.sh` en node.

## Observación menor

`tools/port/Dockerfile` compila SDL3 para Emscripten (`/opt/sdl3/web`), pero el memlog decide que
el Host web no depende de SDL3. Es peso muerto en la imagen: quitarlo cuando se toque el
Dockerfile por V-2, salvo que se quiera como fallback.
