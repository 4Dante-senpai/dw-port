# DW1 Web Port — contexto permanente del proyecto

## Objetivo

Port nativo de **Digimon World (PlayStation, versión USA)** al **navegador** mediante WebAssembly,
compilando el código C descompilado del juego. **No es un emulador**:

- **Permitido:** recompilar el código original del juego a WASM; reimplementar las librerías PsyQ
  (libgpu, libgte, libspu, libcd, libpad, libcard, libetc, libapi) y los chips de la consola
  (GPU, SPU, decodificador XA, MDEC) como librerías del port.
- **Prohibido:** interpretar o recompilar dinámicamente instrucciones MIPS en tiempo de ejecución
  (no hay CPU emulada). Todo el código del juego llega al navegador como C compilado.

Objetivo final: el usuario abre una URL, selecciona su propia imagen del disco (`.bin/.cue`),
el navegador la valida y empaqueta localmente, y el juego corre a 59.94 Hz con audio, video,
input (teclado y gamepad) y partidas guardadas persistentes.

## Fuentes

| Repo | Rol | Licencia |
|---|---|---|
| `jype0/dw_decomp` (GitHub) | **Base del proyecto.** Descompilación matching de DW1. Remote `upstream`. | MIT |
| `Wyrelade/DW2-PC` (GitHub) | **Referencia de arquitectura.** Port nativo de Digimon World 2 desde su decomp: `src/` (C del juego), `psyq/` (librerías PsyQ para PC), `backend/` (GPU, SPU, XA, MDEC, renderer), `host/` (SDL3, input, audio, memcard, pack de datos). Se puede reutilizar código con atribución. | CC0 1.0 |
| `SydMontague/DW1-Code` | Código reconstruido legible (no compilable) + `memoryMap.txt`. Documentación de funciones y estructuras. | Verificar antes de copiar código |
| `SydMontague/DW1-SydPatches` | Reimplementación parcial en C++ estilo "barco de Teseo". | Verificar antes de copiar código |
| PsyCross / Psy-X | Alternativa a la capa `psyq/` de DW2-PC. | Verificar |

DW1 fue compilado con **CodeWarrior for PlayStation R4** (no con el GCC de PsyQ). El build matching
lo necesita (`cc_mips.dll`); el build del port usa Clang/Emscripten.

## Reglas no negociables

1. **Nunca commitear datos ni binarios con copyright:** imagen del disco, archivos extraídos
   (`disks/`, `dumps/`, `assets/` generados), ejecutables originales, `cc_mips.dll` ni nada de
   CodeWarrior. Mantener `.gitignore` y un hook pre-commit que lo verifique. Los manifests
   del pack contienen solo hashes y tamaños.
2. **Build matching:** todo cambio en `src/` necesario para el port va detrás de
   `#ifdef DW1_PORT` y no altera el build PS1.
   - **Con `cc_mips.dll` disponible:** `make` + `make compare` deben seguir produciendo el
     binario idéntico al original en cada merge.
   - **Sin `cc_mips.dll` (modo sin compilador matching):** no se ejecuta el build PS1. Las
     funciones que se descompilen se validan por comportamiento (tests y diff de frames contra
     el emulador de referencia) y se registran en `docs/port/non-matching.md` para verificarlas
     con objdiff si el compilador pasa a estar disponible. No proponerlas a upstream sin esa
     verificación.
3. **Divergencia mínima con upstream:** el código propio del port vive en `port/` y `tools/port/`.
   Los cambios en `src/` son mínimos, aislados y documentados en `docs/port/src-changes.md`,
   para poder mergear `upstream/main` periódicamente sin conflictos grandes.
4. **Funciones descompiladas nuevas** se escriben con estilo upstream (borrador inicial con m2c)
   y, cuando haya compilador matching, se validan con objdiff para poder proponerlas a
   `jype0/dw_decomp`.
5. **Verificación objetiva:** ninguna historia se cierra sin evidencia automática (tests,
   `make compare`, diff de frames contra referencia, logs). No marcar tareas como completas
   con tests fallando o implementaciones parciales.
6. **Idiomas:** documentación BMAD y comunicación con el humano en español neutro; código,
   comentarios, nombres y commits en inglés (Conventional Commits).
7. **Git:** push solo a `origin`. Nunca push a `upstream`. Nunca reescribir historia publicada
   ni hacer force-push sin aprobación explícita.
8. **No inventar:** si un dato del juego, una dirección o un comportamiento del hardware no está
   verificado, se marca como hipótesis y se valida con el binario original o con un emulador de
   referencia (DuckStation / PCSX-Redux) antes de depender de él.

## Layout objetivo

