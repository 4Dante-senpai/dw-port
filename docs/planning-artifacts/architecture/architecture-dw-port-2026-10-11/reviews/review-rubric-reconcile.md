---
title: 'Revisión del Architecture Spine — rúbrica y reconciliación de insumos'
target: ../ARCHITECTURE-SPINE.md
reviewer: revisor independiente (solo lectura)
date: 2026-10-11
inputs:
  - docs/planning-artifacts/prds/prd-dw-port-2026-10-11/prd.md
  - docs/planning-artifacts/prds/prd-dw-port-2026-10-11/addendum.md
  - docs/research/*.md
  - docs/port/kickoff.md (Fase 4, ítem 3)
  - CLAUDE.md
  - código: src/, include/, config/us/, mk/version/us.mk, Makefile, reference/dw2-pc @ 816ed0d
verdict: 'Aprobable con cambios: estructura sólida y los 10 ADR del kickoff existen, pero hay 1 hallazgo crítico (datos del disco dentro del .wasm por el C de upstream) y 4 altos que dejan divergir unidades o contradicen el código.'
---

# Revisión del spine — rúbrica y reconciliación

## Veredicto

El spine cubre las capas, la memoria, los overlays, los hilos, el renderer, el audio, el Pack, el
testing y el CI, y los 17 AD tienen reglas mayormente verificables. **No está listo para aprobar
tal cual:** un hallazgo crítico hace que el check NFR-4 falle por construcción, y cuatro hallazgos
altos dejan puntos de divergencia reales entre historias (pertenencia de archivos a overlays,
clasificación de CI, base de tiempo y audio, determinismo contra el emulador).

Severidades: **crítico** (bloquea la aprobación o hace imposible un requisito), **alto** (dos
unidades divergen o el AD contradice el código), **medio** (requisito que aterrizó a medias),
**bajo** (precisión, redacción o higiene).

## Resumen de hallazgos

| # | Sev. | AD / requisito | Hallazgo | Fix |
|---|---|---|---|---|
| C1 | crítico | NFR-4, AD-12, AD-4, AD-6, NFR-3 | El C de upstream lleva datos byte a byte del disco (tablas, texto y ~680 KB de "basura" residual en `*_bss_us.c`). Compilado al `.wasm`, el check de ventanas de 64 bytes de AD-16 falla siempre | AD nuevo: definir qué bytes de upstream se aceptan en el deploy y cómo se compilan los `*_bss_us.c` |
| H1 | alto | AD-6, AD-1 | "Cada `.c` de `src/<ovl>/`" es falso en USA: `vs_intro`, `vs_combat`, `vs_select` y `shop/script_menu` están linkeados en `main`; `vs_demo.c` y los `*_bss_{eu,jp*}.c` no son de USA | La lista de TUs por unidad sale de `mk/version/us.mk` / `config/us/*.yaml` |
| H2 | alto | AD-16, AD-14 L0 | `make TOOLCHAIN=gcc` necesita `asm/` (generado por splat desde `disks/`, ignorado por git), y `make compare` necesita `cc_mips.dll`: los dos son clase B, no A | Partir L0: L0a preprocesado (A), L0b build gcc + compare (B) |
| H3 | alto | AD-8, AD-10, FR-7, FR-15, NFR-1 | CD 2× = 150 sectores/s ≈ **2,5** por VBlank, no 5; el SPU da 735,735 muestras por VBlank; no hay regla para fracciones, deriva del reloj de audio, pausa o pestaña oculta, ni frames tarde | Acumuladores racionales, remuestreo adaptativo, rebase del deadline al pausar y política de frames tarde en AD-8/AD-10 |
| M1 | medio | FR-5, AD-14, AD-15 | La partida grabada contra el emulador no tiene ancla de inputs (la BIOS cambia el conteo de VBlanks desde el arranque) ni sincronización tras cargas de CD. El supuesto del memlog ("checkpoints tras cargas") no aterrizó | Anclar inputs a un contador del juego y poner checkpoints después de cada carga |
| M2 | medio | AD-3, FR-17, CLAUDE.md | Native i686 contradice "Linux x64" del PRD y de CLAUDE.md; la imagen tiene SDL3 de 64 bits (`sizeof(void*)=8` en `setup.md`) | Aprobación explícita del dueño + historia de toolchain i386 |
| M3 | medio | AD-5 | "stack antes de `__heap_base`" es falso para el hilo del juego: con `PROXY_TO_PTHREAD` su stack sale del heap y en native de `mmap` | Stack del hilo del juego en un buffer estático dentro de la ventana (`pthread_attr_setstack`) |
| M4 | medio | AD-17 | `-DVERSION=US` es incorrecto: upstream usa `-DVERSION_US` (`Makefile:26`). Tampoco se mencionan los headers generados `text/*.yaml` → `gen_text.py` | Corregir el flag y reutilizar los generadores de upstream |
| M5 | medio | FR-16, AD-11, AD-13 | No hay versión de formato del Pack ni del esquema de IndexedDB/`PortSettings`: un deploy nuevo podría invalidar el Pack, y rearmarlo exige otra vez el disco | Versión en la cabecera del Pack y en el esquema; regla de compatibilidad o migración sin disco |
| M6 | medio | FR-10, AD-13 | No dice *cuándo* se toma la copia de la Memory card (un save escribe varios sectores durante varios frames). Tampoco de dónde sale la semántica BIOS del filesystem de la tarjeta (libmcrd llama a la BIOS) | Persistir solo al terminar la operación de libmcrd; nombrar la fuente de la semántica BIOS |
| M7 | medio | AD-13, FR-10 | Dos pestañas: `createSyncAccessHandle` es exclusivo y las Memory cards en IndexedDB quedan con "gana el último" | Una sola instancia con Web Locks y un mensaje en la Shell |
| M8 | medio | Deferred / AD-9 | La presentación diferida no está aislada: `plat_present(rgba…)` supone un frame en CPU, y el Renderer WebGL2 (M5) depende de dónde vive el contexto GL | Decidir ya OffscreenCanvas en el worker o abstraer `plat_present` para frames en GPU |
| M9 | medio | NFR-2, SM-3, FR-7 | Ningún nivel de test mide el rendimiento ni los underruns en el Hardware de referencia (el runner es un servidor) | Nivel L4 de rendimiento, con corridas en el hardware de referencia y evidencia |
| M10 | medio | FR-13, AD-9 | La corrección de perspectiva necesita profundidad/w por vértice; el canal lateral solo lleva posición subpíxel y no define la clave de asociación | Contrato del canal: x, y subpíxel + z/w, con clave por dirección de destino (como `dw2-pc/backend/pgxp.c`) |
| L1–L12 | bajo | varios | Ver §3 | — |

---

## 1. Rúbrica

### 1.1 ¿Fija los puntos reales de divergencia de las épicas e historias?

Sí para: capas y directorios, memoria (arena, `PS1_RAM`, ventana de OT), overlays, modelo de
hilos, interfaz GPU, audio SPU, Pack/Manifest, almacenamiento, niveles de test, clases de CI,
flags de build y convenciones. **Faltan** estos puntos donde dos historias pueden decidir distinto:

1. **Qué bytes de upstream entran al `.wasm` y qué hacer con los `*_bss_us.c`** (C1).
2. **La lista de archivos por unidad** (H1): la historia de CMake y la de overlays la van a derivar
   distinto si el spine dice "directorio".
3. **La base de tiempo con fracciones y discontinuidades** (H3): `libetc`, `libcd`, el SPU, el
   AudioWorklet y la Shell (pausa) tocan el mismo reloj.
4. **El ancla de las Escenas grabadas** (M1): el runner de `tools/port/scenes` y el script Lua del
   emulador tienen que contar frames igual.
5. **Las versiones de formato persistente** (M5): la Shell, el worker del Pack y `libmcrd`.
6. **Una sola instancia** (M7).

### 1.2 ¿Cada Rule es verificable y previene su divergencia?

| AD | ¿Verificable? | ¿Previene? | Nota |
|---|---|---|---|
| AD-1 | Sí (no hay `.s` linkeados, no existe `port/recomp/`) | Parcial | "solo `.c` de `src/`" no excluye `_psstart.c` ni los `*_bss_<otra versión>.c`; ver H1 |
| AD-2 | Sí (comentario de fuente por función) | Sí | La "fuente asm" de `libmcrd`/`libapi` termina en llamadas a la BIOS, que no está en el decomp (M6) |
| AD-3 | Sí (`_Static_assert`) | Sí | Contradice el PRD (M2). En i386 SysV, `long long`/`double` en structs se alinean a 4; hoy solo hay un `long long` local (`src/main/utils.c:270`). Agregar `-malign-double` o un assert (L8) |
| AD-4 | Parcial | Parcial | "solo en dos lugares" omite `include/dw/main.h` (`GENERAL_BUFFER`, `TEXTURE_BUFFER`, `BOSS_EFE_TMD_BUFFER`), las direcciones guardadas en enteros (`int32_t MAIN_D_801349E4 = EVL_MMD_BUFFER`, `src/evl/evl.c:199`) y ~312 inicializadores de puntero no nulos en los `*_bss_us.c` (C1, L12) |
| AD-5 | Sí (abort al arrancar) | Parcial | El chequeo de `__heap_base` no cubre el stack del hilo del juego (M3) |
| AD-6 | Sí (test L1) | **No para USA** | La pertenencia por directorio está mal (H1). La restauración repone la "basura" de `*_bss_us.c` compilada en el binario (C1) |
| AD-7 | Sí (flags) | Sí | No menciona la pausa (FR-15) |
| AD-8 | Parcial | **No** | Regla de CD numéricamente incorrecta; sin fracciones; sin pausa ni overrun (H3). "Root counters": DW1 no llama a funciones de root counter (de `libapi` solo usa `InitHeap3`/`malloc3`/`free3`), es ruido (L10) |
| AD-9 | Sí (interfaz `gpu.h`) | Parcial | Canal lateral sin contrato (M10); dependencia oculta con la presentación (M8) |
| AD-10 | Parcial | Parcial | 44.100 Hz "por VBlank" sin acumulador ni control de deriva (H3) |
| AD-11 | Sí | Sí | Cabecera del Pack sin prohibir campos no deterministas (timestamps), y FR-3 pide Packs idénticos byte a byte (L11). Sin versión de formato (M5) |
| AD-12 | Sí | Parcial | "Generar una tabla con SHA-256 idéntico" mete en el `.wasm` bytes idénticos al exe si se genera en build, y el check NFR-4 falla. Tiene que generarse en runtime (L1) |
| AD-13 | Parcial | Parcial | Momento de la copia (M6), multi-pestaña (M7) |
| AD-14 | Sí | Parcial | L0 mal clasificado (H2); sin nivel de rendimiento (M9); ancla de Escena (M1) |
| AD-15 | Sí | Sí | El SNR en ventanas de 5 s depende de la relación muestras/VBlank (H3) |
| AD-16 | Sí | Parcial | L0 y `make compare` en clase A (H2); el check NFR-4 falla por C1 |
| AD-17 | Sí | Parcial | Flag de versión incorrecto, generadores de upstream ausentes (M4); NFR-7 sin un job que compile dos veces y compare (L6) |

### 1.3 ¿Algo de *Deferred* deja divergir dos unidades?

- **Presentación en web** — **sí** (M8). La frase "ninguna otra pieza depende de la elección" es
  falsa para el Renderer WebGL2.
- **Calibración del timing de CD** — **sí**: "puede ajustar los números, pero no su forma". La
  forma actual (sectores enteros por VBlank) no representa 2,5 sectores/VBlank, así que la forma
  también tiene que cambiar (H3).
- **Service worker y versionado del deploy** — **sí**, a través del formato del Pack y de
  IndexedDB (M5). El formato del SW puede quedar diferido; la versión de los datos persistentes no.
- **Punteros en `CHDAT/*.MMD`** — riesgo menor de lo que parece: `loadMMD`
  (`src/main/model.c:3356`) usa offsets relativos de la cabecera (`mmdPtr + ((long*)mmdPtr)[0]`) y
  `GsMapModelingData` resuelve los offsets del TMD. Los TMD embebidos en `BTL_REL.BIN` tienen
  `flags = 0` (sin `FIXP`, `asm/us/btl/data/model_0.rodata.s`). Igual conviene validarlo en M2,
  no en M3, porque es barato (leer el cargador de animaciones).
- `SHOP_REL.BIN`, 16:9, paralelismo del Backend, upstream merge: no dejan divergir.

### 1.4 ¿Cada dimensión estructural está decidida, diferida o abierta?

| Dimensión | Estado | Comentario |
|---|---|---|
| Capas y layout | Decidida | — |
| Build y toolchain | Decidida | Con errores (M2, M4) |
| Memoria | Decidida | Hueco del stack (M3) |
| Concurrencia y tiempo | Decidida | Incompleta (H3) |
| Formatos persistentes | Decidida | **Sin versión** (M5) |
| Errores y logs | Decidida | — |
| Testing | Decidida | Sin rendimiento (M9) |
| CI | Decidida | Clasificación (H2) |
| Seguridad del deploy (COOP/COEP) | Decidida | **CSP / NFR-5 no** (L2) |
| Entornos (local, CI, staging privado, prod pública) | **Ausente** | CLAUDE.md exige aprobación para lo *público*, lo que implica un entorno privado previo; no está definido (L7) |
| Configuración de nginx (ubicación en el repo, MIME `application/wasm`, cache) | **Ausente** del Structural Seed | (L7) |
| Operación (dominio, TLS, monitoreo) | Diferida a M7 | Correcto. Con NFR-5 el monitoreo solo puede ser del lado del servidor; conviene dejarlo escrito |
| Instancias concurrentes | **Ausente** | (M7) |
| Idioma de la UI de la Shell | **Ausente** (tampoco está en el PRD) | Pregunta abierta, baja |

### 1.5 Contradicciones con docs/research o con el código (spot-checks)

Verificado **correcto**: 2741 funciones / 202 PsyQ (`decomp-status.md`, `psyq-usage.md`);
`reference/dw2-pc` @ `816ed0d`; DW2-PC usa base de imagen `0x01000000`
(`reference/dw2-pc/CMakeLists.txt:214-221`); existen el spike de secciones por overlay
(`tools/port/env_check/overlay_sections/`) y las tablas PsyQ en `asm/us/main/data/psyq/libgte.data.s`;
el scratchpad pasa por `getScratchAddr` (`include/dw/efe.h:24-28`, `src/fish/fish.c:31`);
`psxgpu.c`, `psxspu.c`, `xadec.c` y `mdec.c` existen en `reference/dw2-pc/backend/`.

Contradicciones encontradas:

1. **Pertenencia de archivos** (H1): `config/us/main.yaml:125-132` y `:156-162`/`:210`/`:242`
   linkean `../vs/vs_intro`, `../vs/vs_combat`, `../vs/vs_select` y `../shop/script_menu` en
   `main`; `config/us/vs.yaml:99-112` no los incluye. `src/vs/vs_demo.c` no aparece en ningún YAML
   de USA. `src/<ovl>/` contiene `*_bss_{eu,jp,jp_trial,jp_bombom,jp_rev1}.c` con los mismos
   símbolos que `*_bss_us.c`.
2. **Datos del disco en `src/`** (C1): `src/mov/mov_bss_us.c` (1,5 MB) inicializa
   `MOV_RUN_LEVEL_BUFFER_0[40960]` con palabras que son instrucciones MIPS residuales. En total hay
   ~170.000 palabras no nulas en los `*_bss_us.c` de USA (mov 118k, doo2 9,9k, std 9,5k, kar 8,5k,
   evl 7,9k, …). `docs/research/risks.md` §3 ya marcaba los `*_bss_us.c` y el spine no lo recogió.
3. **Velocidad de CD** (H3): 1× = 75 sectores/s y 2× = 150 sectores/s. A 59,94 Hz eso da 2,50
   sectores por VBlank; 5 por VBlank equivale a 4×.
4. **Flag de versión** (M4): `Makefile:26` → `VERSION_MACRO := VERSION_$(VERSION en mayúsculas)`;
   `include/dw/version.h` prueba `defined(VERSION_US)`. `-DVERSION=US` no define nada de eso.
5. **SDL3 nativo** (M2): `docs/port/setup.md` registra `sizeof(void*)=8` para el SDL3 nativo de la
   imagen; el Stack dice "Clang 18 + gcc-multilib (i686)" como si ya estuviera.
6. **Scratchpad 2 KB** (L4): `hardware-access.md` §2 mide 1 KB (`0x1F800000-0x1F8003FF`). El spine
   copia el `0x800` de `dw2-pc/psyq/ps1mem.h:19` sin decirlo.
7. **`asm/` no versionado** (H2): `.gitignore:2` excluye `asm/`; lo genera splat desde
   `disks/us/*` (`config/us/*.yaml: target_path`).

---

## 2. Reconciliación de insumos

### 2.1 Lista de ADR obligatorios del kickoff (Fase 4, ítem 3)

| ADR exigido | AD | Estado |
|---|---|---|
| Funciones no descompiladas (decomp vs `port/recomp/`) | AD-1 | Aterrizó |
| Capa PsyQ: DW2-PC vs PsyCross vs propia | AD-2 | Aterrizó. PsyCross se descarta solo por la licencia, sin comparar el fondo técnico (bajo, L13) |
| Modelo de memoria y traducción de direcciones | AD-4, AD-5 | Aterrizó con huecos (M3, L12) |
| Overlays en un único binario | AD-6 | **Aterrizó mal para USA** (H1) |
| Threading y loop principal web | AD-7, AD-8 | Aterrizó; falta la pausa (H3) |
| Renderer software → WebGL2, interfaz común | AD-9 | Aterrizó; canal lateral incompleto (M10) |
| Audio: SPU emulado + driver original vs alto nivel | AD-10 | Aterrizó; falta la deriva y las fracciones (H3) |
| Formato del Pack y Manifest | AD-11 | Aterrizó; falta la versión (M5) |
| Testing: `make compare`, unit tests, diff de frames, Playwright | AD-14, AD-15 | Aterrizó; L0 mal clasificado (H2) |
| CI en el servidor: jobs sin datos vs runner privado | AD-16 | Aterrizó; clasificación de L0 y `cc_mips.dll` (H2). La clase A "en cualquier runner" puede no ser "en mi servidor" como pide el kickoff; confirmar con el dueño |

Formato: los AD no tienen "contexto / alternativas / consecuencias". Las alternativas viven en el
addendum. Para el punto de control humano conviene enlazar el addendum desde cada AD (bajo, L13).

### 2.2 PRD — requisitos funcionales

| Req. | Estado | Detalle |
|---|---|---|
| FR-1 | Parcial | No se fija la lectura por partes ni el tope de 512 MB del worker del Pack (`File.slice` + lectura síncrona en el worker). Bajo (L11) |
| FR-2 | OK | — |
| FR-3 | Parcial | "Pack web == Pack nativo byte a byte": falta prohibir campos no deterministas en la cabecera (L11). Borrar el Pack: Shell, OK |
| FR-4 | OK | — |
| FR-5 | **Parcial** | Ancla de inputs y sincronización tras cargas (M1). `rand` bit a bit (addendum §Determinismo) solo implícito en AD-2; conviene nombrarlo |
| FR-6 | **En riesgo** | La sincronía A/V ≤ 1 frame depende del ritmo de CD/XA, y la regla de AD-8 da el doble de velocidad (H3) |
| FR-7 | **Parcial** | Sin underruns en 10 min: falta el control de deriva y un nivel de medición (H3, M9). Volumen general: OK |
| FR-8 | OK | Hot-plug y stick como D-pad son detalle de la Shell |
| FR-9 | OK | `PortSettings` |
| FR-10 | **Parcial** | Atomicidad por transacción sí; consistencia del snapshot no (M6); multi-pestaña (M7) |
| FR-11 | Parcial | Formato `.mcd` sí. Importar con el juego corriendo necesita un evento de "tarjeta cambiada" hacia `libmcrd` (bajo) |
| FR-12 | OK | — |
| FR-13 | **Parcial** | Falta z/w para la corrección de perspectiva (M10) |
| FR-14 | Diferido | El PRD (§10.2) le pedía la respuesta a la arquitectura; queda diferida a M5 con un mecanismo por Escena. Aceptable si el dueño lo acepta como pregunta abierta |
| FR-15 | **Parcial** | Detección de capacidades sí (L3a); **pausa manual y al ocultar la pestaña: ausente** (H3) |
| FR-16 | **Parcial** | SW diferido; versión de datos persistentes ausente (M5) |
| FR-17 | **Cambiado** | x64 → i686 (M2) |

### 2.3 PRD — requisitos no funcionales

| Req. | Estado | Detalle |
|---|---|---|
| NFR-1 | Parcial | Deadline sin deriva OK; faltan pausa y overrun (H3) |
| NFR-2 | **Ausente** como mecanismo | Ningún AD ni nivel de test lo mide (M9); no hay fila en el Capability Map |
| NFR-3 | OK | Check de tamaño en clase A. El C1 suma datos al `.wasm` |
| NFR-4 | **Contradicho** | C1 |
| NFR-5 | Parcial | Sin CSP (`connect-src 'self'`) ni assertion en L3a de "sin pedidos a otros orígenes" (L2) |
| NFR-6 | Parcial | Comparar el preprocesado contra el **merge-base** con `upstream/main`, no contra la punta; usar `-P` para que los cambios de líneas no rompan la igualdad (L3). `make compare` solo en clase B (H2) |
| NFR-7 | Parcial | Flags OK; falta un job que compile dos veces y compare. La base `ubuntu:noble` no está fijada por digest (`setup.md`) (L6) |
| NFR-8 | OK | AD-15 |
| NFR-9 | Parcial | Playwright fija un build de navegador; "las dos últimas versiones estables" necesita una matriz definida (L6) |
| NFR-10 | OK | — |

### 2.4 CLAUDE.md

| Regla / ítem | Estado |
|---|---|
| R1: nada con copyright en el repo; hook | OK para el repo. Para el **deploy**, C1. El Manifest guarda rutas, LBA y modo además de "solo hashes y tamaños": desvío de la letra de la regla, a confirmar por el dueño (L5) |
| R2: `DW1_PORT`, matching, `non-matching.md` | OK (AD-1, convención). `make compare` en clase B (H2) |
| R3: código en `port/` y `tools/port/`, cambios mínimos | `CMakePresets.json` en la raíz y workflows en `.github/workflows/` (donde ya está el `build.yaml` de upstream, que solo corre en `jype0/dw_decomp`) quedan fuera de esas rutas. Registrarlos en `src-changes.md` o justificarlos (L9) |
| R4: estilo upstream + m2c + objdiff | OK (AD-1) |
| R5: evidencia automática | OK (convención de evidencia, AD-14), salvo rendimiento (M9) |
| R6: idiomas | OK |
| R7: git | Fuera del alcance de la arquitectura; OK |
| R8: no inventar | Bien en general (marcas `[ASSUMPTION]`), pero la cifra de CD es un error aritmético, no un supuesto (H3) |
| Layout: `native (Linux x64)` | Cambiado (M2) |
| Hito M3: diff "contra DuckStation" | El spine elige PCSX-Redux con OpenBIOS. OpenBIOS implementa la BIOS (incluido el filesystem de la Memory card) distinto de la de Sony: la referencia de `libmcrd` y del arranque no es la consola real. Documentarlo en AD-14/AD-15 (M6) |
| Riesgo: diferencias de compilador (padding) | i386 alinea los tipos de 8 bytes a 4 (L8) |
| Riesgo: overlays reinicializan datos | AD-6 OK en mecanismo; mal en pertenencia (H1) y en contenido inicial (C1) |

### 2.5 Supuestos del memlog que no llegaron al spine

- "(assumption) CD timing… determinism checkpoints chosen after loads complete": **no aterrizó**
  (M1).
- "(decision) Web host does not depend on SDL3 for video/audio/input": solo implícito en el Stack
  ("SDL3 solo native"). Escribirlo en AD-7 o AD-9, porque CLAUDE.md describe `host/` como "capa
  SDL3".

---

## 3. Detalle y fixes

### C1 (crítico) — Datos del disco compilados desde `src/`

**Hecho.** Upstream (MIT) versiona en C los datos del juego: tablas y strings de `main`
(`file_table_us.c`, `model.c`, …), texto generado desde `text/*.yaml`, y los `*_bss_us.c` de cada
overlay. Estos últimos son el contenido residual que quedó en el `.bss` dentro de cada
`*_REL.BIN`: ~170.000 palabras no nulas, en su mayoría instrucciones MIPS y punteros PS1. Al
compilar el juego para el port, todo eso entra al `.wasm` byte a byte (little-endian, igual que en
el disco). El check NFR-4 de AD-16 ("ningún artefacto del deploy contiene ventanas de 64 bytes de
los archivos del disco") **va a fallar en la primera corrida**. Además:

- el PRD §6 permite servir "el C de upstream (MIT)", así que el PRD se contradice a sí mismo si el
  check no tiene alcance;
- AD-4 ("nunca un puntero PS1 crudo en memoria") lo violan ~312 inicializadores de puntero no
  nulos en los `*_bss_us.c`;
- AD-6 restaura esa basura en cada carga (como la consola, pero con punteros que en native no
  apuntan a nada válido);
- suma cientos de KB a NFR-3.

**Fix (AD nuevo, con decisión del dueño):**
1. *Alcance del check NFR-4:* la referencia es "bytes del disco **menos** los que reproduce el
   código fuente versionado". Se calcula en clase B como una máscara de las ventanas que también
   aparecen en un build del port hecho solo desde fuentes versionadas, o se excluyen los rangos de
   símbolos definidos en C según `config/us/symbols*.txt`. El dueño confirma que el C de upstream
   es aceptable en el deploy.
2. *`*_bss_us.c` en el port:* no se compilan sus inicializadores. CMake genera, con
   `tools/port/`, una versión con las mismas declaraciones en cero (o se envuelven en
   `DW1_PORT`). Si el juego lee algo antes de escribirlo, se copia en `port_overlay_loaded` desde
   la imagen del `REL.BIN` ya cargada en `PS1_RAM`, con una tabla generada (símbolo → dirección
   PS1, tamaño), **solo para campos que no son punteros**. Un test L2 compara las variables de
   estado contra el emulador.
3. Agregar los `*_bss_us.c` y `text/` como fila explícita en AD-12.

### H1 (alto) — Pertenencia de archivos a unidades

**Fix:** reescribir AD-6 y AD-1: "la lista de TUs de cada unidad y su asignación a `main` o a un
overlay salen de `mk/version/us.mk` (y se verifican contra `config/us/<unidad>.yaml`), con un
generador en `tools/port/`. Solo las TUs de un `*_REL.BIN` de USA van en la sección
`dw1ovl_<ovl>`. `vs_intro`, `vs_combat`, `vs_select` y `shop/script_menu` son `main`.
`_psstart.c` se excluye." Un test L1 verifica que la lista generada coincide con los YAML.

### H2 (alto) — L0 necesita datos

**Fix:** L0a = preprocesado sin `DW1_PORT` contra el merge-base (clase A, solo necesita `text/` y
`gen_text.py`). L0b = `make generate && make TOOLCHAIN=gcc` y, con DLL, `make compare` (clase B:
`asm/` sale de `disks/`, y `cc_mips.dll` es CodeWarrior y no puede ir a un runner público).
Actualizar la tabla de AD-14 y el diagrama de CI.

### H3 (alto) — Base de tiempo, CD y audio

**Fix en AD-8 y AD-10:**
- Modelo de CD con acumulador racional: 150 sectores/s a 2× (75 a 1×, según el modo que pida el
  juego) sobre 60000/1001 VBlanks/s, es decir 2,5025 sectores/VBlank. Las lecturas en streaming
  (XA/STR, `CdlModeRT`) siguen el mismo ritmo.
- SPU: acumulador de muestras (44.100 × 1001/60000 = 735,735 por VBlank), sin redondeo
  acumulativo.
- Host: remuestreo adaptativo con nivel objetivo del ring, para absorber la deriva entre el reloj
  de pared y el del `AudioContext`. Se fijan el tamaño del ring y la latencia.
- Pausa (FR-15) y pestaña oculta: se congela el contador de VBlank y se rebasa el deadline al
  reanudar.
- Frames tarde: política explícita (por ejemplo, "si el retraso supera N VBlanks, se rebasa sin
  catch-up"), y cada VBlank perdido se cuenta para NFR-2.
- **Hipótesis a verificar (regla 8):** la PS1 NTSC en modo progresivo corre a ≈59,826 Hz, no a
  59,94. Si es así, el emulador de referencia produce ≈737,1 muestras por VBlank y el PCM de L2 se
  desalinea ~0,19 % en 5 s. AD-15 tiene que fijar si la comparación de audio se hace con el
  emulador forzado a 59,94 o normalizando por VBlank.

### M1 — Determinismo de Escenas grabadas

Anclar la secuencia de inputs al primer `VSync` después de `main()`, o a un contador de frames
del juego leído por símbolo, nunca al encendido. Los puntos de control van después de cada carga
de CD completa. El runner de L2 reinyecta "carga completa" según el emulador, o se acepta que FR-5
solo es verificable entre checkpoints.

### M2 — i686 frente a x64

O el dueño aprueba el cambio y se corrigen PRD §3/FR-17 y CLAUDE.md ("binario i686 que corre en
Linux x64"), o se vuelve al modelo de DW2-PC (64 bits con fixes). Además, una historia M2 para
SDL3 i386 y sus dependencias `:i386`. `native-headless` no la necesita.

### M3 — Stack del hilo del juego

`pthread_create` con `pthread_attr_setstack` sobre un `static char game_stack[N]` dentro de la
ventana, en web y en native. Extender el abort de arranque para que también lo verifique.

### M4 — Flags y generadores

`-DVERSION_US` (no `-DVERSION=US`). CMake ejecuta `tools/gen_text.py us` sobre `text/**.yaml`
hacia un include dir generado, y toma la lista de fuentes de `mk/version/us.mk` (ver H1).

### M5 — Versionado de datos persistentes

Campo `format_version` en la cabecera del Pack, `schema_version` en IndexedDB y en
`PortSettings`. Regla: un deploy nuevo lee el formato N-1 o migra el Pack desde el propio Pack, sin
pedir el disco. Si no puede, lo dice en la Shell antes de borrar nada.

### M6 — Memory cards

Persistir la copia solo cuando `libmcrd` termina una operación (escritura completa y
`MemCardSync` sin trabajo pendiente) o tras N VBlanks sin escrituras. Nombrar la fuente de la
semántica BIOS del filesystem de la tarjeta: directorio, bloques, `firstfile`/`nextfile`. Puede
ser la documentación pública y, si se verifica la licencia, OpenBIOS. Un test L1 hace round-trip
de un `.mcd` creado por DuckStation.

### M7 — Una sola instancia

`navigator.locks.request('dw1-port', {ifAvailable: true})` en la Shell; la segunda pestaña
muestra un mensaje y no abre ni OPFS ni IndexedDB para escribir.

### M8 — Presentación

Decidir ahora que el Renderer WebGL2 vive en el hilo del juego sobre un OffscreenCanvas
transferido. El spike de M4 decide solo el camino del Renderer software. O bien `plat_present`
acepta también "frame ya presentado en GPU".

### M9 — Rendimiento

Agregar L4: corrida instrumentada de 10 minutos (VBlanks perdidos, underruns, tiempo por frame)
en el Hardware de referencia (máquina del dueño o equivalente), en Chrome y Firefox. Sale un log
como evidencia, sin datos del juego. Fila NFR-2 en el Capability Map.

### M10 — Canal lateral del GTE

Contrato: por vértice, `(x, y)` subpíxel y `z`/`w`, indexados por la dirección de destino del
store (`gte_stsxy*`) dentro del paquete. Es el modelo de PGXP, ya presente en
`reference/dw2-pc/backend/pgxp.c`. Se invalida al reescribir la palabra.

### Bajos

- **L1** AD-12: la generación por código solo puede ser en runtime, nunca un array en el binario.
- **L2** NFR-5: `Content-Security-Policy: default-src 'self'; connect-src 'self'` en nginx, y una
  assertion en L3a de que no hay pedidos a otros orígenes.
- **L3** NFR-6: preprocesar con `-P` y comparar contra `git merge-base HEAD upstream/main`. Si
  algún archivo usa `__LINE__`, considerar el desplazamiento de líneas.
- **L4** Scratchpad: el hardware tiene 1 KB; si se usan 2 KB por margen (DW2-PC), decirlo.
- **L5** Manifest con rutas, LBA y modo: confirmar con el dueño que cumple la regla 1.
- **L6** NFR-7: job "build doble + `cmp`"; fijar `ubuntu:noble` por digest. NFR-9: matriz de
  versiones de navegador.
- **L7** Entornos: local / CI / staging privado en el servidor (sin acceso público) / producción
  (aprobación del dueño). Agregar `tools/port/deploy/` (nginx.conf, MIME, cache del SW) al
  Structural Seed.
- **L8** i686: `-malign-double` o `_Static_assert(_Alignof(long long) == 8)` para conservar el
  layout de MIPS/wasm32.
- **L9** Archivos de la raíz (`CMakePresets.json`, workflows): justificarlos bajo la regla 3.
- **L10** AD-8: quitar "root counters" o justificarlo con un uso real.
- **L11** FR-1: lectura por partes y tope de memoria en el worker del Pack. FR-3: cabecera del Pack
  determinista.
- **L12** AD-4 (a): incluir `include/dw/*.h` y las direcciones guardadas en enteros
  (`src/evl/evl.c:199`, `loadMMDAsync(..., int32_t buffer, ...)`). En 32 bits funcionan si el
  macro devuelve `PS1_RAM()`, pero hay que listarlas.
- **L13** AD-2: una línea sobre el fondo técnico de PsyCross. Los AD deberían enlazar las
  alternativas del addendum para la aprobación.
