---
status: final
stepsCompleted: [1, 2, 3, 4]
inputDocuments:
  - docs/planning-artifacts/prds/prd-dw-port-2026-10-11/prd.md
  - docs/planning-artifacts/prds/prd-dw-port-2026-10-11/addendum.md
  - docs/planning-artifacts/architecture/architecture-dw-port-2026-10-11/ARCHITECTURE-SPINE.md
  - docs/research/decomp-status.md
  - docs/research/overlays.md
  - docs/research/psyq-usage.md
  - docs/research/hardware-access.md
  - docs/research/dw2pc-architecture.md
  - docs/research/web-constraints.md
  - docs/research/risks.md
  - CLAUDE.md
  - docs/STATUS.md
mode: autónomo (PRD y arquitectura aprobados por el dueño el 2026-10-11; los menús del workflow se confirman sin parada humana, según el kickoff)
---

# DW1 Web Port - Desglose en épicas

## Resumen

Este documento descompone en épicas e historias los requisitos del PRD y las decisiones de la
arquitectura (AD-1..AD-18). No hay documento de UX: las pantallas de la Shell web salen de FR-15.
Toda historia cierra con evidencia automática, según los niveles de AD-14 (L0a..L4). Las épicas
se numeran con el número de hito (Épica 2 = M2, …, Épica 7 = M7); la Épica 8 es la continua de
decomp. M0 y M1 ya están completos (`docs/STATUS.md`).

## Inventario de requisitos

### Requisitos funcionales

- FR-1: El usuario elige la Imagen del disco (`.bin` o `.bin` + `.cue`) por selector o arrastre. Se acepta `MODE2/2352`; se rechaza un `.cue` con otro modo o un `.iso` de 2048 B/sector, con mensaje. Lectura por partes; ≤ 512 MB de memoria en la pestaña durante la validación.
- FR-2: Cada archivo de la Imagen del disco se valida contra el Manifest. Un byte cambiado hace fallar la validación nombrando el archivo; JP/PAL se rechaza nombrando SLUS-01032; si falla no queda Pack parcial.
- FR-3: El Pack se construye localmente y persiste. Al recargar se ofrece "Jugar" sin pedir el disco; el Pack tiene exactamente los archivos del Manifest con sus SHA-256; el Pack web es idéntico byte a byte al de la herramienta nativa; se pide `navigator.storage.persist()` e informa si se deniega; el usuario puede borrar el Pack.
- FR-4: El Juego arranca y llega a la pantalla de título; la Escena "pantalla de título" a 1× con Renderer software coincide píxel a píxel con su Frame de referencia; solo requiere "Jugar".
- FR-5: Partida de principio a fin. Suite de Escenas que cubre los 16 overlays (`BTL`, `STD`, `FISH`, `EVL`, `KAR`, `VS`, `MOV`, `DOO2`, `DOOA`, `TRN`, `SHOP`, `DGET`, `TRN2`, `MURD`, `ENDI`, `EAB`), cada una pasa su test; una partida grabada se reproduce en nativo sin divergencia de estado contra el Emulador de referencia en los puntos de control.
- FR-6: Videos STR/MDEC con sonido; frames dentro de la tolerancia de AD-15; A/V desfasados ≤ 1 frame de video al final; Start salta el video.
- FR-7: Música, efectos y XA; señal dentro de la tolerancia de AD-15; sin underruns en 10 min en hardware de referencia; volumen general ajustable.
- FR-8: Teclado y gamepad (Gamepad API): mapeo por defecto de los 14 botones digitales; conectar/desconectar no detiene el Juego; stick izquierdo como D-pad (opción).
- FR-9: Remapeo de cada botón a tecla o botón de gamepad, persistente tras recargar; "restaurar valores por defecto".
- FR-10: Saves persistentes entre sesiones; una escritura interrumpida deja el estado anterior o el nuevo, nunca una tarjeta ilegible.
- FR-11: Importar/exportar `.mcd`/`.mcr` de 131.072 bytes en dos ranuras; lo exportado carga en DuckStation y PCSX-Redux; lo importado de DuckStation carga en el Juego; otro tamaño se rechaza.
- FR-12: Escala de resolución interna del 3D de 1× a 4×; a 1× sin mejoras los frames coinciden con la referencia; a ≥ 2× la geometría 3D se dibuja a esa resolución y texturas/2D conservan sus píxeles.
- FR-13: Sin wobble: vértices precisos y corrección de perspectiva; con cámara quieta el desplazamiento máximo de vértice entre frames es < 1 píxel de salida.
- FR-14: Aspecto 4:3 o 16:9 ampliando el FOV del 3D sin estirar la interfaz; donde no sea viable, 4:3 con barras.
- FR-15: Shell web: bienvenida con aviso legal, selección de disco, progreso, "Jugar", opciones (video, audio, controles), Memory cards, borrar datos; mensaje específico si faltan capacidades (aislamiento cross-origin, almacenamiento); pausa manual y automática al ocultar la pestaña.
- FR-16: Offline tras la primera carga; una versión nueva del deploy se aplica al recargar sin perder Pack ni Memory cards.
- FR-17: Build nativo con el mismo código que el web, con y sin ventana; un preset de CMake produce un ejecutable que llega al título; modo headless que corre N frames y vuelca capturas.

### Requisitos no funcionales

- NFR-1: 59,94 Hz (1001/60000 s) sin deriva: tras 10 min el conteo de VBlanks difiere del teórico en < 1 (nativo) y < 0,1 % (web).
- NFR-2: En Chrome y Firefox de escritorio en el hardware de referencia (i5-8250U, GPU integrada, 8 GB), Renderer software a 1×, el 99 % de los frames de 10 min a tiempo.
- NFR-3: `.wasm` con Brotli ≤ 4 MB; primera carga total sin Pack ≤ 6 MB.
- NFR-4: Sin datos del juego en repo ni deploy salvo los bytes que reproduce el C de `src/`/`include/`; check en runner privado: V(deploy) ∩ V(disco) ⊆ V(objetos de `src/`), con ventanas de 64 bytes (AD-18).
- NFR-5: Imagen del disco, Pack y Memory cards nunca salen del dispositivo; el deploy solo pide sus propios archivos.
- NFR-6: Con `cc_mips.dll`, `make compare` OK en cada merge; sin él, todo cambio en `src/` detrás de `DW1_PORT` y preprocesado sin `DW1_PORT` idéntico a `upstream/main`.
- NFR-7: Dos builds del mismo commit en la imagen Docker dan el mismo `.wasm` byte a byte.
- NFR-8: Renderer software a 1× sin mejoras: cada Escena coincide píxel a píxel con su Frame de referencia, salvo excepciones documentadas por Escena.
- NFR-9: Chrome y Firefox de escritorio, dos últimas versiones estables.
- NFR-10: Cada cambio en `src/` listado en `docs/port/src-changes.md`; un merge de `upstream/main` no tiene conflictos fuera de esos puntos.

### Requisitos adicionales (arquitectura)

