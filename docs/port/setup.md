# Entorno de desarrollo del port

Procedimiento verificado el 2026-10-11 (M0). Desde el 2026-10-12 el modo es **con compilador
matching**: `cc_mips.dll` de CodeWarrior for PlayStation Release 4 (regla 2 de `CLAUDE.md`).

## Requisitos del host

- Docker con daemon **local** (los comandos montan el repo con `-v "$PWD":/dw`). Si el contexto
  Docker activo apunta a otra máquina (`docker context ls`), anteponer `DOCKER_CONTEXT=default` a
  los comandos de abajo: con un daemon remoto el build funciona, pero los bind mounts ven el disco
  del servidor remoto, no este repo.
- `git`, `wget`, `unzip`, `sha256sum` (para `tools/dl_deps.sh`).
- La imagen del disco **propia** del usuario: Digimon World (USA), `.bin/.cue`, una pista
  `MODE2/2352`. Nunca se versiona (`.gitignore` y hook pre-commit).

## Imágenes Docker

| Imagen | Dockerfile | Contenido |
|---|---|---|
| `dw-build:latest` | `Dockerfile` (upstream, sin cambios) | Ubuntu 24.04, binutils/gcc `mipsel-linux-gnu`, Python + splat/spimdisasm, clang |
| `dw-port:latest` | `tools/port/Dockerfile` (`FROM dw-build`) | + CMake 3.28, Ninja 1.11, LLD, Clang 18, `gcc-multilib` (build nativo i686, AD-3), **emsdk 6.0.10**, **SDL3 3.4.16** x64 (`/opt/sdl3/native`), i686 (`/opt/sdl3/native32`) y Emscripten estático (`/opt/sdl3/web`), p7zip |

Versiones fijadas como `ARG` en `tools/port/Dockerfile` (`EMSDK_VERSION`, `SDL3_VERSION`). La base
`ubuntu:noble` de upstream no está fijada por digest (pendiente para build reproducible).

```sh
tools/port/docker_build.sh            # construye dw-build y luego dw-port
tools/port/docker_run.sh <comando>    # corre <comando> en dw-port con el repo en /dw, como el usuario del host
```

### Ventana nativa SDL3 (historia 3.9)

```sh
tools/port/docker_run.sh sh -c 'cmake --preset native && cmake --build --preset native && ctest --preset native'
tools/port/docker_run.sh --gui build/port/native/dw1 --pack game_rom/us.pack
```

`native` hereda las flags i686 de `native-headless` y busca SDL3 3.4.16 en
`$SDL3_NATIVE32_PREFIX`. `env_check.sh` compila, enlaza y ejecuta también el smoke
i686 con `-m32`. El preset de tests `native` selecciona las pruebas SDL y audio,
sin repetir toda la suite L1; `native_sdl_title_capture` agrega automáticamente
el fixture del Pack y sale con 77 si faltan datos privados.

