---
title: "PRD: DW1 Web Port"
status: final
approved: 2026-10-11 (dueño; supuestos aceptados tal como están)
created: 2026-10-11
updated: 2026-10-11
---

# PRD: DW1 Web Port

## 0. Propósito del documento

Este PRD define qué tiene que hacer DW1 Web Port para considerarse terminado. Lo leen el dueño
del proyecto (aprobación), el workflow de arquitectura (`bmad-architecture`) y la creación de
épicas e historias. Se apoya en el brief (`docs/planning-artifacts/briefs/brief-dw-port-2026-10-11/brief.md`)
y en la investigación medida (`docs/research/`); no la repite. Los términos del §3 se usan tal
cual en todo el documento. Las funcionalidades agrupan requisitos funcionales con IDs globales
(FR-n); los requisitos no funcionales transversales están en §5 (NFR-n). Las decisiones
técnicas (cómo) van en `addendum.md`. Las inferencias sin confirmar llevan `[ASSUMPTION]` y se
indexan en §11.

## 1. Visión

DW1 Web Port es *Digimon World* (PlayStation, USA) corriendo en el navegador como programa
nativo: el código C del juego descompilado por `dw_decomp`, compilado a WebAssembly, sobre una
reimplementación propia de las librerías PsyQ y del hardware de la consola. El jugador entra a
una URL, elige la imagen de su disco una única vez y juega la partida completa, con la lógica
original, a 59,94 Hz, con sonido, videos, controles a su gusto y saves que no se pierden.

La tesis: con el 100 % del código del juego ya en C (medido) y una arquitectura probada en
DW2-PC, el problema deja de ser "descompilar" y pasa a ser "reemplazar la plataforma": PsyQ, GPU,
SPU, CD y la plataforma web. Si eso se hace con fidelidad verificable (frames idénticos al
emulador a 1×), se obtiene un port que se juega como el original y se ve mejor cuando el usuario
lo pide.

## 2. Usuario objetivo

### 2.1 Jobs to be done

- Jugar *Digimon World* con mi copia del disco en una computadora, sin instalar ni configurar un
  emulador.
- Retomar mi partida días después en el mismo navegador sin volver a elegir el disco.
- Llevar mi save de un emulador al port, y del port a un emulador.
- Jugar con el control que tengo (teclado o gamepad) con los botones donde los quiero.
- Ver el juego más nítido y sin polígonos que tiemblan, si quiero; o idéntico al original.

### 2.2 No usuarios (v1)

- Quien no tiene una copia del juego: el port no la provee ni la descarga.
- Quien busca jugar en celular o tablet [ASSUMPTION: no es objetivo de v1; no se bloquea si
  funciona].
- Quien quiere modificar el juego (mods, traducciones, datos alternativos).

### 2.3 Recorridos clave

- **UJ-1. Lucía juega por primera vez con su disco.**
  - **Persona + contexto:** Lucía, 32 años, jugó DW1 de chica; tiene el `.bin/.cue` de su copia en
    la notebook. No quiere instalar nada.
  - **Entrada:** abre la URL del port en Chrome; nunca la usó.
  - **Camino:** la página explica que hace falta su propia imagen del disco → elige el `.cue` y el
    `.bin` → ve el progreso de la validación y del armado del pack → aparece "Listo para jugar".
  - **Clímax:** presiona "Jugar", suena el audio de inicio y llega a la pantalla de título.
  - **Resolución:** el pack queda guardado en el navegador; la próxima vez no se le pide el disco.
  - **Caso de error:** si elige una imagen PAL o modificada, la página dice qué archivo no coincide
    y no guarda nada.

- **UJ-2. Martín sigue su partida con el gamepad.**
  - **Persona + contexto:** Martín ya armó el pack la semana pasada en Firefox; juega con un
    control de Xbox conectado por USB.
  - **Entrada:** abre la URL; el pack ya está.
  - **Camino:** "Jugar" → carga su memory card → elige "Continue" en el título → juega una hora →
    guarda en el juego → cierra la pestaña.
  - **Clímax:** al volver al día siguiente su save sigue ahí, con el progreso de ayer.
  - **Caso de error:** si el navegador avisa que el almacenamiento no es persistente, la página
    le sugiere exportar su memory card.

