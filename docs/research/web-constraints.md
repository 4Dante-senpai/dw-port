# Restricciones de la plataforma web

Fecha: 2026-10-11. Toolchain: emsdk 6.0.10, SDL3 3.4.16 (imagen `dw-port`). Lo marcado
**medido** sale de pruebas en este repo; lo marcado **a verificar** es conocimiento previo que hay
que confirmar en navegadores reales (Chrome y Firefox de escritorio) antes de depender de él.

## 1. El problema: el juego bloquea

El loop de DW1 espera de forma bloqueante: `VSync` (15 llamadas desde el juego), `DrawSync`,
`CdReadSync`, lecturas síncronas de overlays (`readFile` en `loadDynamicLibrary`) y esperas
activas sobre flags que pone un callback. Además usa `setjmp`/`longjmp` (36 `longjmp` en `main`,
ver `psyq-usage.md`). En el navegador el hilo principal no puede bloquear: hay que devolver el
control al event loop cada frame.

## 2. Opciones de threading

Prueba **medida**: `tools/port/env_check/web_models/loop.c`, un loop de 60 frames con un `VSync()`
bloqueante de 16,7 ms y un `longjmp` desde 8 niveles de profundidad por frame, compilado de tres
formas (`tools/port/env_check/web_models/run.sh`, `-O2`, ejecutado en node 24):

| Modelo | Flags | `.wasm` | Resultado |
|---|---|---:|---|
| `PROXY_TO_PTHREAD` | `-pthread -sPROXY_TO_PTHREAD -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`; `VSync` = `nanosleep` | 30.720 B | OK: 60 frames, 60 longjmp, 1012 ms |
| ASYNCIFY | `-sASYNCIFY -sSUPPORT_LONGJMP=emscripten`; `VSync` = `emscripten_sleep` | 22.562 B | OK: 60 frames, 60 longjmp, 976 ms |
| JSPI | `-sJSPI -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`; `VSync` = `emscripten_sleep` | 9.479 B | OK solo con `node --experimental-wasm-jspi` |

Análisis:

- **`PROXY_TO_PTHREAD`**: `main()` corre en un Web Worker y puede bloquear de verdad (como el hilo
  de juego de DW2-PC). El hilo principal queda para el canvas, input y audio. Requiere
  `SharedArrayBuffer`, que solo existe con aislamiento cross-origin (COOP/COEP, sección 6). Sin
  costo por función instrumentada; el código del juego no cambia. Contras: la presentación tiene
  que ir del worker al hilo principal (OffscreenCanvas o copia del framebuffer por memoria
  compartida), y SDL3 en Emscripten tiene limitaciones de video fuera del hilo principal (**a
  verificar** con SDL 3.4.16).
- **ASYNCIFY**: transforma toda función que pueda llegar a un punto de suspensión para poder
  desenrollar y reanudar la pila. En el juego eso es casi todo el árbol de llamadas desde el loop
  principal. Costo típico: código más grande y más lento (el overhead real hay que medirlo con el
  juego completo en M4; en este modelo de juguete no es representativo). No combina con
  `-sSUPPORT_LONGJMP=wasm`; usa el longjmp por JS (más lento).
- **JSPI** (JavaScript Promise Integration): suspende la pila wasm sin instrumentar el código; el
  `.wasm` es el más chico. Node 24 todavía lo tiene detrás de un flag. Soporte en navegadores
  (**a verificar**): Chrome lo habilitó por defecto en 2025; el estado en Firefox estable debe
  confirmarse antes de elegirlo, porque el PRD exige Chrome **y** Firefox.

**Recomendación:** `PROXY_TO_PTHREAD` como modelo principal: es el único que hoy está disponible
en ambos navegadores sin instrumentar el código, reproduce el modelo de hilos ya probado de
DW2-PC y deja el hilo principal libre para presentar a 59,94 Hz. Mantener JSPI como alternativa a
evaluar cuando Firefox lo tenga estable (evitaría COOP/COEP). ASYNCIFY solo como último recurso.
Decisión final en el ADR de threading.

## 3. Ritmo de frames

- El navegador presenta con `requestAnimationFrame` a la frecuencia del monitor (60, 120, 144 Hz),
  no a 59,94 Hz. El juego debe avanzar con su propio reloj de VBlank (como `host/vblank.c` de
  DW2-PC: deadline `k * 1001/60000 s` sin deriva) y el hilo principal muestra el último frame
  completo.
- Con `PROXY_TO_PTHREAD` el reloj vive en el worker (`nanosleep` / `emscripten_futex_wait` con
  `performance.now()` como base); el hilo principal solo presenta.
- Pestaña en segundo plano: los navegadores frenan `requestAnimationFrame` y los timers; el audio
  sigue si hay un AudioWorklet activo. Comportamiento esperado: pausar el juego al ocultar la
  pestaña (Page Visibility API).

## 4. Audio

- **AudioWorklet** a 44,1 kHz (o la frecuencia del `AudioContext`, típicamente 48 kHz, con
  resampleo). El SPU emulado (`backend/psxspu.c` de DW2-PC) produce muestras por VBlank en el hilo
  del juego; un ring buffer en `SharedArrayBuffer` las pasa al worklet (patrón sin locks,
  productor/consumidor).
- El `AudioContext` solo arranca después de un gesto del usuario (política de autoplay): el botón
  "Jugar" de la shell lo crea.