`--gui` comparte los sockets disponibles de X11, Wayland y PulseAudio con el
contenedor. En WSLg usa `/tmp/.X11-unix`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`
y `PULSE_SERVER`, con valores por defecto bajo `/mnt/wslg`. Si no encuentra
ninguno, avisa. Si existen, también comparte `/dev/input` con la regla de
cgroup `c 13:* rmw` y `/run/udev` en solo lectura para detectar gamepads.
En WSL, primero hay que adjuntar el gamepad USB a la distribución con
`usbipd-win`; WSLg por sí solo no expone el dispositivo USB.

Sin pantalla, correr con `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy`.
`run.py title --build native` configura esos drivers, conserva la ruta de
captura de `crt.c` y verifica el SHA-256 de `title.toml` en dos corridas.
`build/port/native/dw1 --headless ...` acepta la misma CLI y corre sin SDL.
Si falla el audio, el juego continúa con un aviso; un fallo de video sale con 1.
Controles y límites del controlador de audio: [controls.md](controls.md).

`.dockerignore` deja fuera del contexto todo menos `requirements.txt`, así la imagen del disco
nunca se envía al daemon.

Daemon remoto para trabajo pesado: `tools/port/remote_run.sh <comando>` es igual a
`docker_run.sh`, pero corre en el contexto Docker `$DW_REMOTE` (por defecto `windows`:
Docker Desktop del PC del dueño, 24 CPUs y 15 GB, expuesto como `tcp://localhost:2375` a través
de un túnel SSH inverso que solo existe mientras la sesión de VS Code Remote está abierta).
Como ese daemon no ve este disco, el repo (incluidos `game_rom/` y `disks/`, que quedan solo en el PC del
dueño) se sincroniza con rsync al volumen `dw_remote_src` antes de correr y los cambios vuelven
al terminar (`rsync -u`: si un archivo cambió en los dos lados, gana la copia local). Si la
imagen falta en el remoto, se copia desde el contexto `default`. Si el remoto no responde, cae a
`docker_run.sh` en el contexto `default`. Límites por defecto: `DW_CPUS=20`, `DW_MEMORY=12g`.
Se usa un volumen y no un bind mount a una carpeta de Windows porque el bind mount es ~200x más
lento con archivos chicos (medido el 2026-10-20).

Mediciones del 2026-10-20 (remoto i7-13700K vs. local i5-6600K): sync incremental ~3 s (el primero
~3,5 min); un hilo ~15% más lento en el remoto (Python, 3 corridas); `native-headless` limpio 1 s
vs. 7 s; `make` + `make compare` limpio 2 s vs. 6 s. Conviene para trabajo paralelo (`ctest -j`,
varias escenas o capturas a la vez); una sola captura larga de un hilo no gana nada.

Devcontainer: `.devcontainer/port/devcontainer.json` (construye `dw-build` en
`initializeCommand` y luego `tools/port/Dockerfile`). El devcontainer de upstream
(`.devcontainer/devcontainer.json`) sigue igual.

## Pasos

1. Hooks de git (una vez por clon):

   ```sh
   tools/port/hooks/install.sh
   ```

2. Herramientas de upstream (mwccwrap, metrowrap, mkpsxiso, objdiff, wibo; verifica sha256):

   ```sh
   tools/dl_deps.sh
   ```

3. Imagen del disco. Si viene comprimida, extraerla dentro de `game_rom/` (ignorado por git):

   ```sh
   # ejemplo con el .7z del usuario
   tools/port/docker_run.sh 7z x -ogame_rom/extracted "game_rom/Digimon World.7z"
   ```

4. Volcado del disco con `dumpsxiso` (dentro del contenedor: el binario necesita glibc 2.38):

   ```sh
   tools/port/docker_run.sh bin/mkpsxiso-2.20-Linux/bin/dumpsxiso \
       -x disks/us -s disks/us/us.xml "game_rom/extracted/Digimon World.bin"
   sha1sum disks/us/SLUS_010.32   # debe dar 31ec666b987d5b007dec40eecc4092d27ed9085b
   ```