- Sin plantilla de inicio: el proyecto es brownfield sobre `dw_decomp`; la primera historia de código crea el árbol `port/` y `CMakePresets.json` del Structural Seed.
- AD-1: la lista de TUs del juego se genera de `mk/version/us.mk` y `config/us/*.yaml`; no se linkea `asm/`; no existe `port/recomp/`.
- AD-2: una implementación por función PsyQ en `port/psyq/<lib>.c` con comentario `dw2-pc:` o `asm:`; headers del port primero en el include path; macros GTE redefinidas hacia el Backend; atribución DW2-PC en la primera línea.
- AD-3: native es i686 (`-m32`) con `_Static_assert(sizeof(void *) == 4)`; `native-headless` sin SDL; SDL3 i686 en la historia del preset con ventana.
- AD-4: arena `Ps1_Ram[0x200000]`, `PS1_RAM()` con assert, scratchpad de 1 KB; traducción de direcciones solo en macros `DW1_PORT` de `src/`/`include/` y en entradas PsyQ con direcciones enteras; inventario de literales generado por `tools/port/metrics/ps1_literals.py` y verificado en L0a; `__rts_info_t__.h` del port renombra `_end`/`_stack_*`.
- AD-5: ventana de 16 MB para todo lo enlazable en un OT, incluido el stack del hilo del juego; nativo `-no-pie -Wl,-Ttext-segment=0x01000000`; `PS1_LINK`/`PS1_LINK_PTR` con assert; ASan con `detect_stack_use_after_return=0`.
- AD-6: TU envoltorio por archivo de overlay con `#pragma clang attribute` y sección `dw1ovl_<ovl>`; snapshot al arrancar; restore en `loadDynamicLibrary` (sync y async); detector de aliasing en debug; sin LTO en wasm; test L1 de reinicio.
- AD-7: web con `-pthread -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`, hilo del juego con stack estático (fallback `-sPROXY_TO_PTHREAD`), input en el hilo principal, AudioWorklet con ring en SAB; sin ASYNCIFY/JSPI.
- AD-8: `port/psyq/clock.c` dueño único del tiempo (ciclos a 33.868.800 Hz + cola de eventos); toda entrada PsyQ avanza el reloj; callbacks solo en el hilo del juego; costos de CD/GPU/MDEC; `VSync(n)` con semántica PsyQ; inventario de spins por `tools/port/metrics/isr_spins.py` con `port_yield()`; reloj de pared con recuperación de 3 VBlanks y conteo de frames perdidos; input latcheado por VBlank.
- AD-9: interfaz única `port/backend/gpu.h`; Renderer software oráculo (desde `dw2-pc/backend/psxgpu.c`); WebGL2 con canal lateral del GTE; contexto GL en el hilo del juego vía `OffscreenCanvas`; `plat_present(frame)` único.
- AD-10: SPU emulado (24 voces, ADSR, reverb, entrada CD/XA) desde `dw2-pc/backend/psxspu.c`; `libsnd`/`libspu` traducidos del asm de DW1; SPU ejecutado hasta el tiempo actual antes de cada escritura; 44.100 × 1001/60000 muestras por VBlank; deriva y volumen en el Host.
- AD-11: Manifest `tools/port/manifest/us.tsv` (ruta, LBA, sectores, modo F1/F2, tamaño, SHA-256 + fila de `SLUS_010.32`); Pack de un archivo con `format_version`; un solo C en `port/host/pack/` para nativo y web; `CdlModeSize1` reconstruido en `cdrom`; validación todo o nada; migración de formato sin pedir el disco.
- AD-12: tablas de PsyQ (`rcossin_tbl`, `ratan_tbl`, VLC de `libpress`, etc.) leídas de `SLUS_010.32` en el Pack con offsets de `config/us/main.yaml`; generar una tabla solo con test L2 de SHA-256.
- AD-13: worker de E/S único dueño de OPFS, buzón SAB + `Atomics.wait`; IndexedDB con `schema_version` y `durability:'strict'`; Web Lock `dw1-game`; nativo `.mcd` con temporal + `rename`; `navigator.storage.persist()`.
- AD-14: niveles de test L0a, L0b, L1, L2, L3a, L3b, L4; Escenas en `tools/port/scenes/<id>.toml`; referencias fuera del repo.
- AD-15: frame 0 = retorno del primer `VSync` desde `main`; captura tras los eventos del VBlank según GP1; video exacto; MDEC ≤ 4/255 máx. y ≤ 0,5/255 media; traza SPU idéntica y PCM SNR ≥ 40 dB; variables de estado tipadas con punteros traducidos; log subpíxel del GTE para FR-13.
- AD-16: GitLab CI propio; clase A sin datos en cualquier runner; clase B en runner protegido `dw1-data` con disco montado de solo lectura, solo pipelines de ramas propias; imagen `dw-port` del registry fijada por digest; hasta tener el forge conectado, las clases corren a mano en el contenedor y el log es la evidencia.
- AD-17: presets `native`, `native-headless`, `native-asan`, `web`; flags del juego `-funsigned-char -fwrapv -fno-strict-aliasing -fno-common -DDW1_PORT -DLANGUAGE_C -DVERSION_US` (verificar `char` sin signo con diff de objetos); `-ffile-prefix-map` y `SOURCE_DATE_EPOCH`.
- AD-18: los bytes del disco que reproduce el C de `src/`/`include/` se compilan tal cual; `tools/port/ci/data_leak_check` en clase B.
- Deferred de la arquitectura a verificar en historias: punteros absolutos en `CHDAT/*.MMD` (M2), uso de `SHOP_REL.BIN` en USA (M3), viabilidad de 16:9 por Escena (M5).
- Operativa (`CLAUDE.md`): una rama por historia; merge solo en verde con evidencia; `docs/STATUS.md` actualizado y push a `origin` al final de cada sesión.

### Requisitos de diseño UX

No hay documento de UX. Las pantallas y estados de la Shell web se especifican en las historias de FR-15.

### Mapa de cobertura de FR

| Requisito | Épica | Qué cubre |
|---|---|---|
| FR-1 | 4 | Selección de la Imagen del disco en el navegador |
| FR-2 | 2, 4 | Validación con la herramienta nativa (2) y en el navegador (4) |
| FR-3 | 2, 4 | Pack nativo (2); Pack en OPFS, persistencia y borrado (4) |
| FR-4 | 2, 3, 4 | Título en nativo headless (2), píxel a píxel contra la referencia (3), en el navegador (4) |
| FR-5 | 3, 6 | Herramientas de Escenas y estado (3); suite de los 16 overlays y partida grabada (6) |
| FR-6 | 3 | MDEC, STR y sincronía A/V |
| FR-7 | 3, 4 | SPU, `libsnd`, XA (3); AudioWorklet y volumen (4) |
| FR-8 | 3, 4 | Input SDL3 (3); teclado y Gamepad API (4) |
| FR-9 | 4 | Remapeo persistente |
| FR-10 | 3, 4 | Memory card nativa (3); IndexedDB (4) |
| FR-11 | 3, 4 | `.mcd` compatible (3); importar y exportar (4) |
| FR-12 | 5 | Escala de resolución |
| FR-13 | 5 | Sin wobble |
| FR-14 | 5 | 16:9 |
| FR-15 | 4, 5 | Pantallas de la Shell web (4); opciones de video (5) |
| FR-16 | 4 | Offline y actualizaciones |
| FR-17 | 2, 3 | Preset headless (2); preset con ventana SDL3 (3) |
| NFR-1 | 2, 3, 4 | Reloj virtual (2); medición nativa (3); medición web (4) |
| NFR-2 | 5 | L4 en hardware de referencia |
| NFR-3 | 4 | Check de tamaño |
| NFR-4 | 4, 7 | `data_leak_check` (4); sobre el deploy (7) |
| NFR-5 | 4 | Sin pedidos de red fuera de los propios |
| NFR-6 | 2, 8 | L0a (2); L0b y `make compare` (8) |
| NFR-7 | 4 | `.wasm` reproducible |
| NFR-8 | 3, 6 | Diff de frames (3); suite completa (6) |
| NFR-9 | 4 | Playwright en Chromium y Firefox |
| NFR-10 | 2, 8 | `src-changes.md` (2); merge de upstream (8) |

## Lista de épicas

### Épica 2: El Juego arranca en nativo hasta la pantalla de título (M2)
El desarrollador compila el Juego completo como binario i686 headless sobre la Capa PsyQ propia, lo
alimenta con un Pack construido desde su disco y llega a la pantalla de título con el Renderer
software, con una captura determinista.
**FRs cubiertos:** FR-2 (nativo), FR-3 (nativo), FR-4 (llegada), FR-17 (headless); NFR-1 (reloj), NFR-6 (L0a), NFR-10.

### Épica 3: Paridad nativa contra el Emulador de referencia (M3)
El Build nativo se ve, suena y guarda como la consola: frames exactos, audio con traza idéntica,
videos, XA y Memory cards, verificados contra PCSX-Redux; además, se juega con ventana SDL3.
**FRs cubiertos:** FR-4 (exactitud), FR-5 (herramientas), FR-6, FR-7 (señal), FR-8 (SDL3), FR-10 y FR-11 (nativo), FR-17 (ventana); NFR-1 (nativo), NFR-8.

### Épica 4: Jugar en el navegador con el disco propio (M4)
El jugador abre la Shell web, elige su disco, lo valida y empaqueta en el navegador, y juega con
teclado o gamepad, con audio y saves persistentes, también offline.
**FRs cubiertos:** FR-1, FR-2, FR-3, FR-4 (web), FR-7 (salida y volumen), FR-8, FR-9, FR-10, FR-11, FR-15, FR-16; NFR-1 (web), NFR-3, NFR-4 (check), NFR-5, NFR-7, NFR-9.

### Épica 5: Mejoras de video y 59,94 Hz estables (M5)
El jugador elige escala de resolución, sin wobble y 16:9, y el Juego mantiene el ritmo en el
hardware de referencia.
**FRs cubiertos:** FR-12, FR-13, FR-14, FR-15 (opciones de video); NFR-2.

### Épica 6: Partida completa sin divergencias (M6)
El jugador puede jugar de principio a fin: la suite de Escenas cubre los 16 overlays y una
partida grabada se reproduce sin divergencia de estado.
**FRs cubiertos:** FR-5; NFR-8 (suite completa); SM-2.

