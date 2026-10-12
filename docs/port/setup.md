# Entorno de desarrollo del port

Procedimiento verificado el 2026-10-11 (M0). Modo usado: **sin compilador matching**
(`cc_mips.dll` no disponible, regla 2 de `CLAUDE.md`).

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
| `dw-port:latest` | `tools/port/Dockerfile` (`FROM dw-build`) | + CMake 3.28, Ninja 1.11, LLD, Clang 18, `gcc-multilib` (build nativo i686, AD-3), **emsdk 6.0.10**, **SDL3 3.4.16** nativo (`/opt/sdl3/native`) y Emscripten estático (`/opt/sdl3/web`), p7zip |

Versiones fijadas como `ARG` en `tools/port/Dockerfile` (`EMSDK_VERSION`, `SDL3_VERSION`). La base
`ubuntu:noble` de upstream no está fijada por digest (pendiente para build reproducible).

```sh
tools/port/docker_build.sh            # construye dw-build y luego dw-port
tools/port/docker_run.sh <comando>    # corre <comando> en dw-port con el repo en /dw, como el usuario del host
```

`.dockerignore` deja fuera del contexto todo menos `requirements.txt`, así la imagen del disco
nunca se envía al daemon.

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

6. Desensamblado (splat, no necesita CodeWarrior):

   ```sh
   tools/port/docker_run.sh make -j$(nproc) regenerate
   ```

7. Solo con `cc_mips.dll` (no disponible hoy): copiarlo a `bin/cc_mips/cc_mips_40.dll` y
   `make -j$(nproc) && make compare`.

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

## Resultado de la verificación (2026-10-11)

| Paso | Resultado |
|---|---|
| `tools/dl_deps.sh` | OK, 6 sha256 verificados |
| `dumpsxiso` | OK; `SLUS_010.32` sha1 `31ec666b…5b` = `config/us/main.yaml` |
| `make regenerate` | OK (≈10 s, 4 núcleos) |
| `make TOOLCHAIN=gcc` | OK: `SLUS_010.32` + 15 `*_REL.BIN` |
| `make` / `make compare` | **No ejecutado**: sin `cc_mips.dll` |
| `tools/port/env_check.sh` | OK: `SDL 3.4.16, platform Linux, sizeof(void*)=8` y `SDL 3.4.16, platform Emscripten, sizeof(void*)=4` |

Criterio de salida de M0 en modo sin compilador: `make regenerate` OK y entorno del port
funcionando → **cumplido**.

## Emulador de referencia (pendiente)

No se agregó a la imagen. Evaluación:

- **PCSX-Redux**: tiene modo sin UI y scripting Lua (sirve para capturar frames y leer RAM) y trae
  OpenBIOS, así que podría arrancar sin BIOS de Sony; compilarlo desde fuente arrastra muchas
  dependencias y requiere contexto OpenGL (Xvfb en headless).
- **DuckStation**: el binario `duckstation-regtest` corre headless y vuelca frames, pero necesita
  una BIOS real (dato con copyright que el usuario tendría que proveer, fuera del repo).

Se resuelve como historia propia de M3 (diff de frames contra referencia), en una imagen
separada para no engordar `dw-port`.