5. Pack del disco (Manifest versionado en `tools/port/manifest/us.tsv`; el Pack nunca se versiona,
   `*.pack` está en `.gitignore` y en el hook). `dw1-pack` valida todo el disco contra el Manifest
   (primero `SLUS_010.32` por LBA) y escribe el Pack solo si todo coincide; sale con 0 (OK),
   1 (falló una validación, el mensaje nombra el archivo) o 2 (uso):

   ```sh
   tools/port/docker_run.sh sh -c 'cmake --preset native-headless && cmake --build --preset native-headless --target dw1-pack'
   tools/port/docker_run.sh build/port/native-headless/dw1-pack build \
       "game_rom/extracted/Digimon World.cue" tools/port/manifest/us.tsv game_rom/us.pack
   tools/port/docker_run.sh build/port/native-headless/dw1-pack verify \
       game_rom/us.pack tools/port/manifest/us.tsv
   ```

   `dw1-pack manifest <imagen> <tsv>` regenera el Manifest (solo rutas, LBAs, sectores, modo,
   tamaño y SHA-256). El test clase B `pack_disc` lo compara con `us.tsv`, construye el Pack dos
   veces (mismo SHA-256) y lo verifica:

   ```sh
   tools/port/docker_run.sh sh -c 'cd build/port/native-headless && DW1_DISC_DIR=/dw/game_rom/extracted ctest -R pack_disc'
   ```

   Correr el Juego (`dw1`, headless) sobre el Pack. `dw1` solo abre un Pack construido con el
   Manifest de su propio build (el SHA-256 de `us.tsv` se compila en `dw1`); con otro sale con 1 y
   pide reconstruirlo. Sin `--pack` el disco está vacío (`CdSearchFile` no encuentra nada y las
   lecturas no escriben datos). La traza (`DW1_TRACE`, por defecto `port_trace.log`) registra la
   primera llamada a cada stub y cada carga de overlay
   (`<ciclo> overlay <id> <ruta> dest=<dir PS1> sha256=<hash>`):

   ```sh
   tools/port/docker_run.sh sh -c 'DW1_TRACE=build/dw1_trace.log build/port/native-headless/dw1 \
       --headless --pack game_rom/us.pack --until-overlay'
   ```

   `--frames N` sale con 0 tras N frames; `--until-overlay`, tras la primera carga de overlay. El
   test clase B `dw1_overlay` usa el Pack que deja `pack_disc` en el directorio de build
   (`us.pack`) y comprueba que el primer overlay (`MOV_REL.BIN`) queda en su `mov_START` con el
   SHA-256 del Manifest:

   ```sh
   tools/port/docker_run.sh sh -c 'cd build/port/native-headless && DW1_DISC_DIR=/dw/game_rom/extracted ctest -R "pack_disc|dw1_overlay"'
   ```

6. Desensamblado (splat, no necesita CodeWarrior):

   ```sh
   tools/port/docker_run.sh make -j$(nproc) regenerate
   ```

7. Build matching con `cc_mips.dll` (Release 4). El `.7z` del usuario trae una imagen raw de
   2352 B/sector: se convierte a ISO de 2048 B y se extrae
   `CodeWarrior PSX Pro/bin/plugins/Compiler/cc_mips.dll` (759.296 B, 1998-04-15, sha1
   `5320c0acb9b3be317f73b4fd1a1e1e555e029b2b`) a `bin/cc_mips/cc_mips_40.dll` (ignorado por git y
   bloqueado por el hook). La Release 5 trae otro `cc_mips.dll` (sha1 `20654adb…1c8c3a1897`) que
   upstream no usa.

   Corre en `dw-build`: en `dw-port`, `gcc-multilib` desinstala `gcc-mipsel-linux-gnu`
   (los paquetes chocan), y el build PS1 lo usa para los `.s`.

   ```sh
   DW_IMAGE=dw-build:latest tools/port/docker_run.sh sh -c 'make -j$(nproc) && make compare'
   ```

8. Opcional, diagnóstico sin CodeWarrior: build PS1 **no matching** con GCC que trae upstream.
   No reemplaza a `make compare`; sirve para comprobar que el C compila con otro compilador.

   ```sh
   tools/port/docker_run.sh sh -c 'make TOOLCHAIN=gcc generate && make -j$(nproc) TOOLCHAIN=gcc'
   ```

9. Smoke test del toolchain del port (SDL3 nativo con Clang y web con Emscripten, ejecutado en
   node):

   ```sh
   tools/port/docker_run.sh tools/port/env_check.sh
   ```

## CI (GitLab, AD-16)

`.gitlab-ci.yml` corre en el runner Docker del GitLab propio. Los hosts reales están en
`docs/port/local-infra.md` (no versionado; el repo de GitHub es público):