### Épica 7: Deploy en el servidor propio (M7)
El dueño publica el port en su servidor con COOP/COEP, sin datos del juego, y lo abre al público
solo con su aprobación.
**FRs cubiertos:** NFR-4 (deploy), NFR-5; restricción de deploy del §6.

### Épica 8: Decomp, upstream y CI (continua)
El dueño ve en cada sesión el estado del decomp y de la Capa PsyQ, puede mergear `upstream/main`
sin conflictos fuera de los puntos documentados y tiene las clases de CI corriendo.
**FRs cubiertos:** NFR-6, NFR-10; AD-1 (métricas), AD-16.

## Epic 2: El Juego arranca en nativo hasta la pantalla de título (M2)

El desarrollador compila el Juego completo como binario i686 headless sobre la Capa PsyQ propia,
lo alimenta con un Pack construido desde su disco y llega a la pantalla de título
(`runLandingScreen`, después de `playMovie(0)` en `src/main/main.c`) con el Renderer software.
La exactitud píxel a píxel contra el Emulador de referencia queda para la Épica 3.

### Story 2.1: Esqueleto del build del port y lista generada de TUs

Como desarrollador del port,
quiero un build CMake que compile todos los TUs del Juego USA con los flags de AD-17,
para que cada historia siguiente agregue código sobre una base que ya compila.

**Criterios de aceptación:**

**Given** el árbol del Structural Seed (`port/psyq`, `port/backend`, `port/host`, `CMakePresets.json`)
**When** se corre `tools/port/build/tu_list.py us`
**Then** produce la lista de TUs del Juego y su overlay a partir de `mk/version/us.mk` y `config/us/*.yaml`
**And** un test de la herramienta verifica que `vs_intro`, `vs_combat`, `vs_select` y `shop/script_menu` caen en `main` y que ningún `*_bss_{eu,jp*}.c` aparece

**Given** el preset `native-headless`
**When** se corre `cmake --preset native-headless && cmake --build --preset native-headless --target dw1_game_objs` en la imagen `dw-port`
**Then** los 120 TUs del Juego compilan a objetos i686 sin errores con `-funsigned-char -fwrapv -fno-strict-aliasing -fno-common -DDW1_PORT -DLANGUAGE_C -DVERSION_US`
**And** los headers de `port/psyq/include/` van antes que los de upstream, las macros GTE inline están redefinidas como llamadas declaradas en `port/backend/gte.h`, y `_Static_assert(sizeof(void *) == 4)` está en `port/psyq/ps1mem.h`

**Given** los objetos compilados con `char` con y sin signo
**When** se corre `tools/port/metrics/char_sign_diff.sh`
**Then** el informe lista los TUs cuyo objeto cambia y queda en `docs/port/char-signedness.md` (verifica el supuesto de AD-17)

### Story 2.2: Checks L0a y los inventarios de literales y spins

Como desarrollador del port,
quiero un check automático de que `src/` sin `DW1_PORT` no cambió y los inventarios de AD-4 y AD-8,
para que ningún cambio rompa el build matching sin que nadie lo vea.

**Criterios de aceptación:**

**Given** `tools/port/ci/preproc_check.sh`
**When** se corre contra el merge-base con `upstream/main`
**Then** compara el preprocesado de cada `.c` de `src/` sin `DW1_PORT` y sale con 0 si es idéntico
**And** un test inyecta un cambio fuera de `#ifdef DW1_PORT` en una copia temporal y verifica que el check sale con error nombrando el archivo

**Given** `tools/port/metrics/ps1_literals.py`
**When** se corre sobre `src/` e `include/`
**Then** genera `tools/port/metrics/ps1_literals.tsv` con cada literal `0x80xxxxxx` usado como puntero, su archivo, línea y estado (`traducido`, `no aplica: <motivo>` o `pendiente`)
**And** `preproc_check.sh` falla si el inventario versionado no coincide con el generado

**Given** `tools/port/metrics/isr_spins.py`
**When** se corre sobre `src/`
**Then** genera `tools/port/metrics/isr_spins.tsv` con los loops que esperan un flag sin llamar a PsyQ (incluye `MOV_waitForFlip` en `src/mov/mov.c`), cada uno con estado

### Story 2.3: Memoria PS1, enlaces de OT y reinicio de overlays

Como desarrollador del port,
quiero la arena PS1, el scratchpad, `PS1_LINK` y el snapshot/restore por overlay,
para que el Juego use direcciones PS1 y que cada recarga de overlay empiece con sus datos iniciales.

**Criterios de aceptación:**

**Given** `port/psyq/ps1mem.{h,c}` y el preset `native-asan`
**When** corren los tests L1 de memoria
**Then** `PS1_RAM(0x80010000)` apunta a `Ps1_Ram + 0x10000`, una dirección fuera de RAM dispara `PORT_ASSERT`, el scratchpad tiene 0x400 bytes con assert de rango, y `PS1_LINK_PTR(PS1_LINK(p)) == p` para punteros dentro de la ventana de 16 MB
**And** el binario nativo se linkea con `-no-pie -Wl,-Ttext-segment=0x01000000` y un test verifica que globals, arena y stack del hilo del juego caen dentro de la ventana

**Given** las TUs envoltorio generadas por CMake para cada TU de overlay (sección `dw1ovl_<ovl>`)
**When** corre el test L1 de reinicio (derivado de `tools/port/env_check/overlay_sections/`)
**Then** al llamar `port_overlay_loaded(ovl)` todas las variables globales, `static` de archivo y `static` locales del overlay vuelven a su valor inicial, en `-O0` y `-O2`
**And** las secciones de los 16 overlays existen en el binario con sus símbolos `__start_`/`__stop_`

### Story 2.4: Traducción de direcciones PS1 en `src/` e `include/`

Como desarrollador del port,
quiero las macros y tablas con direcciones fijas traducidas con `PS1_RAM()` detrás de `DW1_PORT`,
para que los buffers del Juego (`GENERAL_BUFFER`, `TEXTURE_BUFFER`, `REL_BIN_OFFSETS`, …) caigan en la arena.

**Criterios de aceptación:**

**Given** el inventario de la Historia 2.2
**When** se aplican los bloques `#ifdef DW1_PORT ... #else <original> #endif`
**Then** `ps1_literals.tsv` no tiene ninguna fila `pendiente`
**And** cada bloque tiene una fila en `docs/port/src-changes.md` y `preproc_check.sh` sigue saliendo con 0

**Given** el cargador de animaciones `loadMMD` (`src/main/model.c`)
**When** se revisa cómo resuelve los offsets de `CHDAT/*.MMD`
**Then** el resultado (offsets relativos o punteros absolutos) queda en `docs/research/hardware-access.md` con la evidencia (líneas de código)
**And** si hay punteros absolutos, la historia agrega el caso al inventario y lo traduce antes de cerrar

### Story 2.5: Reloj virtual y semántica de `VSync`

Como desarrollador del port,
quiero `port/psyq/clock.c` como dueño único del tiempo,
para que las esperas del Juego terminen siempre y en el mismo orden en cada corrida.

**Criterios de aceptación:**

**Given** `clock.c` con contador a 33.868.800 Hz y cola de eventos
**When** corren los tests L1 del reloj
**Then** 35.964 VBlanks (10 min) avanzan exactamente 10 × 60 × 60000/1001 VBlanks de tiempo con el acumulador racional, sin deriva (NFR-1)
**And** `VSync(0)`, `VSync(n>1)`, `VSync(1)` y `VSync(-1)` devuelven lo que define AD-8 en casos de prueba propios

**Given** un callback registrado con `VSyncCallback` y un root counter programado con `SetRCnt`
**When** el test hace encuestas en loop (`VSync(-1)`, `TestEvent`)
**Then** el loop termina porque cada entrada PsyQ avanza el reloj, los callbacks corren solo en el hilo que llama, y dos corridas producen la misma traza de eventos byte a byte

### Story 2.6: Linkeo completo y arranque hasta el frame 0

Como desarrollador del port,
quiero un binario `native-headless` que linkea el Juego entero con stubs generados para toda función PsyQ faltante,
para que el Juego arranque y se vea qué función PsyQ necesita a continuación.

**Criterios de aceptación:**

**Given** `tools/port/psyq/gen_stubs.py`, que genera un stub por cada una de las 202 funciones de `docs/research/psyq-usage.md` sin implementar
**When** se corre el build `native-headless`
**Then** el binario linkea sin símbolos sin resolver, con `crt.c` llamando a `main` del Juego y el heap de `__rts_info_t__.h` dentro de la arena (AD-4)
**And** cada stub registra su primera llamada en `port_trace.log`

**Given** el binario y una arena sin datos del disco
**When** se ejecuta `dw1 --headless --frames 1`
**Then** llega al retorno del primer `VSync` desde `main` (frame 0 de AD-15) y sale con código 0
**And** `port_trace.log` lista, en orden, los stubs llamados antes del frame 0
**And** el contador `PsyQ implementadas/202` sale del comentario de fuente (`dw2-pc:` o `asm:`) de cada función en `port/psyq/` y se imprime en el log del build

