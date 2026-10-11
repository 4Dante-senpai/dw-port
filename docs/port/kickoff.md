Vas a iniciar el proyecto **DW1 Web Port**: un port de Digimon World (PS1) al navegador,
compilando a WebAssembly el código C descompilado del juego, sin emulador. Vas a trabajar de
forma autónoma usando el método BMAD (módulo BMM) y me vas a pedir intervención solo en los
puntos de control indicados abajo.

Antes de hacer nada, leé `CLAUDE.md` en la raíz de este directorio: contiene el objetivo, las
reglas no negociables, el layout, los hitos y los riesgos. Esas reglas tienen prioridad sobre
cualquier otra indicación de este prompt.

## Datos de configuración

- Remote propio (origin): `https://github.com/4Dante-senpai/dw-port.git`
- Upstream: `https://github.com/jype0/dw_decomp.git`
- Referencia de arquitectura: `https://github.com/Wyrelade/DW2-PC.git`
- Imagen del disco (USA, .bin/.cue con sectores de 2352 bytes): `./game_rom`
- `cc_mips.dll` de CodeWarrior for PlayStation R4 (**opcional**): `NO_DISPONIBLE`

Si `cc_mips.dll` figura como `NO_DISPONIBLE`, el proyecto opera en **modo sin compilador
matching** (ver regla 2 de `CLAUDE.md`): no se ejecuta `make` ni `make compare` del build PS1, y
todo lo demás sigue igual. Si otro dato falta o la ruta no existe, seguí con todo lo que no
dependa de él y dejalo anotado como bloqueo en `docs/STATUS.md`.

## Fase 0 — Repositorio

1. Clonar `jype0/dw_decomp` con submódulos (`--recursive`) en este directorio.
2. Configurar remotes: renombrar el remote de GitHub a `upstream` y agregar `origin` apuntando a
   `<GIT_REMOTE_URL>`. Deshabilitar el push a upstream (`git remote set-url --push upstream DISABLED`).
3. Agregar DW2-PC como submódulo de solo lectura en `reference/dw2-pc`, fijado al commit actual.
4. Mover a la raíz `CLAUDE.md` y este archivo (este último a `docs/port/kickoff.md`).
5. Extender `.gitignore` para excluir: imagen del disco, `disks/`, `dumps/`, cualquier archivo
   extraído o generado a partir del disco, `bin/cc_mips/*.dll`, builds y caches. Agregar un hook
   pre-commit (versionado en `tools/port/hooks/` con script de instalación) que rechace archivos
   de esas rutas y archivos binarios mayores a 1 MB fuera de `reference/`.
6. Crear `docs/STATUS.md` y `docs/port/src-changes.md` vacíos con su estructura.
7. Primer commit y push a `origin/main`.

## Fase 1 — Entorno

1. Partir del `Dockerfile` / `.devcontainer` que ya trae dw_decomp y extenderlo con: Clang,
   CMake, Ninja, emsdk (versión fijada), SDL3 (nativo y para Emscripten), y DuckStation o
   PCSX-Redux en modo headless si es viable, para obtener frames de referencia.
2. Ejecutar `tools/dl_deps.sh` y extraer el disco con `dumpsxiso` según el README de dw_decomp.
3. Ejecutar `make regenerate` (desensamblado con splat; no requiere CodeWarrior).
4. Solo si hay `cc_mips.dll`: copiarlo a `bin/cc_mips/cc_mips_40.dll` y ejecutar `make` y
   `make compare`. **Criterio de salida:** compare OK. Sin el DLL, el criterio de salida es
   `make regenerate` OK y el entorno del port (Clang, CMake, emsdk, SDL3) funcionando.
5. Documentar el procedimiento exacto en `docs/port/setup.md`, incluido el modo usado.

## Fase 2 — Investigación técnica (antes de cualquier artefacto BMAD)

Producir en `docs/research/`, con datos medidos y no estimados:

- `decomp-status.md`: total de funciones, cuántas en C y cuántas en asm, desglosado por
  ejecutable y overlay. Con compilador matching, usar `make objdiff` y su reporte; sin él,
  contar las funciones que siguen como `INCLUDE_ASM` frente a las que ya están en C en `src/`.
  Tamaño en bytes pendiente.
- `overlays.md`: overlays del juego, cuándo se cargan, a qué región de memoria, dependencias.
- `psyq-usage.md`: todas las funciones PsyQ que llama el juego, agrupadas por librería, con
  cantidad de llamadas.
- `hardware-access.md`: accesos directos a registros de hardware, scratchpad, direcciones
  absolutas y punteros embebidos en archivos de datos del disco.
- `dw2pc-architecture.md`: análisis de `reference/dw2-pc` (`psyq/`, `backend/`, `host/`, pack de
  datos, manejo de overlays, threading). Para cada componente: qué se puede reutilizar tal cual,
  qué hay que adaptar a DW1 y qué no sirve en web (por ejemplo SDL_GPU/Vulkan).