| Job | Clase | Imagen | Qué hace |
|---|---|---|---|
| `native` | A | `dw-port` | build `native-headless` + CTest (los tests de disco se saltean) |
| `native-disc` | B | `dw-port` | CTest `pack_disc` y `dw1_overlay` con `DW1_DISC_DIR=/private/disc` |
| `matching` | B | `dw-build` | `dumpsxiso` + `make regenerate` + `make` + `make compare` |

Los jobs usan `dw-build` y `dw-port` del registry del proyecto
(`$CI_REGISTRY_IMAGE`), fijadas por digest en `.gitlab-ci.yml`. El job
manual `images` (también automático si cambia un Dockerfile o `requirements.txt`) las construye en
el docker del runner, las publica con el `CI_JOB_TOKEN` e imprime los digests nuevos, que se
copian al YAML en un commit. Los jobs de clase B llevan el tag `dw1-data` y solo corren en
pipelines `push`, `web` o `schedule`.

### Preparar el runner (procedimiento usado el 2026-10-12)

Se repite si se reinstala el runner. Si solo cambia un Dockerfile, alcanza con el job `images`.
Los pasos que copian datos del disco o tocan el runner los corre el dueño: el clasificador de
permisos de Claude Code no los deja hacer al agente.

1. **Acceso SSH temporal** con la clave de esta máquina. El CT del runner no traía `sshd`: se
   instaló desde el nodo Proxmox (`pct exec <CT> -- apt-get install -y openssh-server`), se
   habilitó `PermitRootLogin yes` y, desde una terminal interactiva (no con `!` en Claude Code,
   que no puede leer la contraseña):

   ```sh
   ssh-copy-id -i /root/.ssh/id_rsa.pub root@<runner-host>
   ```

2. **Disco del CT:** las dos imágenes ocupan ~4,4 GB (`dw-port` contiene a `dw-build`). El rootfs
   de 12 GB estaba al 91 %; se amplió a 40 GB desde el nodo: `pct resize <CT> rootfs +30G`.
3. **Registry por HTTP:** el registry de GitLab no tiene TLS; el docker del runner lo acepta con
   `insecure-registries` en `/etc/docker/daemon.json` y `systemctl restart docker`. Validar el
   JSON antes de reiniciar (`python3 -m json.tool /etc/docker/daemon.json`): un archivo inválido
   deja a docker caído y, tras tres intentos, systemd deja de reintentar (`systemctl reset-failed
   docker && systemctl start docker`). Agregar el registry a la lista que ya tenga:

   ```json
   { "insecure-registries":["<gitlab-host>:5050"] }
   ```

4. **Datos privados**, fuera de cualquier repo:

   ```sh
   ssh root@<runner-host> 'mkdir -p /srv/dw-port-private/disc /srv/dw-port-private/mwcc'
   scp game_rom/extracted/Digimon\ World.bin game_rom/extracted/Digimon\ World.cue root@<runner-host>:/srv/dw-port-private/disc/
   scp bin/cc_mips/cc_mips_40.dll root@<runner-host>:/srv/dw-port-private/mwcc/
   ssh root@<runner-host> 'chmod -R a+rX /srv/dw-port-private'
   ```

5. **`config.toml`** (`/srv/gitlab-runner/config/config.toml` en el host; el runner lo recarga
   solo). En `[runners.docker]`, con backup previo:

   ```toml
   volumes = ["/cache", "/var/run/docker.sock:/var/run/docker.sock", "/srv/dw-port-private:/private:ro"]
   ```

   El montaje `/private` lo ven todos los jobs del
   runner; no baja el aislamiento, porque todos montan `docker.sock` (root en el host).
6. **Tag** `dw1-data` en el runner (Admin → CI/CD → Runners → Edit), manteniendo "Run untagged
   jobs". Correr el job `images`, fijar los digests y comprobar los tres jobs del pipeline.