### Story 2.7: Manifest y herramienta nativa del Pack

Como desarrollador del port,
quiero validar mi Imagen del disco y construir el Pack con una herramienta nativa,
para que el Juego nativo lea los archivos del disco y el Pack web tenga una referencia byte a byte.

**Criterios de aceptación:**

**Given** `port/host/pack/` (BIN/CUE, ISO9660, validación, escritura del Pack) y un disco sintético generado por los tests
**When** corren los tests L1 del Pack
**Then** un disco sintético correcto produce un Pack con `format_version`, el SHA-256 del Manifest, el índice y los datos alineados a 2048 bytes en orden de LBA
**And** un byte cambiado en cualquier archivo hace fallar la validación nombrando el archivo, un `.cue` con modo distinto de `MODE2/2352` y un `.iso` de 2048 B/sector se rechazan con mensaje, y en ningún caso queda Pack escrito

**Given** la Imagen del disco USA en `DW1_DISC_DIR` (clase B)
**When** se corre `dw1-pack manifest` y luego `dw1-pack build`
**Then** `tools/port/manifest/us.tsv` tiene una fila por archivo (ruta, LBA, sectores, modo `F1`/`F2`, tamaño, SHA-256) más la de `SLUS_010.32`, sin contenido del disco, y el hook pre-commit lo acepta
**And** el Pack pasa la validación y dos construcciones dan el mismo SHA-256

### Story 2.8: `libcd` y modelo del CD sobre el Pack

Como desarrollador del port,
quiero `CdSearchFile`, `CdRead`, `CdReadSync`, `CdSync`, `CdControl` y el resto de `libcd` que usa el Juego sobre el Pack,
para que el Juego cargue archivos y overlays como en la consola.

**Criterios de aceptación:**

**Given** un Pack sintético
**When** corren los tests L1 de `libcd`
**Then** `CdSearchFile` resuelve por ruta ISO9660, `CdRead` lee por LBA en modo F1 y `CdlModeSize1` (2340 bytes, con cabecera MSF reconstruida), y leer fuera de un archivo del índice aborta en debug
**And** una lectura de N sectores termina en el reloj virtual a 75 o 150 sectores/s según `CdlModeSpeed`, más el seek fijo, y `CdReadCallback` corre al despachar ese evento

**Given** el Pack real (clase B) y el binario `native-headless`
**When** el Juego corre hasta cargar su primer `*_REL.BIN`
**Then** el archivo queda en `PS1_RAM(REL_BIN_OFFSETS[i])` con el SHA-256 del Manifest y `port_overlay_loaded` se llama una vez por pedido

### Story 2.9: `libc`, `libapi` y `libetc` deterministas

Como desarrollador del port,
quiero `rand`, `malloc3`/`free3`/`InitHeap3`, `setjmp`/`longjmp`, las funciones de cadenas, `ResetCallback`, `PadInit`/`PadRead` y `VSyncCallback` con la semántica de `mw_lib41`,
para que el Juego tome las mismas decisiones que en la consola.

**Criterios de aceptación:**

**Given** las implementaciones traducidas de `asm/us/main/psyq/libc.s`, `libapi.s` y `libetc.s`
**When** corren los tests L1
**Then** la secuencia de los primeros 10.000 valores de `rand` tras `srand` con semillas de prueba coincide con la del algoritmo traducido del asm, y el heap de `malloc3` devuelve las mismas direcciones PS1 para una secuencia de pedidos de prueba
**And** cada función tiene el comentario `asm:` con su fuente y el contador de la Historia 2.6 sube en consecuencia

### Story 2.10: Modelo GTE y macros inline

Como desarrollador del port,
quiero el GTE del Backend (registros, `RTPS`, `RTPT`, `NCLIP`, `AVSZ3/4`, `MVMVA`, `NCDS`, etc.) detrás de las macros GTE del port,
para que la geometría 3D del Juego se calcule con la aritmética de la PS1.

**Criterios de aceptación:**

**Given** `port/backend/gte.{h,c}` adaptado de DW2-PC con atribución
**When** corren los tests L1 del GTE
**Then** cada operación usada por las 11 macros inline (181 usos) da los resultados, flags y saturación esperados en vectores de prueba propios escritos a partir de la especificación del GTE
**And** los 23 TUs que no compilaban a wasm32 por las macros GTE compilan también con el preset `web` (sin linkear)

### Story 2.11: `libgte` y tablas leídas del exe en el Pack

Como desarrollador del port,
quiero las 56 funciones de `libgte` que llama el Juego y las tablas `rcossin_tbl`/`ratan_tbl` cargadas desde `SLUS_010.32` en el Pack,
para que las matrices y los ángulos coincidan con la consola sin embeber datos de Sony.

**Criterios de aceptación:**

**Given** un header generado con los offsets de las tablas desde `config/us/main.yaml`
**When** el port arranca con el Pack real (clase B)
**Then** las tablas se cargan desde `SLUS_010.32` y su SHA-256 coincide con el de la región del exe indicada en `config/us/main.yaml`
**And** no hay `.incbin` ni datos del exe en archivos nuevos del repo (verificado por el hook pre-commit y por `grep` en `port/`)

**Given** las funciones traducidas de `asm/us/main/psyq/libgte.s`
**When** corren los tests L1 con tablas sintéticas
**Then** `RotMatrix`, `TransMatrix`, `MulMatrix0`, `ApplyMatrix`, `ratan2`, `rsin`/`rcos` y el resto dan los resultados del algoritmo traducido en vectores de prueba

### Story 2.12: Renderer software y `libgpu`

Como desarrollador del port,
quiero `port/backend/gpu.h` con el Renderer software (VRAM 1024×512×16) y las 28 funciones de `libgpu` que usa el Juego,
para que el Juego dibuje en una VRAM como la de la PS1.

**Criterios de aceptación:**

**Given** el Renderer software adaptado de `dw2-pc/backend/psxgpu.c` con atribución
**When** corren los tests L1 del rasterizador
**Then** cada tipo de primitiva usada (polígonos F/G/FT/GT de 3 y 4 vértices, sprites, líneas, tiles, transferencias de VRAM) produce un SHA-256 de VRAM fijo en casos sintéticos, con dithering, semitransparencia y ventana de textura
**And** `DrawOTag`, `LoadImage`, `StoreImage`, `MoveImage`, `ClearImage`, `PutDispEnv`, `PutDrawEnv`, `ClearOTagR` y `DrawSync` hablan con el Backend solo por `gpu.h`, y `DrawSync` termina por costo en el reloj virtual

### Story 2.13: `libgs` con el pipeline TMD

Como desarrollador del port,
quiero las 35 funciones de `libgs` (incluidos `GsSortObject4J` y las 27 `GsTMD*` tomadas por dirección),
para que los modelos 3D del Juego se ordenen y dibujen como en la consola.

**Criterios de aceptación:**

**Given** las funciones traducidas de `asm/us/main/psyq/libgs.s`
**When** corren los tests L1 con un TMD sintético
**Then** `GsSortObject4J` genera la misma lista de primitivas y el mismo OT (comparados por SHA-256) que la traza de referencia escrita a partir del algoritmo traducido
**And** `GsInitGraph`, `GsDefDispBuff`, `GsSwapDispBuff`, `GsSortClear`, `GsDrawOt` y el doble buffer funcionan en un test que dibuja dos frames alternados

### Story 2.14: Stubs con comportamiento definido para audio, video y Memory card

Como desarrollador del port,
quiero `libsnd`, `libspu`, `libpress`, `libds` y `libmcrd` en versión mínima documentada (silencio, video que termina, tarjeta ausente),
para que el arranque llegue al título antes de tener el audio y el video reales (Épica 3).

**Criterios de aceptación:**

**Given** la lista de funciones de esas librerías en `docs/research/psyq-usage.md`
**When** se implementan como mínimas
**Then** cada una tiene el comentario `stub-m2:` con el comportamiento elegido, y `docs/port/stubs.md` las lista con la historia de la Épica 3 que las reemplaza
**And** un test L1 verifica que el stream de `playMovie(0)` termina en un número fijo de frames y que `MemCardSync` informa "sin tarjeta" sin bloquear

### Story 2.15: Pantalla de título en nativo headless

Como desarrollador del port,
quiero correr el Juego headless con el Pack real hasta la pantalla de título y volcar su captura,
para que M2 quede cerrado con evidencia (SM-1, parte nativa).

**Criterios de aceptación:**