- **UJ-3. Sofía trae su save de DuckStation.**
  - **Persona + contexto:** Sofía jugó 20 horas en DuckStation y quiere seguir en el navegador.
  - **Camino:** abre "Memory cards" → "Importar" → elige su `.mcd` → la tarjeta aparece en la
    ranura 1 → juega → más tarde "Exportar" y la carga de nuevo en DuckStation.
  - **Clímax:** el emulador lee el save exportado con el progreso hecho en el port.

## 3. Glosario

- **Imagen del disco** — el `.bin` (sectores crudos de 2352 bytes, una pista `MODE2/2352`) y
  opcionalmente su `.cue`, provistos por el usuario. Única fuente de datos del juego.
- **Manifest** — lista versionada con un SHA-256 y el tamaño de cada archivo de la Imagen del
  disco USA esperada. No contiene datos del juego.
- **Pack** — archivo derivado de la Imagen del disco, construido en el dispositivo del usuario,
  con los archivos del disco en el formato que lee el port. Vive solo en el almacenamiento del
  navegador (o en disco en el build nativo).
- **Shell web** — la página HTML/JS que aloja al Juego: selector de Imagen del disco, progreso,
  opciones, Memory cards, y el canvas.
- **Juego** — el código C del juego (de `dw_decomp`) compilado para la plataforma, más la Capa
  PsyQ y el Backend.
- **Capa PsyQ** — reimplementación propia de las funciones de las librerías PsyQ que el Juego
  llama (202 funciones, ver `docs/research/psyq-usage.md`).
- **Backend** — implementación propia del GPU, SPU, decodificador XA y MDEC de la PS1.
- **Renderer** — la parte del Backend que convierte la VRAM y las primitivas en la imagen que se
  presenta. Hay dos: **Renderer software** y **Renderer WebGL2**.