7. **Cierre (opcional):** quitar la clave (`sed -i '/<comentario-de-la-clave>/d' /root/.ssh/authorized_keys`) y
   volver `PermitRootLogin prohibit-password` + `systemctl restart ssh`.

Antes de `.gitlab-ci.yml`, GitLab corría Auto DevOps en cada push (jobs fallidos sin relación).

## Resultado de la verificación (2026-10-11)

| Paso | Resultado |
|---|---|
| `tools/dl_deps.sh` | OK, 6 sha256 verificados |
| `dumpsxiso` | OK; `SLUS_010.32` sha1 `31ec666b…5b` = `config/us/main.yaml` |
| `make regenerate` | OK (≈10 s, 4 núcleos) |
| `make TOOLCHAIN=gcc` | OK: `SLUS_010.32` + 15 `*_REL.BIN` |
| `make` / `make compare` | OK (2026-10-12, `dw-build`, build limpio): 120 TUs con `cc_mips_40.dll`, `Compiled binaries match with target binaries` |
| `tools/port/env_check.sh` | OK: `SDL 3.4.16, platform Linux, sizeof(void*)=8` y `SDL 3.4.16, platform Emscripten, sizeof(void*)=4` |

Criterio de salida de M0 en modo sin compilador: `make regenerate` OK y entorno del port
funcionando → **cumplido**.

## Emulador de referencia

El Dockerfile de `dw-port` compila PCSX-Redux y OpenBIOS desde un SHA fijo
en una etapa separada. Solo los artefactos de runtime pasan a la imagen final.
La captura automática de la historia 3.1 usa Xvfb y Lua; consultar
[referencia](reference.md) para uso, límites y procedimiento con BIOS propio.
La referencia OpenBIOS es provisional hasta contrastarla con el BIOS del dueño.

## Web: hilo del juego y presentación (Historia 4.1)

Dentro del contenedor, con el entorno de emsdk cargado:

```sh
cmake --preset web
cmake --build --preset web -j8
ctest --preset web
python3 port/web/dev/serve.py --build build/port/web --port 8080
```

Abrir `http://localhost:8080/?frames=1`; `&pattern=1` sustituye el display por un
patrón RGB15 para comprobar colores, orientación y tamaño. El servidor agrega COOP
`same-origin` y COEP `require-corp`. `/isolated-off/` omite ambos: la página muestra
`cross-origin isolation required` antes de solicitar el runtime. Este servidor es
exclusivamente de desarrollo y escucha en loopback.

El ejecutable `dw1.js` enlaza todo el juego, con pthreads, excepciones WASM y
`SUPPORT_LONGJMP=wasm`. La memoria queda fija en 64 MiB, sin crecimiento; se conserva
`-fsigned-char`, sin LTO. El hilo principal devuelve el control al navegador; el
hilo del juego usa la pila estática de 4 MiB y comprueba la dirección de una variable
local dentro de la ventana OT de 16 MiB. El canvas se transfiere a ese mismo hilo.
`plat_present` sube el display software como textura RGBA a WebGL2, con filtro nearest.

`web_boot` usa Playwright 1.64.0 instalado en `/opt/node_modules` y los navegadores
de `/opt/ms-playwright`. Chromium usa SwiftShader; Firefox corre con Xvfb (el modo
headless de este contenedor no creó un contexto WebGL2). No se toman capturas de
pantalla: `glReadPixels` compara cada píxel y comunica cantidad y primer índice
diferente; Playwright verifica el DOM y esos valores. Los tiempos de presentación
se guardan en `build/port/web/present-timing.json`, sin umbral de aceptación y sin
incluir el readback de validación. El primer tiempo incluye la creación del contexto.

`fnptr_inventory` analiza los 119 TU con el frontend Emscripten fijado (también
al correr desde presets nativos), inventaría casts, argumentos de callback y
llamadas sin prototipo o con aridad distinta. Compara el resultado con
`tools/port/metrics/fnptr_casts.tsv` y rechaza drift o resoluciones vacías. En web
recompila además las declaraciones originales con `DW1_ABI_AUDIT` y compara
los diagnósticos de firmas de wasm-ld. El enlace de producción trata las
advertencias como errores. Para actualizarlo tras revisar un cambio intencional:

