---
review: adversarial
target: ../ARCHITECTURE-SPINE.md
date: '2026-10-11'
lens: 'dos unidades que cumplen cada AD al pie de la letra y aun así no encajan; solidez técnica de AD-4/AD-5/AD-6 contra el código real'
baseline: upstream/main @ 09c174c (árbol local)
---

# Revisión adversarial — Architecture Spine DW1 Web Port

## Veredicto

La forma del spine es buena: capas, Pack único, oráculo software y la decisión de no tener
`port/recomp/` son coherentes. Pero **AD-8 tiene un modelo de tiempo que no puede ejecutar el
código real**: las encuestas no avanzan el tiempo, el root counter de 240 Hz se dispara una vez
por VBlank, DrawSync espera un VBlank y la tasa de CD está mal por un factor 2. Además **AD-6
elige las fuentes por directorio y eso no coincide con el layout USA**, y **AD-1/AD-6 chocan con
NFR-4**: el `.wasm` contiene sí o sí bytes del disco. Hay cinco huecos que bloquean. Hay que
cerrarlos antes de partir el trabajo en épicas, porque cada uno deja que dos historias bien hechas
produzcan un port que se cuelga, suena a 1/4 de velocidad o tiene el CI rojo para siempre.

Severidad: **Crítica** = el port no funciona o el CI no puede pasar; **Alta** = divergencia
segura o integración rota entre historias; **Media** = divergencia probable o ambigüedad que
genera retrabajo; **Baja** = error de dato o redacción.

---

## Pares incompatibles (huecos)

### H1 — Las encuestas no avanzan el tiempo: cuelgue en `CdReadSync(1)` y en el spin de MOV [Crítica]

**Unidad A (libcd/libetc del port), fiel a AD-8:** el VBlank solo se cruza dentro de `VSync` (o
en una espera bloqueante); CD y GPU terminan "en límites de VBlank"; en headless "el deadline se
ignora".
**Unidad B (juego, sin cambios):**

- `src/main/efe.c:1023` — `while (CdReadSync(1, 0) != 0) {}`
- `src/main/efe_table.c:31` — `while (CdReadSync(1, 0));`
- `src/mov/mov.c:159-170` — `MOV_waitForFlip` hace spin sobre `env->flipPending`, que pone el
  callback de `DecDCTout`, sin llamar a ninguna función PsyQ dentro del loop. Clang puede además
  sacar la carga del loop, porque el campo no es `volatile`.

Con A, `CdReadSync(1)` nunca ve completarse la lectura, porque ningún VBlank cruza mientras el
juego encuesta. En headless se cuelga para siempre. En tiempo real depende de si alguien mira el
reloj de pared, y AD-8 lo prohíbe para los modelos de dispositivo. MOV termina siempre por la rama
de timeout, que fuerza el flip: es otra ruta de estado distinta de la de la consola. Además AD-8 no
nombra al MDEC, así que un dev de MDEC que llama al callback de forma síncrona dentro de
`DecDCTout` y otro que lo difiere al VBlank producen comportamientos distintos, y los dos cumplen.

**Fix (reescribir AD-8):** tiempo virtual con un único dueño, `port/psyq/clock.c`:

- Un contador de ciclos virtuales y una cola de eventos ordenada (VBlank, root counters, fin de
  CD/MDEC/GPU/memcard).
- **Toda entrada a una función PsyQ** (bloqueante o de encuesta: `CdReadSync(1)`, `CdSync(1)`,
  `DrawSync(1)`, `VSync(-1)`, `StGetNext`, `TestEvent`, `MemCardSync(1)`) avanza el reloj una
  cantidad fija de ciclos y despacha los eventos vencidos.
- Las esperas bloqueantes saltan directo al próximo evento.
- El reloj de pared solo pausa al hilo en `VSync`, nunca decide el orden.
- Los spins sin llamadas PsyQ sobre flags que escribe un ISR se inventarían con un script de
  `tools/port/metrics`. Cada uno recibe un `DW1_PORT` con `port_yield()` (un bloque por spin en
  `src-changes.md`) o se lista como no aplicable.
- Test L1: `CdReadSync(1)` en loop termina en headless.

### H2 — El root counter de libsnd a 240 Hz se dispara una vez por VBlank [Crítica]

