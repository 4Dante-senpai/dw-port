# Epic 4 Context: Jugar en el navegador con el disco propio (M4)

<!-- Compilado a partir de los artefactos de planificación. Edición libre. Regenerar con compile-epic-context si cambian los documentos de planificación. -->

## Goal

Permitir que el jugador abra la Shell web, seleccione su disco USA, lo valide y empaquete localmente, y juegue con teclado o gamepad, audio y partidas persistentes, también sin conexión. El navegador ejecuta el mismo C del juego compilado a WebAssembly sobre la plataforma propia, sin CPU MIPS emulada ni instalación adicional.

## Stories

- Story 4.1: El Juego en wasm con su propio hilo y presentación
- Story 4.2: Worker de E/S y pantalla de título en el navegador
- Story 4.3: Selección del disco, validación y Pack en el navegador
- Story 4.4: Pantallas de la Shell web y capacidades
- Story 4.5: Teclado y gamepad
- Story 4.6: Remapeo y opciones persistentes
- Story 4.7: Audio en el navegador
- Story 4.8: Memory cards en el navegador
- Story 4.9: Offline y actualizaciones
- Story 4.10: Checks del build web: ritmo, tamaño, reproducibilidad, red y datos

## Requirements & Constraints

- Soportar Chrome y Firefox de escritorio, dos últimas versiones estables. Validar la Shell con Playwright 1.64.0 en Chromium y Firefox.
- Aceptar BIN de pista única MODE2/2352, con o sin CUE, mediante selector o arrastre. Rechazar ISO de 2048 bytes/sector, modos incompatibles y versiones JP/PAL, indicando el problema y la versión requerida SLUS-01032. Validar cada archivo por SHA-256; ante cualquier fallo, nombrar el archivo y eliminar el Pack parcial.
- Leer por partes, con memoria de validación ≤ 512 MB. El Pack web debe ser idéntico byte a byte al nativo y sobrevivir a recargas. Medir el tiempo hasta estar listo; objetivo inicial: ≤ 60 s en el equipo de referencia, pendiente de validar.
- Mantener 59,94 Hz, período 1001/60000 s: desviación del conteo de VBlanks < 0,1 % en diez minutos. Presupuestos iniciales: WASM Brotli ≤ 4 MB y primera descarga sin Pack ≤ 6 MB. Dos builds del mismo commit en la imagen del proyecto deben producir WASM idéntico.
- Disco, Pack y tarjetas permanecen en el dispositivo; las solicitudes de red se limitan a archivos del programa en su origen. Solo se permiten bytes del disco ya reproducidos por el C upstream de `src/` e `include/`. Verificar repo y artefactos mediante ventanas de 64 bytes, sin contenido del disco en objetos de `port/`.
- Exigir evidencia automática: pruebas sintéticas sin datos privados y pruebas con disco en el runner privado, publicando solo hashes, métricas y diferencias numéricas. Toda modificación upstream queda protegida por `DW1_PORT`, documentada y compatible con el build matching.

## Technical Decisions

- Mantener Juego → PsyQ → Backend/Host; solo el Host conoce el navegador. Compilar con Emscripten 6.0.10, pthreads, `-sSUPPORT_LONGJMP=wasm` y `-fwasm-exceptions`, sin ASYNCIFY/JSPI ni LTO en objetos del juego. Conservar las mismas opciones semánticas que en nativo, incluido `char` con signo. Resolver las llamadas indirectas con firmas incompatibles antes de ejecutar WASM.
- Un hilo ejecuta Juego, PsyQ, Backend y callbacks. Su stack estático y todo objeto enlazable en OT deben caber en `[0, 16 MB)`; memoria inicial fija, sin crecimiento. Verificar `pthread_attr_setstack`; si no se respeta, aplicar `-sPROXY_TO_PTHREAD` y registrar la decisión.
- Presentar el rasterizador software a 1× como textura WebGL2 sobre `OffscreenCanvas`, con contexto en el hilo del juego y salida única `plat_present`. Medir el coste de presentación como insumo de rendimiento.
- Un worker posee OPFS y sus handles síncronos, abiertos antes del arranque. Reutiliza el constructor C del Pack nativo y sirve sectores mediante buzón SAB y `Atomics.wait`. El hilo del juego nunca accede directamente a OPFS. Versionar el Pack con `format_version` y migrarlo sin pedir nuevamente el disco.
- El reloj virtual determina eventos; el reloj de pared solo regula el ritmo. Muestrear teclado/gamepad en el hilo principal y aplicar su estado compartido en el latch de cada VBlank. Ofrecer 14 botones digitales, remapeo y stick izquierdo opcional como D-pad.
- AudioWorklet consume un ring SAB. El Host controla remuestreo, deriva y volumen persistente; no modifica el VBlank por audio. El PCM previo al remuestreo debe coincidir con el nativo; comprobar cero underruns durante diez minutos en ambos navegadores.
- IndexedDB almacena `PortSettings` y tarjetas con `schema_version`. Cada ranura contiene 131.072 bytes, escritos atómicamente con `durability: 'strict'`: una interrupción conserva la tarjeta anterior o la nueva. Enviar copia al hilo principal al terminar escrituras y en `pagehide`. Tomar el Web Lock `dw1-game` para impedir dos instancias.

## UX & Interaction Patterns

Mostrar bienvenida y aviso de copia propia → selección → progreso → «Listo para jugar» → «Jugar». Ese único gesto debe llevar al título con audio. En visitas posteriores, ofrecer jugar directamente. Incluir opciones, dos ranuras de Memory cards y borrado de datos.

Impedir el arranque con mensajes específicos si faltan aislamiento cross-origin, SAB u OPFS, o existe otra instancia. Solicitar almacenamiento persistente; si se deniega, informar y recomendar exportar las tarjetas. Importar `.mcd`/`.mcr`, exportar `.mcd` compatibles con DuckStation/PCSX-Redux y rechazar tamaños incorrectos. Permitir restaurar controles predeterminados.

Pausar manualmente y al ocultar la pestaña, sin avance de VBlanks ni penalización por frames perdidos al regresar. Un service worker con caché versionada permite recargar offline; las actualizaciones se aplican al recargar, conservando Pack y tarjetas.

## Cross-Story Dependencies

El arranque y la presentación preceden al acceso al Pack y al título web. Reutilizar de la épica 2 el reloj, renderer, Pack y título determinista: primero comparar el título web con nativo; la comparación contra PCSX-Redux depende de la épica 3. Audio y saves web requieren sus implementaciones nativas de esa épica.

Persistencia y migraciones sostienen remapeo, volumen, tarjetas y actualizaciones. Servir con COOP/COEP desde desarrollo para habilitar SAB. Las mejoras gráficas y la optimización final pertenecen a la épica 5; la publicación al servidor, a la 7.