**Given** el Pack real, el binario `native-headless` y la Escena `tools/port/scenes/title.toml` (entrada: ninguna; captura en el VBlank k posterior a la primera llamada a `runLandingScreen`)
**When** se corre `tools/port/scenes/run.py title --build native-headless`
**Then** la corrida llega a `runLandingScreen`, vuelca la captura del área de display según GP1 fuera del repo, y la captura no es uniforme (más de 16 colores distintos)
**And** dos corridas dan el mismo SHA-256 de captura y la misma traza de eventos del reloj

**Given** la misma corrida en `native-asan`
**When** termina
**Then** ASan y UBSan no reportan errores, y no quedan filas `pendiente` en `isr_spins.tsv` para el camino hasta el título
**And** `docs/STATUS.md` registra "Escenas que funcionan (nativo): 1 (título, sin referencia)"

## Epic 3: Paridad nativa contra el Emulador de referencia (M3)

El Build nativo se ve, suena y guarda como la consola. Toda comparación usa los puntos de medición y
las tolerancias de AD-15, contra PCSX-Redux fijado por SHA con OpenBIOS, en la clase B.

### Story 3.1: Emulador de referencia y captura automática

Como desarrollador del port,
quiero PCSX-Redux en la imagen `dw-port` con un script que captura frames, audio y variables de estado en los mismos puntos que el port,
para tener Frames de referencia sin intervención manual.

**Criterios de aceptación:**

**Given** PCSX-Redux compilado desde un SHA fijo en el Dockerfile, con OpenBIOS
**When** se corre `tools/port/reference/capture.py title` con el disco de `DW1_DISC_DIR`
**Then** el script detecta el frame 0 por breakpoint en el retorno del primer `VSync` desde `main`, aplica los inputs de la Escena en el latch de cada VBlank y guarda la captura del área de display según GP1 fuera del repo
**And** dos corridas dan el mismo SHA-256 de captura y el repo solo recibe el hash en `title.toml`

**Given** la captura de referencia tomada con OpenBIOS
**When** el dueño la repite una vez con su BIOS real fuera del repo (pregunta abierta 3 del PRD)
**Then** el resultado (iguales o con diferencias listadas) queda en `docs/port/reference.md`

### Story 3.2: Runner de Escenas y pantalla de título exacta

Como desarrollador del port,
quiero `tools/port/scenes/run.py` comparando el port contra la referencia,
para que FR-4 y NFR-8 se verifiquen solos.

**Criterios de aceptación:**

**Given** `title.toml` con el SHA-256 de referencia
**When** se corre `run.py title --build native-headless --compare`
**Then** la captura del port es idéntica a la de referencia (comparación de RGB555 exacta) y el comando sale con 0
**And** ante una diferencia, el comando sale con error, guarda fuera del repo una imagen de diferencias e imprime la cantidad de píxeles distintos y su caja envolvente

**Given** una Escena con una excepción declarada en su `.toml` (región y motivo)
**When** la diferencia cae dentro de la región
**Then** la Escena pasa y el informe lista la excepción usada

### Story 3.3: Inputs por VBlank y comparación de estado

Como desarrollador del port,
quiero reproducir una secuencia de inputs por VBlank y comparar las variables de `state_vars.txt` en ambos lados,
para detectar divergencias de lógica aunque los frames coincidan.

**Criterios de aceptación:**

**Given** `tools/port/scenes/state_vars.txt` con variables nombradas por símbolo y tipadas
**When** `run.py` corre una Escena con inputs hasta un punto de control
**Then** los valores del port y de la referencia son idénticos, con punteros traducidos (arena → `0x80000000 | offset`, global nativo → símbolo + offset)
**And** una divergencia nombra la primera variable distinta y el VBlank en que apareció, y `rand` en la referencia y en el port dan la misma secuencia en una Escena con decisiones aleatorias

### Story 3.4: SPU emulado y `libspu`

Como desarrollador del port,
quiero el SPU del Backend (24 voces ADPCM, ADSR, reverb, entrada de CD) y `libspu` traducida del asm de DW1,
para que el audio se genere con la lógica de la consola.

**Criterios de aceptación:**

**Given** `port/backend/spu.{h,c}` adaptado de `dw2-pc/backend/psxspu.c` con atribución
**When** corren los tests L1
**Then** decodificar bloques ADPCM sintéticos, envolventes ADSR y la reverb dan muestras con SHA-256 fijo, y el SPU corre hasta el tiempo virtual antes de cada escritura de registro
**And** en 35.964 VBlanks el SPU genera exactamente 44.100 × 1001/60000 × 35.964 muestras, con acumulador exacto

### Story 3.5: `libsnd` de DW1 y traza de audio

Como desarrollador del port,
quiero el secuenciador `libsnd` traducido de `mw_lib41` con su tick por root counter (`SS_TICK240`) y la comparación de audio de AD-15,
para que la música y los efectos suenen como en la consola (FR-7).

**Criterios de aceptación:**

**Given** las 21 funciones de `libsnd` traducidas con comentario `asm:` y la Escena de audio `title_audio.toml`
**When** `run.py title_audio --compare-audio` corre contra la referencia
**Then** la traza de escrituras a registros SPU con timestamp en muestras es idéntica
**And** el PCM alineado por correlación cruzada da SNR ≥ 40 dB en ventanas de 5 s, y los stubs `stub-m2:` de `libsnd`/`libspu` ya no existen

### Story 3.6: XA y sectores Form 2

Como desarrollador del port,
quiero leer sectores F2 del Pack, decodificar XA y mezclarlo en la entrada de CD del SPU,
para que las voces y pistas XA suenen como en la consola.

**Criterios de aceptación:**

**Given** `port/backend/xa.{h,c}` y una Escena con XA (`xa_*.toml`) definida en esta historia
**When** se compara contra la referencia
**Then** la traza SPU es idéntica y el PCM da SNR ≥ 40 dB
**And** un test L1 decodifica sectores XA sintéticos (4 y 8 bits, mono y estéreo) a un SHA-256 fijo

### Story 3.7: MDEC, `libpress` y videos STR

Como jugador,
quiero ver los videos del disco con su sonido y poder saltarlos con Start,
para tener la intro y las cinemáticas como en la consola (FR-6).

**Criterios de aceptación:**

**Given** `port/backend/mdec.{h,c}`, `libpress` traducida y las tablas VLC leídas del Pack (AD-12)
**When** `run.py movie0 --compare` corre la intro (`playMovie(0)`) contra la referencia
**Then** cada frame de video tiene diferencia absoluta máxima por canal ≤ 4/255 y media ≤ 0,5/255
**And** al final del video audio y video difieren en ≤ 1 frame de video, y la Escena `movie0_skip.toml` (Start en el VBlank k) sale del video en el mismo VBlank que la referencia

**Given** el reemplazo de los stubs de video de la Historia 2.14
**When** se corre `title.toml`
**Then** sigue pasando exacta contra la referencia con el video real

### Story 3.8: Memory card nativa

Como jugador,
quiero que lo que guardo en el Juego quede en un `.mcd` compatible con los emuladores,
para conservar mis partidas (FR-10, FR-11 en nativo).

**Criterios de aceptación:**

**Given** las 11 funciones de `libmcrd` traducidas y `port/host` con `.mcd` escrito por temporal + `rename`
**When** una Escena guarda en la ranura 1 y otra corrida carga ese save con "Continue"
**Then** el estado cargado coincide con el guardado según `state_vars.txt`
**And** el `.mcd` resultante tiene 131.072 bytes y PCSX-Redux lo carga y muestra el save en una corrida automática de `capture.py`

**Given** un proceso que se mata con `SIGKILL` durante la escritura (test L1 con punto de corte inyectado)
**When** se reabre la tarjeta
**Then** contiene el estado anterior o el nuevo, nunca uno mezclado

### Story 3.9: Preset `native` con ventana SDL3

Como desarrollador del port,
quiero jugar el Build nativo en una ventana con teclado, gamepad y audio,
para depurar a mano con el mismo código que el web (FR-17).

**Criterios de aceptación:**

**Given** SDL3 3.4.16 i686 agregado a la imagen `dw-port`
**When** se corre `cmake --preset native && cmake --build --preset native`
**Then** produce un ejecutable que abre una ventana y llega al título; en CI se verifica con `SDL_VIDEODRIVER=offscreen` y `SDL_AUDIODRIVER=dummy` comparando la captura con la de `native-headless`
**And** el Host remuestrea el audio a la frecuencia del dispositivo con ratio ajustado por nivel del buffer (AD-10), y un test L1 con un dispositivo simulado al 101 % y al 99 % mantiene el buffer dentro de la banda sin underruns en 10 minutos simulados

### Story 3.10: Calibración de costos y ritmo nativo

Como desarrollador del port,
quiero las constantes de costo de CD, GPU y MDEC calibradas contra la referencia,
para que las cargas y los videos tarden lo mismo que en la consola.