**Unidad A (libetc del port), fiel a AD-8:** "al cruzar [el deadline] ejecuta los callbacks
instalados (`VSyncCallback`, root counters)".
**Unidad B (libsnd traducido del asm de DW1, AD-10):** `src/main/sound.c:524` llama
`SsSetTickMode(SS_TICK240)`, así que libsnd espera que `SsSeqCalledTbyT` corra 240 veces por
segundo desde RCnt2.

Si se combinan las dos, la música va a 1/4 de tempo, o salen 4 ticks en ráfaga por VBlank si el
dev de libetc "compensa" por su cuenta. A eso se suma AD-10: el SPU "avanza por VBlank", así que
todos los key-on de un VBlank caen en la misma muestra. El inicio de las notas queda cuantizado a
16,7 ms frente a los 4,17 ms del original. Un SNR de 40 dB tras correlación cruzada (AD-15b) es
casi imposible con transitorios corridos hasta 12 ms.

**Fix:**

- AD-8: los root counters son eventos propios de la cola de tiempo virtual (H1), con su período
  programado (`SetRCnt`), y no se pegan al VBlank.
- AD-10: "antes de cada escritura a un registro SPU y al cerrar cada VBlank, el SPU se ejecuta
  hasta el tiempo virtual actual (precisión de muestra)". En lugar de "avanza por VBlank".
- La traza de registros de AD-15a se compara con timestamp en muestras, no solo agrupada por
  VBlank.

### H3 — DrawSync "en límite de VBlank" y tasa de CD equivocada [Crítica]

AD-8 dice que "las operaciones de CD y GPU terminan en límites de VBlank". El juego llama
`DrawSync(0)` muchas veces dentro de un mismo frame, después de cada `LoadImage`/`ClearImage`:
`src/main/font.c:1512,1533,1699`, `effect_common.c:359,404,417,453`, `inventory.c:315,844`,
`overworld.c:387,390`. Si cada una espera al próximo VBlank, un frame con 4 o más DrawSync se pasa
del presupuesto de `VSync(3)` (`main.c:3958`, juego a 20 fps). Resultado: el juego corre más lento
que la consola y el determinismo por frames (AD-15) se rompe.

Además, "CD a 2× = 5 sectores por VBlank" está mal: 2× = 150 sectores/s, es decir
150 × 1001/60000 ≈ **2,5 sectores por VBlank**. Cinco sectores por VBlank es 4×. El
`[ASSUMPTION]` cubre la calibración del seek, no un error de base. Con 5, los loads async (BTL
desde `btl.c:379`, TRN desde `map.c:5460`, ENDI) terminan antes de tiempo y el orden de eventos
difiere del Emulador de referencia.

**Fix (AD-8):**

- GPU: `DrawSync` termina con un costo en ciclos proporcional a lo que se transfiere o dibuja
  (constante inicial, calibrable en M3) dentro de la cola de tiempo virtual, nunca redondeado a
  VBlank.
- CD: 150 sectores/s en `CdlModeSpeed` y 75 sin él, con un acumulador fraccional (sin redondear
  por VBlank), más un seek fijo calibrable.
- `VSync(n)` con la semántica PsyQ: espera hasta que pasen n VBlanks desde el **retorno del
  `VSync` anterior**, no "hasta el próximo deadline". `VSync(-1)` devuelve el contador, que usa
  `fish.c:2975,3067,3073`. `VSync(1)` devuelve las líneas horizontales.

### H4 — AD-6 elige las fuentes "por directorio" y el layout USA no es así [Alta]

AD-6: "el build compila cada `.c` de `src/<ovl>/` a través de una TU envoltorio … `dw1ovl_<ovl>`".
En USA:

- `vs_intro.c`, `vs_combat.c` y `vs_select.c` (en `src/vs/`) y `shop/script_menu.c` están
  **linkeados en `main`** (`config/us/main.yaml:125-132, 156-162, 204-210, 236-242`). Si siguen
  la regla, sus `.data`/`.sdata` se reinician en cada carga de VS o SHOP, algo que en la consola
  nunca pasa.
- `src/vs/vs_demo.c` no aparece en `config/us/vs.yaml` ni en `main.yaml`.
- Cada directorio tiene `*_bss_{eu,jp,jp_rev1,jp_trial,jp_bombom}.c` además del `_us.c`. Un
  glob los linkea juntos, lo que da definiciones duplicadas, o elige uno a mano.