```sh
python3 tools/port/fnptr/inventory.py build/port/web --update
```

Las llamadas indirectas se resuelven con `-sEMULATE_FUNCTION_POINTER_CASTS=1`;
no hay adaptadores de callbacks en `src/`. La emulación agrega código y coste de
despacho, sin presupuesto de rendimiento en esta historia. `native-asan` mantiene
`-fno-sanitize=function`. Los cambios `DW1_PORT` corrigen solamente las ABI directas
rechazadas por el linker, conservando los parámetros consumidos. Los stubs PsyQ se
generan desde prototipos reales y fallan si falta uno o si una firma variádica
requiere implementación explícita.

El patrón cambia con el frame: R5=(x+f)&31, G5=(y+2f)&31,
B5=(x+y+3f)&31; cada canal se expande con `(v<<3)|(v>>2)`. Usar
`?frames=3&pattern=1&validate=1` para validar varias presentaciones. Solo
`--validate` (`validate=1`) habilita `glReadPixels`, `preserveDrawingBuffer` y
`Module.presentations`; las pruebas contrastan cada píxel con un oráculo RGB15
independiente en JavaScript. Las corridas normales no acumulan muestras.
El build deja `dw1.html`, `dw1.js` y `dw1.wasm` en `build/port/web`.

Limitación pendiente: `script_interp.c` llama al `setActiveAnim` original con un
solo argumento aunque su implementación consume dos. El port conserva el no-op
cuando no existe un NPC con scriptId 12 y aborta si ese NPC existe; no inventa una
animación. Esto no afecta el frame 0 probado, pero la fidelidad de esa ruta de
scripts queda incompleta hasta reconstruir el valor de a1 del binario original.

### Worker de E/S y título web (historia 4.2)

El host de desarrollo admite `?pack=1`: antes del arranque, un worker dedicado
abre `dw1/us.pack` en OPFS y recibe la memoria wasm compartida. El juego solicita
rangos por un buzón y duerme con `Atomics.wait`. Nunca se carga el Pack completo
en la memoria wasm de 64 MiB. El formato y el SHA del Manifest los comprueba el
mismo `pack_open` que usa el ejecutable nativo. Un Pack ausente o inválido deja
el error en `#status` y no ejecuta el juego.

Dentro del contenedor con el toolchain instalado:

```sh
cmake --preset web
cmake --build --preset web
ctest --preset web
```

`web_io` es clase A: genera bytes sintéticos con la fórmula de `test_pack.c`,
comprueba cuatro lectores concurrentes, errores de rango, esperas acotadas con
respuestas demoradas, Pack ausente, header corrupto, Manifest ajeno e índice
corrupto. `web_boot` conserva sus pruebas existentes. Sin `DW1_DISC_DIR`, los
fixtures privados y `web_scene_title` salen con 77.

Para clase B, primero compilar el preset `native-headless`; sus herramientas
preparan el Pack y `tools/port/scenes/run.py title` produce dos capturas nativas
idénticas como fixture de CTest:

```sh
cmake --preset native-headless
cmake --build --preset native-headless
DW1_DISC_DIR=/dw/game_rom/extracted ctest --test-dir build/port/web -R web_scene_title --output-on-failure
```

Playwright copia el Pack a OPFS por streaming desde el servidor local de pruebas,
cierra el escritor y luego arranca el worker lector. Chromium y Firefox comparan
el SHA-256 del PPM con la captura nativa, con y sin `?poison=1`. Este modo instala
un getter que falla ante cualquier acceso a `navigator.storage` en los pthreads.
`?capture-mark=runLandingScreen&capture-vblank=60&frames=10000&pack=1` publica el
PPM en `Module.capture` y el resultado final en `Module.finished` (0 éxito).
Los PPM y Packs solo quedan en `build/`; los JSON del título contienen hashes,
modo de prueba y conteos. El recorrido con disco propio se describe a continuación.