**Criterios de aceptación:**

**Given** las Escenas `title`, `movie0` y una carga de mapa
**When** se mide en cuántos VBlanks llega cada punto de control en el port y en la referencia
**Then** la diferencia es 0 VBlanks en cada punto, y las constantes quedan en `port/psyq/clock_costs.h` con la Escena que las calibró
**And** 10 minutos de `native` con reloj de pared dan un conteo de VBlanks que difiere del teórico en < 1 (NFR-1 nativo)

### Story 3.11: Hipótesis pendientes de los overlays

Como desarrollador del port,
quiero verificar si `SHOP_REL.BIN` se usa en USA,
para cerrar la hipótesis de `docs/research/overlays.md` con evidencia.

**Criterios de aceptación:**

**Given** una Escena que entra a una tienda (`shop_*.toml`)
**When** corre en el port y en la referencia con un breakpoint en la carga de `SHOP_REL.BIN`
**Then** `docs/research/overlays.md` registra si el archivo se carga y desde qué función, y la Escena pasa contra la referencia

## Epic 4: Jugar en el navegador con el disco propio (M4)

El mismo código corre con Emscripten en el hilo del juego (AD-7), lee el Pack desde OPFS por el
worker de E/S (AD-13) y se presenta por WebGL2 en un `OffscreenCanvas` (AD-9). Los tests usan
Playwright 1.64.0 en Chromium y Firefox (NFR-9).

### Story 4.1: El Juego en wasm con su propio hilo y presentación

Como desarrollador del port,
quiero el preset `web` corriendo el Juego en un pthread con stack estático dentro de la ventana de 16 MB y presentando el Renderer software por WebGL2,
para validar el modelo de hilos y memoria de AD-5 y AD-7 en los dos navegadores.

**Criterios de aceptación:**

**Given** el build `web` con `-pthread -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`, `-sINITIAL_MEMORY` fijo y sin crecimiento
**When** un test Playwright abre la página de desarrollo servida con COOP/COEP
**Then** el hilo del juego arranca con `pthread_attr_setstack` sobre el buffer estático, un `PORT_ASSERT` verifica que el stack está en `[0, 16 MB)`, y el Juego llega al frame 0 en Chromium y Firefox
**And** si Emscripten no respeta el stack, la historia aplica el fallback `-sPROXY_TO_PTHREAD` de AD-7 y lo registra en la memlog de la arquitectura

**Given** el `OffscreenCanvas` transferido al hilo del juego
**When** `plat_present` presenta un frame del Renderer software
**Then** el contenido leído del canvas en el test coincide píxel a píxel con el frame de VRAM convertido
**And** el test mide y registra el tiempo de presentación por frame en ambos navegadores (sin umbral; insumo de la Épica 5)

### Story 4.2: Worker de E/S y pantalla de título en el navegador

Como desarrollador del port,
quiero un worker de E/S dueño de OPFS que sirve sectores al hilo del juego por un buzón en `SharedArrayBuffer`,
para que el Juego lea el Pack de forma síncrona en el navegador.

**Criterios de aceptación:**

**Given** `port/web/io_worker.js` con el handle de OPFS abierto antes de arrancar el Juego, y un Pack copiado a OPFS por el fixture de test (clase B)
**When** se corre la Escena `title` en el build `web`
**Then** la captura es idéntica a la de `native-headless` (SM-1, parte web; la comparación contra la referencia llega con la Historia 3.2, que corre en ambos builds)
**And** el hilo del juego nunca llama a APIs de OPFS (verificado por un test que reemplaza `navigator.storage` en el hilo del juego por un objeto que falla)

**Given** un Pack sintético (clase A)
**When** un test pide lecturas concurrentes desde el hilo del juego
**Then** cada lectura devuelve los bytes esperados y el hilo del juego espera con `Atomics.wait` sin espera activa

### Story 4.3: Selección del disco, validación y Pack en el navegador

Como jugadora,
quiero elegir mi `.bin`/`.cue`, ver el progreso de la validación y quedar lista para jugar,
para no instalar nada (UJ-1, FR-1, FR-2, FR-3).

**Criterios de aceptación:**

**Given** la Shell web y el C de `port/host/pack` compilado para el worker de E/S
**When** el test elige o arrastra archivos sintéticos (clase A)
**Then** se acepta un `.bin` `MODE2/2352` con y sin `.cue`; se rechazan un `.cue` con `TRACK 01` de otro modo y un `.iso` de 2048 B/sector con un mensaje que nombra el problema; un disco con un byte cambiado falla nombrando el archivo; una imagen con el ejecutable de JP o PAL se rechaza nombrando SLUS-01032; y tras cualquier falla no queda Pack en OPFS
**And** la lectura es por partes y la memoria de la pestaña no supera 512 MB durante la validación (medida con `performance.measureUserAgentSpecificMemory` en Chromium)

**Given** la Imagen del disco real (clase B)
**When** se construye el Pack en el navegador
**Then** su SHA-256 es igual al del Pack de la herramienta nativa (Historia 2.7), se mide el tiempo hasta "Listo para jugar" (SM-4) y, al recargar la página, la Shell ofrece "Jugar" sin pedir el disco
**And** la Shell llama a `navigator.storage.persist()` e informa si se deniega, y "Borrar datos" elimina el Pack

### Story 4.4: Pantallas de la Shell web y capacidades

Como jugador,
quiero una Shell web que me explique qué necesito, me avise si mi navegador no sirve y me deje pausar,
para entender qué pasa en cada momento (FR-15).

**Criterios de aceptación:**

**Given** la Shell web con bienvenida y aviso legal, selección de disco, progreso, "Jugar", opciones, Memory cards y borrar datos
**When** corren los tests L3a
**Then** sin `crossOriginIsolated`, sin OPFS o sin `SharedArrayBuffer` la Shell muestra un mensaje específico por capacidad faltante y no arranca el Juego
**And** una segunda pestaña con el Web Lock `dw1-game` tomado muestra un mensaje y no arranca el Juego

**Given** el Juego corriendo
**When** el test pulsa "Pausa" o simula `visibilitychange` a oculto
**Then** el reloj de pared se detiene, el conteo de VBlanks no avanza durante la pausa, y al volver el Juego sigue sin contar frames perdidos por la pausa (AD-8)
**And** "Jugar" es el único gesto necesario para llegar al título con audio (FR-4)

### Story 4.5: Teclado y gamepad

Como jugador,
quiero jugar con teclado o con cualquier gamepad estándar con un mapeo por defecto,
para empezar sin configurar nada (FR-8).

**Criterios de aceptación:**

**Given** el muestreo de input en el hilo principal escrito una vez por VBlank en un `SharedArrayBuffer`
**When** el test envía teclas y un gamepad simulado (`navigator.getGamepads` reemplazado en el test)
**Then** los 14 botones digitales llegan a `PadRead` con el mapeo por defecto documentado en `docs/port/controls.md`, aplicados en el latch del VBlank en que se muestrearon
**And** conectar y desconectar el gamepad simulado no detiene el Juego ni cambia el conteo de VBlanks, y con la opción activa el stick izquierdo actúa como D-pad

### Story 4.6: Remapeo y opciones persistentes

Como jugador,
quiero reasignar cada botón y que el cambio se mantenga,
para jugar con los botones donde los quiero (FR-9).

**Criterios de aceptación:**

**Given** `PortSettings` en IndexedDB con `schema_version`
**When** el test reasigna un botón, recarga la página y lo pulsa
**Then** el Juego recibe el botón PS1 reasignado
**And** "Restaurar valores por defecto" vuelve al mapeo de `docs/port/controls.md`, y una base con `schema_version` anterior se migra sin perder datos (test con una base sembrada)

### Story 4.7: Audio en el navegador

Como jugador,
quiero el audio del Juego por un AudioWorklet con volumen ajustable,
para escuchar el Juego sin cortes (FR-7).

**Criterios de aceptación:**

**Given** el ring buffer en `SharedArrayBuffer` y `port/web/audio_worklet.js`
**When** corre la Escena `title_audio` en el build `web`
**Then** el PCM capturado del worklet antes del remuestreo es idéntico al de `native-headless`
**And** el volumen general de la Shell escala la salida en el Host y persiste en `PortSettings`; en un test de 10 minutos con reloj de pared en Chromium y Firefox, el contador de underruns del worklet es 0 (umbral final en hardware de referencia: Épica 5)

### Story 4.8: Memory cards en el navegador

Como jugador,
quiero que mis saves persistan, y poder importar y exportar mis Memory cards,
para seguir mi partida y moverla entre el port y un emulador (UJ-2, UJ-3, FR-10, FR-11).

**Criterios de aceptación:**