- `src/main/file_table_us.c` define `ATAN_LOOKUP_TABLE` desde un header (`dw/atan_lookup_table.h`).
  Solo funciona si exactamente un `file_table_*.c` entra al build.

Dos devs que siguen AD-6 al pie de la letra (uno con glob y otro con una lista a mano) producen
binarios distintos, y ninguno coincide con la consola.

**Fix (AD-6 + AD-17):** la lista de TUs y la pertenencia a `dw1ovl_<ovl>` se **generan** de
`config/us/*.yaml` (subsegmentos `c`). El segmento dueño manda, no el directorio. Un test L0 o L1
comprueba que cada `.c` del build aparece en exactamente un segmento de `config/us/` y que no
queda ningún `.c` USA sin compilar. Los headers de texto generados (`text/<ovl>/*.yaml` →
`text/<ovl>/*.h`, que usa por ejemplo `src/kar/kar.c`) salen del mismo generador de upstream,
invocado desde CMake. Hay que nombrarlo en AD-17.

### H5 — AD-1/AD-6 meten bytes del disco en el `.wasm` y el check de NFR-4 no puede pasar [Crítica]

AD-1 compila el 100 % del C de `src/`, AD-6 compila los `*_bss_us.c` dentro de las secciones de
overlay, y AD-12 dice que "no hay arrays con datos del disco o del exe en el repo". Pero `src/` ya
**es** esos arrays:

- `src/mov/mov_bss_us.c` contiene ~469 KB (de `0x800109EC` a `0x80083428`) copiados de
  `MOV_REL.BIN`: `MOV_RUN_LEVEL_BUFFER_*`, `MOV_RING_BUFFER`, `MOV_VLC_TABLE`, todos con el
  contenido residual del archivo.
- Los `*_bss_us.c` del resto de los overlays suman unas 48.800 líneas de datos de ese tipo.
- A eso se suman `file_table_us.c`, las tablas de `model.c` y los textos generados.