- **Escena** — un estado reproducible del Juego (por ejemplo "pantalla de título", "primer
  combate", "Ciudad Archivo de día") identificado por un nombre estable y un punto de entrada
  (save o secuencia de inputs).
- **Frame de referencia** — captura de una Escena tomada en el Emulador de referencia.
- **Emulador de referencia** — DuckStation o PCSX-Redux, usado solo en desarrollo para
  comparar.
- **Memory card** — imagen cruda de 128 KB (131.072 bytes) de una tarjeta PS1, formato `.mcd` de
  DuckStation/PCSX-Redux (equivalente a `.mcr` de ePSXe).
- **Build nativo** — el Juego compilado para Linux x64 con SDL3, para depurar.
- **Build web** — el Juego compilado con Emscripten a WebAssembly, dentro de la Shell web.
- **Hardware de referencia** — el equipo contra el que se miden NFR-2 y SM-3 (definido en NFR-2).
- **Build matching** — el build PS1 de upstream con CodeWarrior que debe dar el binario original
  byte a byte (`make compare`).

## 4. Funcionalidades

### 4.1 Imagen del disco, validación y Pack

**Descripción:** la primera vez, la Shell web pide la Imagen del disco, la lee localmente, la
valida contra el Manifest y construye el Pack en el almacenamiento del navegador. Las veces
siguientes usa el Pack guardado. Realiza UJ-1.

#### FR-1: Selección de la Imagen del disco

El usuario puede elegir la Imagen del disco con un selector de archivos o arrastrándola a la
página, sea solo el `.bin` o el `.bin` más el `.cue`. Realiza UJ-1.

**Consecuencias (verificables):**
- Con un `.bin` de pista única `MODE2/2352` la Shell web acepta la selección; con un `.cue` cuyo
  `TRACK 01` no es `MODE2/2352` la rechaza con un mensaje que nombra el problema.
- Un `.iso` de 2048 bytes por sector se rechaza explicando que le faltan los sectores XA/video.
- La lectura se hace por partes; la pestaña no supera 512 MB de memoria durante la validación
  [ASSUMPTION: límite razonable para hardware de referencia; medir].

#### FR-2: Validación contra el Manifest

El sistema verifica cada archivo de la Imagen del disco contra el Manifest antes de construir el
Pack. Realiza UJ-1.

**Consecuencias:**
- Con la Imagen del disco USA correcta la validación pasa; con una imagen con un solo byte
  cambiado en cualquier archivo, falla y la Shell web muestra el nombre del archivo que no
  coincide.
- Una imagen JP o PAL se rechaza indicando que solo se soporta la versión USA (SLUS-01032).
- Si la validación falla no queda ningún Pack parcial guardado.

#### FR-3: Construcción y persistencia del Pack

El sistema construye el Pack localmente y lo guarda de forma que el usuario no tenga que elegir
la Imagen del disco otra vez. Realiza UJ-1, UJ-2.

**Consecuencias:**
- Al recargar la página después de construir el Pack, la Shell web ofrece "Jugar" sin pedir la
  Imagen del disco.
- El Pack contiene exactamente los archivos del Manifest y cada uno coincide con su SHA-256.
- El Pack que genera el Build web es idéntico byte a byte al que genera la herramienta nativa
  `tools/port` a partir de la misma Imagen del disco.
- La Shell web pide almacenamiento persistente (`navigator.storage.persist()`) e informa si el
  navegador lo deniega.
- El usuario puede borrar el Pack desde la Shell web.

**Fuera de alcance:** construir el Pack en el servidor o subir cualquier parte de la Imagen del
disco.

### 4.2 Juego completo

**Descripción:** el Juego corre completo con su lógica original: arranque, videos de intro,
pantalla de título, nueva partida y continuar, mundo, combates, menús, entrenamiento, minijuegos
(pesca, curling), evolución, torneos, modo VS contra la CPU y el final. Realiza UJ-1, UJ-2.

#### FR-4: Arranque hasta la pantalla de título

El Juego arranca, reproduce la secuencia inicial y llega a la pantalla de título.

**Consecuencias:**
- La Escena "pantalla de título" a 1× con el Renderer software coincide píxel a píxel con su
  Frame de referencia.
- Llegar a la pantalla de título no requiere ninguna acción del usuario salvo "Jugar" (gesto para
  habilitar audio).

#### FR-5: Partida de principio a fin

El usuario puede jugar una partida nueva hasta los créditos finales.

**Consecuencias:**
- Existe una suite de Escenas que cubre todos los overlays del Juego (`BTL`, `STD`, `FISH`,
  `EVL`, `KAR`, `VS`, `MOV`, `DOO2`, `DOOA`, `TRN`, `SHOP`, `DGET`, `TRN2`, `MURD`, `ENDI`, `EAB`) y
  cada una pasa su test de regresión.
- Una partida completa grabada (secuencia de inputs) se reproduce en el Build nativo de principio
  a fin sin divergencia de estado respecto del Emulador de referencia en los puntos de control
  definidos [ASSUMPTION: la reproducción determinista con inputs grabados es viable; depende de
  que `rand` y el timing se repliquen exactamente].

#### FR-6: Videos

El Juego reproduce los videos del disco (STR/MDEC) con su sonido.

**Consecuencias:**
- Los frames de video decodificados coinciden con los del Emulador de referencia (tolerancia
  definida en la arquitectura por diferencias de redondeo del IDCT).
- Audio y video del video no se desfasan más de 1 frame de video al final de cada video.
- Start salta el video, como en la consola.

#### FR-7: Audio

El Juego reproduce música, efectos y audio XA (voces y pistas de CD) del disco.

**Consecuencias:**
- Las Escenas de prueba de audio producen una señal que coincide con la del Emulador de
  referencia dentro de una tolerancia definida en la arquitectura.
- Sin cortes audibles (buffer underrun) durante 10 minutos de juego en hardware de referencia.
- El volumen general es ajustable en la Shell web.

### 4.3 Controles

**Descripción:** el usuario juega con teclado o gamepad, con un mapeo por defecto razonable y
la opción de cambiarlo. Realiza UJ-2.

#### FR-8: Teclado y gamepad

El usuario puede controlar el Juego con teclado o con cualquier gamepad que el navegador exponga
por Gamepad API.

**Consecuencias:**
- Los 14 botones digitales del pad PS1 (D-pad, ×, ○, □, △, L1, L2, R1, R2, Start, Select) tienen
  mapeo por defecto en teclado y en gamepad estándar.
- Conectar o desconectar un gamepad durante el juego no detiene el Juego.
- El stick izquierdo puede actuar como D-pad (opción).

#### FR-9: Remapeo

El usuario puede reasignar cada botón del pad PS1 a una tecla o botón de gamepad, y el mapeo
persiste.

**Consecuencias:**
- Tras reasignar un botón y recargar la página, el mapeo nuevo sigue vigente.
- Existe "restaurar valores por defecto".

### 4.4 Partidas guardadas y Memory cards

**Descripción:** los saves que hace el Juego se guardan en Memory cards persistentes; el usuario
puede importarlas y exportarlas. Realiza UJ-2, UJ-3.

#### FR-10: Saves persistentes

Lo que el Juego graba en la Memory card persiste entre sesiones del navegador.

**Consecuencias:**
- Guardar en el Juego, cerrar la pestaña, reabrir y "Continue" carga ese save.
- Una escritura de Memory card interrumpida (cerrar la pestaña en medio) no deja la Memory card
  ilegible: o queda el estado anterior o el nuevo.

#### FR-11: Importar y exportar Memory cards

El usuario puede importar un `.mcd`/`.mcr` de 131.072 bytes a una ranura y exportar la Memory card
de una ranura como `.mcd`. Realiza UJ-3.

**Consecuencias:**
- Una Memory card exportada del port carga en DuckStation y PCSX-Redux y muestra el save.
- Una Memory card creada en DuckStation e importada en el port carga en el Juego.
- Un archivo de tamaño distinto de 131.072 bytes se rechaza con un mensaje.
- Hay dos ranuras de Memory card [ASSUMPTION: DW1 usa la ranura 1; la ranura 2 se ofrece por
  compatibilidad].

### 4.5 Opciones de video

**Descripción:** el usuario puede ver el Juego como el original o con mejoras. Por defecto, el
Juego se ve como el original escalado sin filtro [ASSUMPTION: el default es "original"; las
mejoras son opt-in].

#### FR-12: Escala de resolución

El usuario puede elegir la resolución interna del 3D (1× a N×) [ASSUMPTION: N = 4 en v1; más si
el Renderer WebGL2 lo sostiene a 59,94 Hz].

**Consecuencias:**
- A 1× sin otras mejoras, los frames coinciden con los Frames de referencia.
- A 2× o más, la geometría 3D se dibuja a esa resolución; texturas y 2D conservan sus píxeles.

#### FR-13: Sin wobble

El usuario puede activar vértices precisos y texturas con corrección de perspectiva.

**Consecuencias:**
- Con la opción activa, una Escena con cámara quieta no muestra vértices que salten entre frames
  (medible como desplazamiento máximo de vértice < 1 píxel de salida entre frames idénticos).

#### FR-14: Relación de aspecto

El usuario puede elegir 4:3 o, si es viable, 16:9 ampliando el campo de visión del 3D sin
estirar la interfaz.

**Consecuencias:**
- En 16:9 la interfaz 2D (menús, HUD, textos) mantiene su layout 4:3.
- Si 16:9 no es viable en alguna Escena (por ejemplo, fondos pre-renderizados), esa Escena se
  muestra en 4:3 con barras [NOTE FOR PM: viabilidad a confirmar en arquitectura].

### 4.6 Shell web y modo offline

#### FR-15: Pantallas de la Shell web

La Shell web ofrece: bienvenida con explicación legal ("necesitás tu propia copia"), selección de
Imagen del disco, progreso, "Jugar", opciones (video, audio, controles), Memory cards, y borrar
datos.

**Consecuencias:**
- Si el navegador no tiene las capacidades necesarias (aislamiento cross-origin, almacenamiento)
  la Shell web lo dice con un mensaje específico en vez de fallar en silencio.
- El Juego se puede pausar desde la Shell web y se pausa solo al ocultar la pestaña.

#### FR-16: Offline tras la primera carga

Después de una primera carga completa, la Shell web y el Juego funcionan sin conexión.

**Consecuencias:**
- Con la red desconectada, recargar la página abre la Shell web y el Juego arranca con el Pack
  guardado.
- Una versión nueva del deploy se detecta y se aplica al recargar, sin perder Pack ni Memory
  cards.

### 4.7 Build nativo para desarrollo

#### FR-17: Build nativo

El desarrollador puede compilar y ejecutar el Juego en Linux x64 con el mismo código que el Build
web, con y sin ventana.

**Consecuencias:**
- `cmake --preset native && cmake --build` (o equivalente documentado) produce un ejecutable que
  llega a la pantalla de título [ASSUMPTION: presets de CMake; el nombre exacto lo fija la
  arquitectura].
- Modo headless que corre N frames y vuelca capturas de Escenas para los tests.

## 5. Requisitos no funcionales transversales

- **NFR-1 Ritmo:** el Juego avanza a 59,94 Hz (VBlank NTSC, 1001/60000 s) sin deriva acumulada:
  tras 10 minutos, el conteo de VBlanks difiere del teórico en menos de 1 en el Build nativo y en
  menos de 0,1 % en el Build web.
- **NFR-2 Rendimiento:** en Chrome y Firefox de escritorio actuales en el hardware de referencia,
  con Renderer software a 1×, el 99 % de los frames de 10 minutos de juego se producen a tiempo
  (sin VBlanks perdidos) [ASSUMPTION: hardware de referencia = CPU de 4 núcleos de ~2018 (clase
  Intel Core i5-8250U), GPU integrada, 8 GB de RAM].
- **NFR-3 Tamaño de descarga:** el `.wasm` del Juego comprimido con Brotli pesa como máximo 4 MB
  y la descarga total de la primera carga (sin Pack) como máximo 6 MB [ASSUMPTION: presupuesto
  inicial; ajustar tras medir en M4].
- **NFR-4 Legal:** ni el repositorio ni el deploy contienen datos del juego. Verificado por: hook
  pre-commit, y un check de CI en un runner privado que confirma que ningún artefacto del deploy
  contiene ventanas de 64 bytes presentes en los archivos de la Imagen del disco.
- **NFR-5 Privacidad:** la Imagen del disco, el Pack y las Memory cards nunca salen del
  dispositivo. El deploy no hace pedidos de red salvo los de sus propios archivos.
- **NFR-6 Build matching:** cuando `cc_mips.dll` está disponible, `make compare` da OK en cada
  merge a `main`. Sin el compilador, todo cambio en `src/` está detrás de `DW1_PORT` y el
  preprocesado de cada archivo de `src/` sin `DW1_PORT` es idéntico al de `upstream/main`.
- **NFR-7 Build reproducible:** dos builds del mismo commit en la imagen Docker del proyecto
  producen el mismo `.wasm` byte a byte.
- **NFR-8 Fidelidad:** con Renderer software a 1× y sin mejoras, cada Escena de la suite coincide
  píxel a píxel con su Frame de referencia, salvo excepciones documentadas por Escena.
- **NFR-9 Navegadores:** Chrome y Firefox de escritorio, dos últimas versiones estables. Safari y
  móviles no se prueban en v1.
- **NFR-10 Divergencia con upstream:** cada cambio en `src/` está listado en
  `docs/port/src-changes.md`; un merge de `upstream/main` no requiere resolver conflictos fuera de
  esos puntos.

## 6. Restricciones

- **Legales:** sin imagen, archivos extraídos, ejecutable original, ni bytes de ellos embebidos
  en el binario; sin CodeWarrior. El servidor solo sirve código propio y el C de upstream (MIT).
- **Técnicas:** no se interpreta ni se recompila MIPS en tiempo de ejecución (sin CPU emulada).
  El Juego corre como C compilado.
- **Deploy:** servidor propio con COOP/COEP; publicación pública solo con aprobación explícita
  del dueño del proyecto.

## 7. No objetivos

- No es un emulador ni ofrece cargar otros juegos.
- No hay multijugador (ni VS entre dos personas, local u online).
- No hay mods, parches de traducción ni datos alternativos.
- No se soportan las versiones JP, JP trial, JP rev1, JP BomBom ni EU/PAL en v1, aunque upstream
  las descompile.
- No hay cuentas, nube ni sincronización de saves.
- No se rebalancea ni cambia la lógica del juego.

## 8. Alcance de la v1

### 8.1 Dentro

- FR-1 a FR-17 y NFR-1 a NFR-10.
- Versión USA (SLUS-01032).
- Chrome y Firefox de escritorio.

### 8.2 Fuera de la v1

- Renderer WebGL2 con escala > 4× (v2, si el rendimiento lo permite).
- Filtros de imagen (sharpening, CRT) — v2.
- Speed-up / avance rápido — v2 (decidido en la aprobación del 2026-10-11).
- Soporte móvil (controles táctiles) — v2.

## 9. Métricas de éxito

**Primarias**
- **SM-1:** pantalla de título alcanzada en el Build nativo (M2) y en el Build web (M4), con
  Frame de referencia idéntico. Valida FR-4, NFR-8.
- **SM-2:** porcentaje de Escenas de la suite que pasan su test de regresión en el Build web.
  Meta v1: 100 %. Valida FR-5.
- **SM-3:** porcentaje de frames a tiempo en 10 minutos de juego en hardware de referencia
  (Chrome y Firefox). Meta: ≥ 99 %. Valida NFR-1, NFR-2.

**Secundarias**
- **SM-4:** tiempo desde elegir la Imagen del disco hasta "Listo para jugar". Meta: ≤ 60 s en
  hardware de referencia [ASSUMPTION]. Valida FR-2, FR-3.
- **SM-5:** Memory cards exportadas que cargan en DuckStation: 100 % de los casos de prueba.
  Valida FR-11.

**Contra-métricas (no optimizar)**
- **SM-C1:** cantidad de cambios en `src/`. No se reduce a costa de meter hacks en la Capa PsyQ
  que reproduzcan bugs del original de forma frágil; tampoco se infla. Contrapesa NFR-10.
- **SM-C2:** tamaño del `.wasm`. No se reduce sacrificando fidelidad (por ejemplo, simplificando
  el SPU). Contrapesa NFR-3.

## 10. Preguntas abiertas

1. ~~¿Entra el speed-up en v1?~~ Resuelta: no, queda para v2 (§8.2).
2. ¿Es viable 16:9 en todas las Escenas? Lo responde la arquitectura (FR-14).
3. ¿Qué Emulador de referencia se usa para los Frames de referencia, y con qué BIOS (OpenBIOS de
   PCSX-Redux o BIOS del usuario fuera del repo)?
4. ¿JSPI está estable en Firefox? Si sí, puede evitar COOP/COEP (ver
   `docs/research/web-constraints.md`).
5. ¿El contenido de `SHOP_REL.BIN` se usa en USA? (hipótesis en `docs/research/overlays.md`).

## 11. Índice de supuestos

Aprobados por el dueño el 2026-10-11 sin cambios. Siguen marcados porque se validan con
mediciones (no son hechos verificados).

- §2.2 — Móviles y tablets no son objetivo de v1.
- §4.1 FR-1 — Límite de 512 MB de memoria durante la validación.
- §4.2 FR-5 — La reproducción determinista con inputs grabados es viable.
- §4.4 FR-11 — DW1 usa la ranura 1; se ofrecen dos ranuras.
- §4.5 — El default de video es "original"; mejoras opt-in.
- §4.5 FR-12 — Escala máxima 4× en v1.
- §4.7 FR-17 — Presets de CMake para el Build nativo.
- §5 NFR-2 — Hardware de referencia: CPU 4 núcleos ~2018 (i5-8250U), GPU integrada, 8 GB.
- §5 NFR-3 — Presupuesto de descarga: `.wasm` ≤ 4 MB Brotli, primera carga ≤ 6 MB.
- §9 SM-4 — ≤ 60 s para validar y construir el Pack.