```
src/, include/, config/, asm/   # upstream dw_decomp (tocar lo mínimo)
port/
  psyq/      # reimplementación de las librerías PsyQ que usa el juego
  backend/   # GPU (rasterizador software fiel a PSX + renderer WebGL2), SPU, XA, MDEC
  host/      # capa de plataforma SDL3: ventana, input, audio, memcard, pack de datos
  web/       # shell HTML/JS, selector de disco, IndexedDB/OPFS, AudioWorklet, headers COOP/COEP
  recomp/    # (si se adopta) funciones aún no descompiladas, traducidas mecánicamente MIPS -> C
tools/port/  # constructor del pack, manifest de hashes, comparación de frames, métricas
reference/   # submódulos de solo lectura (DW2-PC)
docs/        # artefactos BMAD + docs/port/ + docs/research/
```

Targets de build (CMake + Ninja para el port, Makefile upstream para matching):
`matching` (PS1), `native` (binario i686 sobre Linux x64, para depurar; ver AD-3), `web` (Emscripten).

## Hitos técnicos (orden de dependencias)

- **M0 Entorno:** contenedor con toolchain del decomp + Clang + CMake/Ninja + emsdk + SDL3.
  Build matching verde.
- **M1 Inventario:** % de funciones en C vs asm por segmento/overlay; mapa de overlays; lista de
  funciones PsyQ llamadas por el juego; accesos a direcciones fijas (scratchpad `0x1F800000`,
  registros de hardware, punteros absolutos `0x80xxxxxx` en datos del disco).
- **M2 Nativo headless:** el juego linkea completo para Linux (stubs donde haga falta), arranca y
  llega a la pantalla de título con renderer software.
- **M3 Paridad nativa:** video, audio, CD/XA, MDEC, memcard; comparación de frames contra DuckStation.
- **M4 Web:** mismo código en Emscripten; renderer software presentado vía WebGL; input teclado y
  Gamepad API; audio con AudioWorklet; saves en IndexedDB; pack construido en el navegador.
- **M5 Renderer WebGL2 y rendimiento:** escalado de resolución, opción sin wobble, 59.94 Hz estables.
- **M6 Jugabilidad completa:** partida de inicio a fin, tests de regresión por escena.
- **M7 Deploy:** contenedor nginx con `Cross-Origin-Opener-Policy: same-origin` y
  `Cross-Origin-Embedder-Policy: require-corp`, en el servidor propio.

## Riesgos conocidos (tratar como ADRs en la arquitectura)

- **Funciones sin descompilar:** el port necesita el 100% del código en C. Evaluar estrategia
  híbrida: traducción mecánica MIPS→C de las funciones pendientes en `port/recomp/`, reemplazadas
  a medida que se descompilan.
- **Overlays:** en PS1 se cargan en la misma región de memoria; en el port se linkean juntos y el
  cambio de escena debe reinicializar sus datos como lo hacía la recarga original.
- **Modelo de memoria:** WASM32 usa punteros de 32 bits y little-endian, igual que PS1, así que los
  structs conservan su layout; aun así hay que traducir direcciones absolutas PS1 presentes en
  datos y código.
- **Diferencias de compilador:** signedness de `char`, padding/alineación, comportamiento
  indefinido y overflows que CodeWarrior resolvía distinto que Clang.
- **Bloqueos en el loop:** el juego espera VSync y lecturas de CD de forma bloqueante; en el
  navegador el hilo principal no puede bloquear. Evaluar `-sPROXY_TO_PTHREAD` (requiere
  SharedArrayBuffer + COOP/COEP) frente a ASYNCIFY/JSPI.
- **Renderer:** SDL_GPU no tiene backend WebGL; en web usar rasterizador software primero y luego
  un renderer WebGL2 propio.

## Operación de cada sesión

Las ejecuciones de Codex lanzadas por `tools/port/codex-run.sh` ignoran esta sección: no leen
`docs/STATUS.md` ni el sprint, y no commitean ni pushean.

1. Leer este archivo, `docs/STATUS.md` y el sprint activo de BMAD.
2. Trabajar sobre la historia en curso siguiendo el workflow de desarrollo de BMAD.
3. Al terminar: actualizar `docs/STATUS.md` (hecho, en curso, bloqueos, métricas de % descompilado
   y escenas que funcionan), commitear y hacer push a `origin`.
4. **Detenerse y pedir intervención humana** solo para: proveer la imagen del disco o
   `cc_mips.dll`, aprobar PRD y arquitectura, decisiones irreversibles, force-push, o publicar
   un deploy accesible públicamente.

## GitLab (gitlab.homelab.local)
- Para operaciones sobre GitLab (issues, merge requests, ramas, pipelines, jobs, logs)
  usar las herramientas del servidor MCP `GitLab-Local`. No usar curl ni tokens en la línea de comandos.
- Para subir cambios del clon local usar `git push`; las credenciales ya están configuradas.
- Para disparar o reintentar pipelines usar `save_pipeline`; para revisar fallos, `get_pipeline` y `get_job` con el log.