**Given** IndexedDB con una entrada de 131.072 bytes por ranura escrita en una transacción con `durability: 'strict'`
**When** el test guarda en el Juego, cierra la página, la reabre y elige "Continue" (clase B)
**Then** el save se carga, y una copia se envía al hilo principal al terminar cada escritura de `libmcrd` y en `pagehide`
**And** cerrar la página en medio de una escritura (test con corte inyectado) deja el estado anterior o el nuevo

**Given** la pantalla "Memory cards" con dos ranuras
**When** el test importa un `.mcd` creado con PCSX-Redux por `capture.py` y exporta la ranura 1
**Then** el Juego carga el save importado, PCSX-Redux carga el `.mcd` exportado y muestra el save, y un archivo de tamaño distinto de 131.072 bytes se rechaza con mensaje
**And** si `navigator.storage.persist()` fue denegado, la Shell sugiere exportar la Memory card

### Story 4.9: Offline y actualizaciones

Como jugador,
quiero que el port funcione sin conexión después de la primera carga y que se actualice al recargar,
para jugar en cualquier lado sin perder nada (FR-16).

**Criterios de aceptación:**

**Given** `port/web/sw.js` con una caché versionada de los archivos del programa
**When** el test carga la página, pasa a modo offline y recarga
**Then** la Shell abre y el Juego arranca con el Pack guardado
**And** al publicar una versión nueva en el servidor de test y recargar, la versión nueva queda activa sin perder Pack ni Memory cards, y un Pack con `format_version` anterior se migra sin pedir el disco (test con un Pack sintético de la versión anterior)

### Story 4.10: Checks del build web: ritmo, tamaño, reproducibilidad, red y datos

Como dueño del proyecto,
quiero los checks automáticos de NFR-1 web, NFR-3, NFR-4, NFR-5 y NFR-7,
para que ningún merge empeore estas garantías sin que el CI lo marque.

**Criterios de aceptación:**

**Given** el build `web`
**When** corren los checks
**Then** `tools/port/ci/size_check` falla si el `.wasm` con Brotli supera 4 MB o la primera carga sin Pack supera 6 MB, y dos builds del mismo commit en la imagen `dw-port` dan el mismo `.wasm` byte a byte
**And** en 10 minutos de juego con reloj de pared el conteo de VBlanks difiere del teórico en < 0,1 %, y el registro de red de Playwright no muestra pedidos fuera del origen del deploy

**Given** `tools/port/ci/data_leak_check` en la clase B
**When** corre sobre los artefactos web y el árbol del repo
**Then** toda ventana de 64 bytes del disco presente en un artefacto aparece también en los objetos compilados de `src/`, los objetos de `port/` no tienen ninguna, y un test con un archivo de `port/` contaminado con 64 bytes del disco hace fallar el check

## Epic 5: Mejoras de video y 59,94 Hz estables (M5)

El Renderer WebGL2 implementa `gpu.h` (AD-9); las mejoras usan el canal lateral del GTE y por
defecto están apagadas. Las mediciones de rendimiento son L4, en el hardware de referencia.

### Story 5.1: Renderer WebGL2 idéntico a 1×

Como desarrollador del port,
quiero un Renderer WebGL2 que a 1× sin mejoras dé los mismos frames que el Renderer software,
para tener una base acelerada sin perder fidelidad.

**Criterios de aceptación:**

**Given** `port/backend/gpu_gl/` implementando `gpu.h`
**When** corren en el build `web` las Escenas que ya pasan con el Renderer software
**Then** cada captura es idéntica a la del Renderer software, salvo excepciones listadas en el `.toml` con su motivo
**And** los casos sintéticos de primitivas de la Historia 2.12 dan el mismo SHA-256 de VRAM con los dos Renderers

### Story 5.2: Escala de resolución de 1× a 4×

Como jugador,
quiero elegir la resolución interna del 3D,
para ver el Juego más nítido (FR-12).

**Criterios de aceptación:**

**Given** el canal lateral del GTE con x/y subpíxel, z y 1/w por vértice
**When** una Escena con 3D se captura a 2×, 3× y 4×
**Then** la imagen tiene N veces la resolución del área de display y, en un caso sintético, los bordes de un triángulo 3D caen en las posiciones subpíxel esperadas
**And** en una Escena solo 2D, la captura a N× reducida por vecino más cercano es idéntica a la de 1× (texturas y 2D conservan sus píxeles)

### Story 5.3: Sin wobble

Como jugador,
quiero activar vértices precisos y texturas con corrección de perspectiva,
para que los polígonos no tiemblen (FR-13).

**Criterios de aceptación:**

**Given** el log subpíxel del GTE de AD-15 y una Escena con cámara quieta (`wobble_*.toml`)
**When** corre con la opción activa
**Then** el desplazamiento máximo de cada vértice entre dos frames consecutivos es < 1 píxel de salida
**And** con la opción apagada la Escena sigue siendo idéntica a la del Renderer software

### Story 5.4: Relación de aspecto 16:9

Como jugador,
quiero 16:9 ampliando el campo de visión del 3D sin estirar la interfaz,
para usar toda la pantalla (FR-14).

**Criterios de aceptación:**

**Given** la opción 16:9 y las Escenas definidas hasta esta historia
**When** cada Escena se captura en 16:9
**Then** la interfaz 2D conserva su layout 4:3 (la región 2D centrada coincide con la captura 4:3) y las Escenas con fondos pre-renderizados donde no es viable se muestran en 4:3 con barras
**And** `docs/port/widescreen.md` lista por Escena si 16:9 es viable, con la evidencia (responde la pregunta abierta 2 del PRD)

### Story 5.5: Opciones de video en la Shell

Como jugador,
quiero elegir escala, sin wobble y aspecto desde las opciones de la Shell,
para cambiar la imagen sin recargar (FR-15).

**Criterios de aceptación:**

**Given** la pantalla de opciones de video
**When** el test cambia cada opción y recarga
**Then** el Renderer aplica la opción en el próximo frame y la opción persiste en `PortSettings`
**And** el valor por defecto es "original" (1×, opción sin wobble apagada, 4:3), y con él las capturas son idénticas a las del Renderer software

### Story 5.6: Rendimiento en el hardware de referencia

Como jugador,
quiero que el Juego no pierda frames en una computadora común,
para jugar a 59,94 Hz (NFR-2, SM-3).

**Criterios de aceptación:**

**Given** el script L4 `tools/port/perf/run_l4.py` con 10 minutos de juego grabado
**When** el dueño lo dispara en el hardware de referencia en Chrome y Firefox con el Renderer software a 1×
**Then** el informe muestra ≥ 99 % de frames a tiempo según el contador de frames perdidos de AD-8, y 0 underruns de audio
**And** el informe queda en `docs/port/perf/` con navegador, versión, equipo y commit; si no se llega al umbral, la historia sigue abierta con el perfil de los puntos calientes adjunto

## Epic 6: Partida completa sin divergencias (M6)

La suite de Escenas cubre los 16 overlays y corre en `native-headless` y en `web` contra la
referencia (L2, L3b). Las Memory cards de entrada y las referencias viven fuera del repo.

### Story 6.1: Definición de la suite de Escenas

Como desarrollador del port,
quiero la lista de Escenas por overlay con su punto de entrada,
para que FR-5 tenga una definición verificable de "terminado".

**Criterios de aceptación:**

**Given** los 16 overlays (`BTL`, `STD`, `FISH`, `EVL`, `KAR`, `VS`, `MOV`, `DOO2`, `DOOA`, `TRN`, `SHOP`, `DGET`, `TRN2`, `MURD`, `ENDI`, `EAB`)
**When** se escribe `tools/port/scenes/suite.toml`
**Then** cada overlay tiene al menos una Escena con id estable, punto de entrada (inputs desde el frame 0 o Memory card externa), frames a capturar y puntos de control de estado
**And** un check verifica que todo overlay de la lista de TUs de AD-1 aparece en la suite, las referencias de todas las Escenas se capturan con `capture.py` (clase B), y la viabilidad de 16:9 de cada Escena nueva se agrega a `docs/port/widescreen.md`

### Story 6.2: Mundo, combate y evolución

Como jugador,
quiero explorar, combatir y ver evolucionar a mi Digimon como en la consola,
para jugar el núcleo de la partida.

**Criterios de aceptación:**

**Given** las Escenas de `STD`, `BTL` y `EVL` de la suite
**When** corren en `native-headless` y en `web` con `--compare`
**Then** pasan frames, estado y audio contra la referencia (AD-15)
**And** el detector de aliasing de AD-6 registra cero eventos en esas Escenas o una lista justificada en sus `.toml`

### Story 6.3: Entrenamiento, tiendas y objetos

Como jugador,
quiero entrenar, comprar y conseguir objetos,
para progresar en la partida.