- SDL3 en Emscripten tiene backend de audio propio (**a verificar** si usa AudioWorklet o
  ScriptProcessor en 3.4.16); si usa ScriptProcessor (deprecado, corre en el hilo principal) se
  reemplaza por un worklet propio en `port/web/`.

## 5. Almacenamiento

| | IndexedDB | OPFS (Origin Private File System) |
|---|---|---|
| Modelo | Base clave/valor, transacciones asíncronas | Sistema de archivos privado del origen |
| Archivos grandes | Blobs; lectura parcial incómoda | `FileSystemSyncAccessHandle` en workers: lectura/escritura **síncrona** por offset |
| Uso natural en el port | Saves (`.mcd` de 128 KB), settings | Pack del juego (cientos de MB), lectura por sector desde el hilo del juego |
| Soporte (**a verificar**) | Universal | Chrome, Firefox y Safari actuales; `createSyncAccessHandle` solo en workers |

**Recomendación:** pack en **OPFS** (el hilo del juego, que es un worker con `PROXY_TO_PTHREAD`,
lee sectores con `FileSystemSyncAccessHandle.read()` de forma síncrona, que encaja con
`CdRead` bloqueante) y saves/settings en **IndexedDB** (o también OPFS; lo importante es
`navigator.storage.persist()` para que el navegador no borre los datos). Import/export de
memory cards como descarga/subida de `.mcd` crudos de 128 KB.

Tamaños medidos del disco USA del usuario (para dimensionar cuota): imagen `.bin` 381.771.936 bytes;
archivos extraídos por `dumpsxiso` en `disks/us/`: 350.264.816 bytes (ver `hardware-access.md`,
sección 5, para el conteo por directorio). El pack ocupará del orden de 350-380 MB en OPFS.

## 6. Lectura del disco del usuario en el navegador

- `<input type="file">` (o drag-and-drop) entrega un `File`; se lee por partes con
  `File.slice()` sin cargar 380 MB en memoria.
- `.cue` + `.bin`: el usuario puede elegir ambos (`multiple`) o solo el `.bin`; validar que la
  pista 1 sea `MODE2/2352`.
- Validación contra el manifest: SHA-256 por archivo con `crypto.subtle.digest` (async, no
  streaming) o una implementación SHA-256 en wasm (streaming por bloques; DW2-PC ya tiene
  `host/sha256.c`).
- La construcción del pack (parseo ISO9660, extracción de archivos, sectores Form 2 crudos para
  XA/STR) puede ser el mismo código C del port compilado a wasm, corriendo en un worker, para que
  nativo y web generen el mismo pack.
- Nada del disco sale del navegador: no hay upload al servidor.

## 7. COOP / COEP

`SharedArrayBuffer` (y por lo tanto pthreads de Emscripten) requiere que la página esté
"cross-origin isolated":

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

- Todo recurso de otro origen (fuentes, CDNs) debe venir con `Cross-Origin-Resource-Policy` o
  CORS; lo más simple es servir todo desde el mismo origen.
- nginx (M7): agregar ambos headers en el `server` del deploy; servir `.wasm` como
  `application/wasm` (compilación en streaming).
- Hosts estáticos que no permiten headers: existe el workaround de un service worker que los
  inyecta (`coi-serviceworker`); no hace falta en el servidor propio.
- El service worker de la shell (offline tras la primera carga, requisito del PRD) es compatible
  con COOP/COEP si las respuestas cacheadas conservan los headers.

## 8. Memoria y punteros

- WASM32: punteros de 32 bits, little-endian: los structs del juego conservan su layout (igual que
  PS1). **Medido:** 97 de los 120 archivos C del juego (USA) compilan a objeto wasm32 sin cambios
  con los headers PsyQ de upstream (`tools/port/metrics/wasm_compile_check.sh us`:
  `files_ok=97 files_with_errors=23 errors=103`). Los 23 restantes fallan solo por el inline asm
  MIPS de las macros GTE (`gte_ldv0`, `gte_rtps`, ...: 11 macros distintas, 181 usos en 22
  archivos) y por `src/main/_psstart.c` (crt0, que el port reemplaza). Se resuelven con headers
  PsyQ propios del port, sin tocar `src/`.
- La memoria lineal de wasm empieza en 0. Los enlaces de OT de 24 bits (`addr & 0xFFFFFF`) se
  pueden resolver directamente si toda la memoria que entra en OTs está bajo 16 MB de la memoria
  lineal. Fijar `-sINITIAL_MEMORY` y ubicar la arena PS1 (2 MB) y los buffers de primitivas en
  los primeros 16 MB (a diseñar en el ADR de memoria).
- `char` es `signed` por defecto en clang/wasm; CodeWarrior PS1 lo trataba como **a verificar**
  (DW2-PC usa `-funsigned-char` porque el GCC de PsyQ usa `unsigned`). La prueba de compilación usó
  `-funsigned-char`; confirmar con CodeWarrior R4 o con diffs de comportamiento.

## 9. Tamaño de descarga

Sin medición del juego todavía (M4). Referencias para fijar el presupuesto: `SLUS_010.32`
(675.840 bytes) + los 16 `*_REL.BIN` (1.282.480 bytes) suman ~1,96 MB de MIPS, código y datos; el `.wasm` del juego más la capa PsyQ/backend debería quedar en
el mismo orden (medir con `-O2`/`-Os`, `wasm-opt`, y servir con Brotli). ASYNCIFY lo agrandaría;
`PROXY_TO_PTHREAD` no.