El build del port genera copias de todas las fuentes del Juego con literales CP932 en escapes octales (`tools/port/sjis_literals.py`), conservando la ruta original mediante `#line` y sin modificar `src/`.


### Disco propio y Pack en OPFS (historia 4.3)

Abrir `/` permite seleccionar un `.bin` solo, o su `.bin` y `.cue`, y también
arrastrarlos sobre el selector. El worker monta los `File` con WORKERFS y llama
la misma CLI C que el port nativo. El Manifest público es `/us.tsv`, copiado de
`tools/port/manifest/us.tsv`. El módulo `dw1_pack.js`/`dw1_pack.wasm` usa memoria
fija de 64 MiB, sin pool de pthreads. No carga la imagen ni el Pack completos.
Las lecturas se hacen por partes y el nodo de salida escribe directamente a un
handle síncrono de `dw1/us.pack.tmp`. Tras validar y calcular el SHA con el mismo
C, mueve el archivo a `us.pack` y publica `us.pack.sha256`. Conserva temporalmente
el Pack anterior para restaurarlo si falla la publicación del sidecar.

Al terminar aparece «Listo para jugar» y se solicita persistencia sin bloquear
«Jugar». La denegación muestra un aviso. Recargar reconoce Pack y sidecar;
«Borrar datos» elimina el directorio `dw1/`. El cargador del fixture de testing
se muestra únicamente cuando responde `/__fixture/us.pack.sha256`.

Dentro del contenedor, sin disco (clase A y comprobación del salto clase B):

```sh
cmake --preset web
cmake --build --preset web
ctest --preset web
```

`web_disc` genera un disco sintético de 32 sectores, obtiene su Manifest con
`dw1-pack manifest` en wasm y sustituye `/us.tsv` mediante Playwright. Comprueba
selección, arrastre, errores del C, persistencia denegada, recarga, borrado y
conservación del Pack previo ante errores de validación o escritura. Chromium
usa el navegador completo y `ForceEagerMeasureMemory` para medir con
`performance.measureUserAgentSpecificMemory()`; Firefox registra memoria nula
porque no expone esa API. La prueba sintética pausa el worker antes del
constructor C para incluir su heap en la muestra. El umbral comprobado en
Chromium es 512 MiB.

Clase B con el disco privado (no se publica ni se agrega a la evidencia):

```sh
cmake --preset native-headless
cmake --build --preset native-headless
DW1_DISC_DIR=/dw/game_rom/extracted ctest --preset web -R 'pack_disc|web_disc_real' --output-on-failure
ctest --preset native-headless
cmake --preset native-asan
cmake --build --preset native-asan
ctest --preset native-asan
```

`web_disc_real` requiere `DW1_DISC_DIR`; sin él sale con 77. Su fixture prepara
el Pack nativo y el test vuelve a construirlo en ambos navegadores desde los
archivos elegidos. Reabre el archivo OPFS, recalcula su SHA por partes y compara
archivo y sidecar contra el SHA nativo conocido. Los JSON `disc-*.json` y
`disc-real-*.json` en `build/port/web/` contienen memoria máxima, hashes y el
tiempo desde la selección hasta «Listo para jugar» (SM-4, milisegundos). Solo se
copian esos metadatos a `docs/implementation-artifacts/evidence/story-4-3/`.

### Shell web: capacidades, instancia única y pausa (historia 4.4)

`dw1.html` verifica primero aislamiento (`crossOriginIsolated`), `SharedArrayBuffer`,
OPFS y Web Locks; si falta alguna, muestra un mensaje por capacidad
(`#capabilities li[data-capability]`) y no carga `dw1.js` ni crea workers. Después
toma el Web Lock `dw1-game` durante toda la vida de la página: una segunda pestaña
muestra `#instance`, no arranca nada (tampoco construye ni borra el Pack) y continúa
sola cuando la primera se cierra. «Jugar» crea el `AudioContext` dentro del clic y
arranca el juego en la misma página (sin navegar), con la corrida finita de 300 frames.