**Criterios de aceptación:**

**Given** las Escenas de `TRN`, `TRN2`, `SHOP` y `DGET`
**When** corren en `native-headless` y en `web` con `--compare`
**Then** pasan frames, estado y audio contra la referencia
**And** cero eventos de aliasing o lista justificada

### Story 6.4: Minijuegos y modo VS

Como jugador,
quiero pescar, jugar al curling y usar el modo VS contra la CPU,
para tener todas las actividades del Juego.

**Criterios de aceptación:**

**Given** las Escenas de `FISH`, `KAR` y `VS`
**When** corren en `native-headless` y en `web` con `--compare`
**Then** pasan frames, estado y audio contra la referencia
**And** cero eventos de aliasing o lista justificada

### Story 6.5: Videos, eventos y mapas especiales

Como jugador,
quiero las cinemáticas y los eventos especiales de la historia,
para seguir la trama completa.

**Criterios de aceptación:**

**Given** las Escenas de `MOV`, `DOO2`, `DOOA` y `MURD`
**When** corren en `native-headless` y en `web` con `--compare`
**Then** pasan frames (videos con la tolerancia MDEC), estado y audio contra la referencia
**And** cero eventos de aliasing o lista justificada

### Story 6.6: Final y créditos

Como jugador,
quiero llegar al final y ver los créditos,
para terminar la partida.

**Criterios de aceptación:**

**Given** las Escenas de `ENDI` y `EAB`, con una Memory card externa cerca del final
**When** corren en `native-headless` y en `web` con `--compare`
**Then** pasan frames, estado y audio contra la referencia hasta los créditos
**And** cero eventos de aliasing o lista justificada

### Story 6.7: Partida grabada sin divergencias

Como dueño del proyecto,
quiero reproducir una partida completa grabada en el port y en la referencia,
para probar que la lógica no diverge de principio a fin (FR-5).

**Criterios de aceptación:**

**Given** una secuencia de inputs por VBlank de una partida completa, grabada en PCSX-Redux por `capture.py` y guardada fuera del repo (en el repo solo su hash)
**When** se reproduce en `native-headless`
**Then** en cada punto de control las variables de `state_vars.txt` son idénticas a las de la referencia y la corrida llega a los créditos
**And** si el supuesto de determinismo de FR-5 falla, la historia documenta la primera divergencia (VBlank, variable, causa) en `docs/port/determinism.md` y sigue abierta

### Story 6.8: Regresión completa en CI

Como dueño del proyecto,
quiero la suite completa corriendo en la clase B,
para que ningún merge rompa una Escena que ya pasaba (SM-2).

**Criterios de aceptación:**

**Given** el job de regresión de la clase B (o su script equivalente mientras no haya forge, AD-16)
**When** corre en un merge a `main`
**Then** ejecuta todas las Escenas en `native-headless` y `web`, publica solo hashes y conteos, y falla si una Escena que pasaba deja de pasar
**And** `docs/STATUS.md` se actualiza con el número de Escenas que pasan en nativo y en web

## Epic 7: Deploy en el servidor propio (M7)

### Story 7.1: Contenedor nginx con COOP/COEP

Como dueño del proyecto,
quiero una imagen nginx que sirva solo los archivos del programa con los headers necesarios,
para alojar el port en mi servidor.

**Criterios de aceptación:**

**Given** `port/web/deploy/Dockerfile` con nginx, `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp`, tipos MIME de wasm y Brotli
**When** los tests L3a corren contra el contenedor local
**Then** la página queda `crossOriginIsolated` en Chromium y Firefox y todos los archivos responden con los headers esperados
**And** la imagen contiene solo los artefactos del build `web`, y `data_leak_check` pasa sobre el sistema de archivos de la imagen

### Story 7.2: Publicación privada en el servidor propio

Como dueño del proyecto,
quiero publicar la imagen en mi registry y desplegarla en un entorno no público,
para probar el deploy real antes de abrirlo.

**Criterios de aceptación:**

**Given** la imagen fijada por digest en el registry del GitLab propio
**When** el pipeline (o el script equivalente) la despliega en el entorno privado
**Then** un smoke test L3b contra esa URL construye el Pack, llega al título y guarda en la Memory card
**And** el registro de red del test confirma que no hay pedidos fuera del origen del deploy (NFR-5)

### Story 7.3: Publicación pública

Como dueño del proyecto,
quiero decidir cuándo el port queda accesible públicamente,
para mantener el control legal y operativo.

**Criterios de aceptación:**

**Given** la Historia 7.2 en verde
**When** el dueño aprueba explícitamente la publicación (punto de control humano de `CLAUDE.md`)
**Then** la aprobación queda registrada en `docs/STATUS.md` con fecha y digest de la imagen, y el smoke test de la Historia 7.2 pasa contra la URL pública

## Epic 8: Decomp, upstream y CI (continua)

Épica sin fin fijo: sus historias se repiten o se retoman cuando cambian `upstream/main`, la
disponibilidad de `cc_mips.dll` o el forge.

### Story 8.1: Métricas automáticas en `STATUS.md`

Como dueño del proyecto,
quiero que las métricas del proyecto se calculen solas,
para ver el avance real en cada sesión.

**Criterios de aceptación:**

**Given** `tools/port/metrics/status_table.py`
**When** se corre
**Then** imprime funciones del Juego en C / total, funciones PsyQ implementadas / 202 (por comentario `dw2-pc:`, `asm:` o `stub-m2:`), bloques `DW1_PORT` en `src/` e `include/`, filas de `docs/port/src-changes.md` y Escenas que pasan
**And** falla si la cantidad de bloques `DW1_PORT` no coincide con las filas de `src-changes.md` (NFR-10)

### Story 8.2: Pipeline de GitLab CI con clases A y B

Como dueño del proyecto,
quiero el pipeline de AD-16 en mi GitLab,
para que cada merge tenga evidencia sin correr nada a mano.

**Criterios de aceptación:**

**Given** el repo conectado al GitLab propio (decisión pendiente del dueño: cómo llega `origin` a GitLab) y la imagen `dw-port` en su registry
**When** se hace push de una rama
**Then** la clase A (L0a, L1, L3a, build web, NFR-3) corre en cualquier runner y la clase B (L0b, L2, L3b, NFR-4) solo en el runner protegido `dw1-data`, con `DW1_DISC_DIR` montado de solo lectura
**And** un merge request desde un fork no dispara la clase B (verificado con las `rules` del pipeline y un MR de prueba), y los logs de la clase B no contienen bytes del disco

### Story 8.3: Merge periódico de `upstream/main`

Como dueño del proyecto,
quiero un procedimiento automático para traer `upstream/main`,
para seguir el decomp sin conflictos grandes (NFR-10).

**Criterios de aceptación:**

**Given** `tools/port/upstream/merge_check.sh`
**When** se corre
**Then** hace un merge de prueba de `upstream/main` en una rama temporal, lista los archivos en conflicto y falla si alguno no está en `docs/port/src-changes.md`
**And** tras un merge real, L0a, L0b, L1 y las Escenas que pasaban siguen en verde, y la lista de TUs de AD-1 se regenera sin cambios inesperados

### Story 8.4: Registro de funciones no verificadas y `make compare`

Como dueño del proyecto,
quiero que toda función nueva en `src/` quede registrada para verificarla cuando haya compilador matching,
para no proponer a upstream código sin objdiff (reglas 2 y 4).

**Criterios de aceptación:**

**Given** `tools/port/ci/non_matching_check.py`
**When** se corre
**Then** falla si una función de `src/` que no está en `upstream/main` falta en `docs/port/non-matching.md`
**And** cuando `DW1_MWCC_DIR` existe, el job L0b corre `make` y `make compare`, y falla si el binario no es idéntico

## Validación final (paso 4)

- Cobertura: los 17 FR y los 10 NFR tienen al menos una historia (mapa de cobertura arriba).
- Sin plantilla de inicio (brownfield); la Historia 2.1 crea el árbol del Structural Seed.
- Dependencias: cada historia usa solo salidas de historias anteriores de su épica o de épicas
  anteriores. Dependencias externas al trabajo del agente: la Historia 8.2 y el registry de la
  Historia 7.2 esperan que el dueño conecte el repo a su GitLab; la Historia 5.6 necesita que el
  dueño corra L4 en el hardware de referencia; la 7.3 es un punto de control humano.
- Orden de ejecución (kickoff, Fase 5): Épica 2 completa, luego 4.1 y 4.2 (título en el
  navegador), luego la Épica 3 y el resto de la 4. La Épica 8 se intercala cuando se desbloquea.
- 57 historias: Épica 2 (15), 3 (11), 4 (10), 5 (6), 6 (8), 7 (3), 8 (4).