El check de clase B (AD-16, NFR-4: "ningún artefacto del deploy contiene ventanas de 64 bytes
presentes en los archivos de la Imagen") va a encontrar cientos de miles de coincidencias en el
`.wasm`. La historia del build web y la del check de leaks cumplen sus ADs y el CI queda rojo para
siempre. Si alguien "arregla" el check con una whitelist, el check deja de medir algo.

**Fix:** decisión humana (legal) registrada como AD nuevo, porque cambia NFR-4.

- (a) **Opción recomendada:** el `.data` de overlays y main que viene de `src/` se inicializa al
  arrancar desde el Pack. Los `*_bss_us.c` se compilan bajo `DW1_PORT` como `.bss` (sin
  inicializador), y `port/psyq/overlay.c` llena la copia inicial de `dw1ovl_<ovl>` leyendo los
  bytes del REL en el Pack, a partir de una tabla generada símbolo→offset (lo que AD-12 ya hace
  para PsyQ). Cuidado: esto solo vale para variables sin punteros, así que la tabla generada tiene
  que marcar los campos puntero y relocarlos.
- (b) La otra opción es aceptar que el deploy incluye lo que upstream ya publica bajo MIT y
  redefinir NFR-4 como "ventanas que no estén en el preprocesado de `src/`". Eso lo tiene que
  aprobar el dueño.

En cualquiera de los dos casos, AD-12 y AD-16 tienen que decir explícitamente qué bytes de `src/`
pueden llegar al deploy.

### H6 — La ventana de OT de AD-5 no cubre el stack del hilo del juego [Alta]

AD-5 (web): "datos estáticos, stack y arena antes de `__heap_base`". AD-7: `-sPROXY_TO_PTHREAD`.
Con PROXY_TO_PTHREAD, `main()` corre en un pthread cuyo stack lo reserva el runtime con `malloc`,
así que queda **arriba** de `__heap_base` y no en el stack estático. En native es igual: el stack
de un pthread sale de `mmap`, fuera de la base de 16 MB. ASan (`native-asan`) mueve además las
variables locales a un *fake stack* en el heap. El check de arranque de AD-5 solo mira
`__heap_base` y el fin de la arena, así que no detecta nada de esto. Cualquier primitiva o OT
local (en el juego o en el port, por ejemplo un `DR_MODE` temporal en libgs) enlazada con
`addPrim`/`AddPrim` produce un enlace de 24 bits que `PS1_LINK_PTR` reconstruye a otra dirección.

Hay una inconsistencia más: AD-5 define la vuelta (`PS1_LINK_PTR`) pero no la ida.

**Fix (AD-5):**

- El hilo del juego se crea con `pthread_attr_setstack` sobre un buffer estático dentro de la
  ventana, en ambos builds. En web eso implica no usar PROXY_TO_PTHREAD tal cual, o crear el hilo
  propio desde `main`.
- `PS1_LINK(ptr)` es la única forma de construir un enlace, la usan `addPrim`/`setaddr` de los
  headers del port, y lleva un `PORT_ASSERT(in_window(ptr))`.
- En `native-asan`: `detect_stack_use_after_return=0`.

### H7 — Frame 0, instante de captura y punteros en el estado: el port y la referencia miden cosas distintas [Alta]

AD-14/AD-15 definen la Escena por "secuencia de inputs desde el arranque" y "frames a capturar",
y comparan "el área de display de la VRAM". No definen:

- **Origen del índice de frame.** PCSX-Redux con OpenBIOS pasa N VBlanks en el BIOS y en
  `_psstart` antes de `main`, mientras que el port empieza en `main`. El runner del port y el
  script Lua de la referencia (dos historias distintas) van a contar distinto, y todos los inputs
  y capturas quedan corridos.
- **Instante de captura.** Puede ser antes o después de los callbacks de VBlank, y antes o
  después del `PutDispEnv` de ese frame. El área de display la define el registro GP1 *display
  start* en ese instante.
- **Variables de estado con punteros** (AD-15, `state_vars.txt`). En el port valen punteros
  nativos y en la referencia `0x80xxxxxx`, así que nunca son iguales.

**Fix (AD-15):**

- Frame 0 = retorno del primer `VSync` llamado desde `main` del juego. Los dos lados lo detectan
  por símbolo (breakpoint en la referencia).
- Captura = área de display según GP1 después de despachar los eventos del VBlank k y antes de
  volver al juego.
- Los inputs se aplican en el latch del VBlank k.
- `state_vars.txt` tipifica cada variable. Los punteros se comparan traducidos: arena →
  `0x80000000 | offset`, global nativo → símbolo + offset.

### H8 — El momento del reinicio de overlay en loads async no está definido [Media]

AD-6 llama a `port_overlay_loaded(lib)` "después" del `readFile` o `addFileReadRequestPath` dentro
de `loadDynamicLibrary`. En async (BTL `btl.c:379`, TRN/TRN2 `map.c:5460,5466`, DOO2
`dooa.c:365`, ENDI `script_interp.c:1850`, SHOP `script_common.c:3084`), los bytes llegan al arena
varios frames después, desde `tickFileReadQueue` (`src/main/file_queue.c`, código del juego), que
pone `*isRunningPtr` y llama `finishCallback`.

Un dev hace el restore en el momento del pedido (lectura literal de AD-6). Otro lo engancha a la
finalización de la cola, porque es cuando la consola "pisa" los datos. Son dos mecanismos
posibles, justo lo que AD-6 dice que previene. Además el flag tiene forma de "pendiente" (≠0 → 0:
`while (DOO2_LOADING_COMPLETE != 0)`, `while (loadState > 0)`), mientras que
`docs/research/overlays.md` dice "`*isComplete` pasa a 1 al terminar". Un dev que lea la
investigación lo implementa al revés.

**Fix (AD-6):** el restore ocurre en un único punto. Opciones: en el pedido, siempre (válido
porque el código del overlay no corre hasta que el juego ve el flag), o al completar. Hay que
elegir una y documentarla. Además, corregir `overlays.md` con la semántica real del flag (lo
escribe el `file_queue.c` del juego, el port no lo toca).

### H9 — El aliasing entre buffers absolutos y variables de overlay desaparece en silencio [Media]

En la consola, las variables de un overlay y los buffers absolutos del juego **comparten bytes**:

- `TEXTURE_BUFFER`/`ARRAY_SECTION_START = 0x80088800` (`include/dw/main.h:9`,
  `script_common.c:251`, bump allocator `allocateArray`) es exactamente la base de TRN/TRN2.
- `BOSS_EFE_TMD_BUFFER = 0x80058000` cae dentro del rango de BTL/STD/VS.
- `GENERAL_BUFFER = 0x80010000` es donde empieza MOV.

En el port, el arena recibe esos bytes, pero el `.data`/`.bss` nativo del overlay está en otra
parte. Si el juego depende del aliasing en algún punto (un buffer que pisa el `.bss` de un overlay
residente, o al revés), la consola y el port divergen sin ningún error. AD-4 además solo cuenta
"los 28 casts de `src/`" y omite los macros de `include/dw/main.h` (`GENERAL_BUFFER`,
`TEXTURE_BUFFER`, `BOSS_EFE_TMD_BUFFER`) e `include/dw/vs.h:14-18`. Un dev que traduce solo lo
listado deja punteros PS1 crudos.

**Fix:**

- AD-4: el inventario de traducciones abarca `src/` **e** `include/`, y se genera con un script
  (`grep` de `0x80[0-9a-f]{6}` casteado) que falla en L0 si aparece uno nuevo sin traducir.
- AD-6: en debug, mientras el overlay X está residente, toda escritura al arena dentro del rango
  PS1 `.data`/`.bss` de X (sale de `config/us/X.yaml`) se registra como "aliasing" con un hook de
  `PS1_RAM` en modo checked o con mprotect en native. Un test L2 exige cero eventos por Escena, o
  bien una lista justificada.

### H10 — Reloj maestro de audio vs reloj de VBlank: nadie es dueño de la deriva [Media]

AD-8 fija el VBlank al reloj de pared (`k × 1001/60000 s`), AD-10 genera 44.100 Hz (735,7
muestras por VBlank, fraccional) y el AudioWorklet consume al reloj del dispositivo. Los dos
relojes derivan algunos ppm. En una sesión larga, el ring SAB (AD-7) se vacía o se llena. El dev
del Host agrega control de tasa dinámico en el worklet (un remuestreo variable, que AD-10 deja en
el Host) y el dev de libetc ajusta el deadline al nivel del ring. Cumplen ADs distintos y se pelean
en un lazo de control doble.

**Fix (AD-10):**

- El dueño único de la deriva es el Host: remuestreo con ratio ajustado según el nivel del ring
  (banda muerta definida).
- El juego nunca ajusta el VBlank por audio.
- El SPU lleva un acumulador fraccional exacto (44100 × 1001 / 60000 muestras por VBlank) dentro
  de `clock.c`.

---

## Solidez técnica de AD-4 / AD-5 / AD-6 contra el código

| Punto | Resultado | Evidencia |
|---|---|---|
| Heap de `malloc3` dentro del arena | **Correcto en la forma.** `initializeHeap` usa el *valor* de `_end`, `_stack_addr` y `_stack_size` (`include/__rts_info_t__.h:33-39`, `main.c:3537-3539`), y `InitHeap3` recibe un entero casteado. **Riesgo (hipótesis):** `_end` es un símbolo que define el script de link por defecto de GNU ld en i386 (`_end = .;`). Definirlo en C puede chocar o quedar sombreado por el linker, y el heap se calcularía con basura. Fix: el `__rts_info_t__.h` del port (que va primero en el include path, AD-2) hace `#define _end dw1_ps1_end`, o se fija `-fuse-ld=lld` y se verifica en L1. | `src/main/main.c:3534` |
| Overlays sin cross-refs de main por dirección | **Correcto.** `config/us/symbols.txt` no tiene símbolos en `0x80010000-0x80090000`. main llama a los overlays por símbolo. | — |
| Blobs embebidos (TMD/TIM) referenciados por C | **Correcto.** Solo se usan vía macros numéricas (`BTL_/STD_/VS_*_MODEL`, `*_FINISHER_TIM`). Ningún C hace `extern` a símbolos que solo existen en `asm/us/<ovl>/data/{texture,model_*,header}.rodata.s`. El header de cada REL tiene un self-pointer PS1 (`BTL_D_80056C68`) que nadie lee. | `asm/us/btl/data/header.rodata.s` |
| `#pragma clang attribute … variable(is_global)` | **Funciona.** Cubre statics de archivo y statics locales (`hasGlobalStorage`). Ningún overlay tiene globals `const`, así que no hay "section type conflict". Lo **no cubierto**: literales de string y compound literals (van a `.rodata`). Un juego que escribe sobre un literal crashea en native y en wasm conserva el valor entre recargas. Agregar un test L1 o UBSan. | grep `const` en `src/<ovl>/` |
| Leer el REL entero al arena | **Correcto y necesario** (blobs TMD/TIM en su dirección, `GsMapModelingData` escribe punteros nativos en el arena). MOV pisa `0x80010000-0x80083428` en el arena exactamente como en la consola. `GENERAL_BUFFER` es temporal (los offsets `VHB_SOUNDBUFFER_START` son de SPU RAM, no de RAM). Lo que no se reproduce es el aliasing (H9). | `src/main/sound.c:45` |
| Scratchpad 2 KB | **Dato erróneo.** El scratchpad PS1 es 1 KB (`0x1F800000-0x1F8003FF`, `hardware-access.md` §2). Con 2 KB, un acceso fuera de rango que en la consola daría bus error pasa inadvertido. Fix: 1 KB + `PORT_ASSERT` en debug. | AD-4 |
| `PS1_RAM(addr & 0x1FFFFF)` | **Riesgo de enmascarar errores.** Hay valores basura en los `_bss` (`(Entity *)0x80820000`, `endi_bss_us.c:145`) y `NULL` se convierte en la base del arena. Fix: `PS1_RAM` hace `PORT_ASSERT((addr & 0xFFE00000) == 0x80000000)` en debug. No se traducen los casts que viven en `*_bss_us.c` (son residuos del REL, no direcciones). | — |
| Native 16 MB | **Incompleto.** Falta `-no-pie` (Ubuntu genera PIE por defecto) y la base concreta (por ejemplo `-Wl,-Ttext-segment=0x01000000`). | AD-5 |

---

## Otros puntos (Baja)

- **AD-13 / AD-7:** `createSyncAccessHandle()` es asíncrona y exclusiva. Hay que abrirla antes de
  que el hilo del juego entre en `main` (un pthread bloqueado en `Atomics.wait` no resuelve
  promesas), y el worker del Pack tiene que cerrar su handle antes. Falta la regla de orden.
- **AD-8, memory card:** las operaciones async de `libmcrd` (`MemCardSync(1)`, eventos) no tienen
  modelo de finalización. Hay que incluirlas en la cola de H1.
- **AD-11:** `MOV_startRead` usa `CdlModeSize1` (2340 bytes por sector). El Pack F2 guarda 2336
  (subheader + datos) y le faltan los 4 bytes de cabecera MSF. Se pueden reconstruir desde la LBA,
  pero hay que decirlo para que el dev de `cdrom` no lo resuelva distinto que el de `pack`.
- **AD-4:** el inventario "28 casts" contradice la regla "en la memoria nunca se guarda un puntero
  PS1 crudo": los `*_bss_us.c` guardan valores como `(GsOT_TAG *)0x80040000`
  (`dooa_bss_us.c:1015`). Aclarar que la regla aplica al código del port y que los residuos del
  juego no se tocan.

## Resumen de cambios propuestos

| AD | Cambio |
|---|---|
| AD-8 | Reescribir como tiempo virtual con cola de eventos única (`clock.c`); toda llamada PsyQ avanza el tiempo; root counters propios; GPU por costo, no por VBlank; CD 150 sectores/s; semántica PsyQ de `VSync(n)`; MDEC y memcard incluidos; inventario de spins sin llamadas. |
| AD-10 | SPU con precisión de muestra hasta el tiempo virtual antes de cada escritura; dueño de la deriva = Host. |
| AD-6 | Fuentes y secciones generadas de `config/us/*.yaml`; punto único del restore en async; detector de aliasing en debug. |
| AD-5 | Stack del hilo del juego dentro de la ventana; `PS1_LINK()` con assert; `-no-pie` + base fija. |
| AD-4 | Inventario que incluye `include/`, generado y verificado en L0; scratchpad de 1 KB; asserts en `PS1_RAM`; resolver el choque de `_end`. |
| AD nuevo (NFR-4) | Decisión del dueño sobre los bytes de `src/` en el deploy (cargar `.data` del Pack, o redefinir el check). |
| AD-15 | Frame 0, instante de captura, latch de input y tipado de punteros en `state_vars.txt`. |