- `web-constraints.md`: opciones de threading en Emscripten (`PROXY_TO_PTHREAD`, ASYNCIFY,
  JSPI), audio (AudioWorklet), almacenamiento (IndexedDB vs OPFS), lectura del disco del usuario
  en el navegador, y requisitos de COOP/COEP, con recomendación justificada.
- `risks.md`: riesgos priorizados con mitigación.

Commit y push al terminar. Mostrame un resumen de 10 líneas con las métricas clave.

## Fase 3 — Instalación de BMAD

1. Instalar BMAD con `npx bmad-method install --directory . --modules bmm --tools claude-code`.
   Idioma de comunicación: español. Idioma de los documentos: español.
2. Listar los comandos/agentes/workflows que quedaron instalados y usar exactamente esos
   nombres en adelante (no asumir nombres de versiones anteriores de BMAD).
3. Elegir el track "BMad Method" (PRD + Arquitectura; sin UX detallado salvo la shell web).
   El módulo de game dev de BMAD apunta a Unity/Unreal/Godot y no aplica a este proyecto.

## Fase 4 — Planificación con BMAD

Ejecutar los workflows en este orden, usando `docs/research/` como insumo principal:

1. **Product brief (Analyst).** Problema, objetivo, usuarios (jugadores con su propia copia del
   juego), alcance, fuera de alcance (multijugador, mods, versiones JP/PAL), restricciones legales
   (sin datos del juego en el repo ni en el deploy).
2. **PRD (PM).** Requisitos funcionales mínimos:
   - Selección de la imagen del disco en el navegador, validación contra manifest de hashes,
     construcción del pack local y persistencia para no pedir el disco de nuevo.
   - Juego completo jugable: título, mundo, combates, menús, videos (MDEC), audio XA, música y SFX.
   - Input por teclado y gamepad con remapeo; saves persistentes e importación/exportación de
     memory cards en formato `.mcd` compatible con emuladores.
   - Opciones de video: escala, sin wobble, 4:3/16:9 si es viable.
   Requisitos no funcionales: 59.94 Hz estables en Chrome y Firefox de escritorio en hardware
   medio; tamaño de descarga del `.wasm` acotado; funcionamiento offline tras la primera carga;
   build reproducible; matching build siempre verde.
   **Punto de control humano: esperar mi aprobación del PRD.**
3. **Arquitectura (Architect).** Debe incluir ADRs explícitos para:
   - Estrategia para funciones no descompiladas (completar decomp vs. híbrido con `port/recomp/`).
   - Capa PsyQ: adaptar `reference/dw2-pc/psyq` vs PsyCross vs propia.
   - Modelo de memoria y traducción de direcciones PS1.
   - Manejo de overlays en un único binario.
   - Modelo de threading y loop principal en web.
   - Renderer: software primero, WebGL2 después; interfaz común entre ambos.
   - Audio: SPU emulado ejecutando el driver de sonido original vs alto nivel.
   - Formato del pack de datos y manifest.
   - Estrategia de testing: `make compare`, unit tests del port, diff de frames por escena
     contra emulador de referencia, smoke tests headless en navegador (Playwright).
   - CI en mi servidor: qué jobs pueden correr sin datos del juego y cuáles necesitan un
     runner privado con la imagen del disco montada fuera del repo.
   **Punto de control humano: esperar mi aprobación de la arquitectura.**
4. **Épicas e historias.** Alineadas con los hitos M2–M7 de `CLAUDE.md`, más una épica continua
   de descompilación con métrica de avance. Cada historia con criterios de aceptación
   verificables automáticamente y sin ambigüedad.
5. **Sprint planning.**

## Fase 5 — Ejecución autónoma

A partir de la aprobación de la arquitectura, ejecutar el ciclo de BMAD historia por historia
(desarrollo → code review → cierre) sin pedirme confirmación, respetando:

- Una rama por historia, merge a `main` solo con build matching verde, tests del port verdes y
  evidencia adjunta en la historia (logs, capturas, métricas).
- Prioridad del primer tramo: llegar a la pantalla de título en nativo (M2) y luego en el
  navegador (M4) con renderer software, antes de pulir cualquier otra cosa.
- Cuando una historia quede bloqueada por funciones sin descompilar, aplicar la estrategia del
  ADR correspondiente en lugar de detenerse.
- Al cerrar cada sesión, actualizar `docs/STATUS.md` y hacer push.

## Formato de tus reportes

Al final de cada fase o sesión, un resumen breve con: qué se completó, métricas (% descompilado,
hito alcanzado, escenas funcionando), bloqueos y el siguiente paso. Sin narrar cada comando.

Empezá por la Fase 0.