El hilo del juego tiene ritmo de pared en cada `VSync` que presenta un frame
(`port/host/web/pace.c`, mismo cálculo que `port/host/sdl/video.c`; el ancla es la
primera presentación), salvo en corridas de captura de Escena, que no muestran «Pausa».
La pausa actúa en el siguiente frame presentado: una carga larga sin presentar no se
retiene hasta terminar. «Pausa» o la pestaña oculta escriben la palabra de pausa
compartida; el hilo queda retenido dentro de `VSync`, sin despachar VBlanks, y al
reanudar rebasa el deadline sin contar resincronización. `Module.control()` expone
`pause`, `held`, `vblanks`, `resynchronizations` y `pauses` para los tests, y
`Module.stall(ms)` atrasa el siguiente frame para probar la resincronización.

```sh
ctest --preset web -R web_shell --output-on-failure
python3 port/web/tests/run.py build/port/web shell --project chromium   # un navegador
```

La suite `shell` (clase A) cubre las cuatro capacidades, la segunda pestaña, la
pausa manual, la pestaña oculta (simulada redefiniendo `document.visibilityState`)
y «Jugar» como único gesto. Como `page.evaluate` de Playwright concede activación de
usuario, ese caso no evalúa nada en la Shell antes del clic: un init script registra
la activación al construir cada `AudioContext` y un contexto de control creado al
cargar debe quedar `suspended`. Firefox corre con `media.autoplay.*` bloqueando Web
Audio sin gesto. Sin servidor de audio, Firefox deja todo `AudioContext` en `suspended`,
así que `run.py` levanta un PulseAudio con sink nulo para cada suite. El hilo del juego
nunca vuelve a su bucle de eventos, así que cada frame llega al hilo principal como
memoria compartida y se muestra en `#screen`; un caso comprueba que cambia mientras corre. Deja `shell-{pause,visibility,play}-<navegador>.json` en
`build/port/web/`. La clase B (`web_scene_title`) agrega un clic en «Jugar» con el Pack
real que debe llegar a `runLandingScreen` con el `AudioContext` en `running`.

### Input web (historia 4.5)

```sh
ctest --preset web -R web_input --output-on-failure
python3 port/web/tests/run.py build/port/web input --project chromium   # un navegador
```

La suite `input` (clase A, sin Pack) pulsa los 14 botones por teclado y por un gamepad
simulado (reemplaza `navigator.getGamepads`) y compara `Module.input().padValue` (lo que
devolvería `PadRead`, vía `port_pad_value`) con el mapeo de `docs/port/controls.md`.
Cubre combinación, gatillos, stick como cruceta, conexión en caliente con ritmo y sin
resincronizaciones, foco perdido y capturas sin input. Deja `input-*-<navegador>.json`.

`web_scene_title` (clase B) cierra el recorrido real: tras el clic en «Jugar», Enter en el
título abre el menú principal, señal que da la marca `runMainMenu` en `Module.marks`.

### Presentación larga (L4, historia 4.5)

```sh
DW1_DISC_DIR=/dw/game_rom/extracted python3 port/web/tests/run.py build/port/web long
```

Se dispara a mano (AD-14), no está en `ctest`. Necesita `build/port/web/us.pack`, que deja
`ctest --preset web -R pack_disc`. Corre 10 minutos por navegador (`DW1_WEB_LONG_MINUTES` los
cambia) y deja `long-<navegador>.json`: demora entre postear y mostrar cada frame, memoria
residente del navegador, ritmo y drenaje tras bloquear 2 s el hilo principal. `web_shell`
incluye la versión corta de 30 s que falla si el navegador crece.
