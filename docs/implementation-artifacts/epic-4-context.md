# Epic 4 Context: Jugar en el navegador con el disco propio (M4)

<!-- Compilado a partir de los artefactos de planificación. Puede editarse libremente. Regenerar con compile-epic-context si cambia la planificación. -->

## Goal

Permitir que el jugador abra la Shell web, seleccione su disco USA, lo valide y empaquete localmente y juegue con teclado o gamepad, audio y partidas persistentes, incluso sin conexión. El mismo C del juego se compila a WebAssembly: los datos proceden del dispositivo del usuario y permanecen allí.

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

- Aceptar `.bin` de pista única `MODE2/2352`, con `.cue` opcional, por selector o arrastre. Rechazar otros modos, ISO de 2048 bytes/sector y versiones JP/PAL, indicando que se requiere USA SLUS-01032. Validar cada archivo por SHA-256; identificar el archivo incorrecto y no conservar Packs parciales.
- Leer por partes con memoria de pestaña ≤ 512 MB. El Pack web debe ser idéntico al nativo y reutilizarse al recargar. Medir el tiempo hasta «Listo para jugar»; la meta de ≤ 60 s en hardware de referencia sigue siendo un supuesto.
- Ofrecer 14 botones digitales, remapeo persistente, restauración de valores y stick izquierdo como D-pad opcional. Conectar o desconectar gamepads no debe interrumpir el juego.
- Mantener dos tarjetas crudas de 131.072 bytes; importar `.mcd`/`.mcr` y exportar `.mcd` compatibles con DuckStation y PCSX-Redux. Una escritura interrumpida conserva el estado anterior o el nuevo.
- En diez minutos, la desviación del conteo de VBlanks web debe ser < 0,1 % a 60000/1001 Hz. Verificar PCM idéntico al nativo antes del remuestreo y cero underruns del worklet en diez minutos en ambos navegadores.
- `.wasm` con Brotli ≤ 4 MB; primera descarga sin Pack ≤ 6 MB. Dos builds del mismo commit en la imagen fijada deben producir `.wasm` idénticos.
- Disco, Pack y tarjetas permanecen locales; la red solo solicita archivos del programa. Prohibir datos extraídos o incrustados; únicamente se admiten los bytes que reproduce el C upstream de `src/` e `include/`. Verificarlo con ventanas de 64 bytes y un caso de contaminación que haga fallar el control.
- Cubrir Chrome y Firefox de escritorio, sus dos últimas versiones estables. Exigir evidencia automática con Playwright en Chromium y Firefox: pruebas sintéticas sin datos y pruebas privadas con disco. No cerrar historias con fallos o implementación parcial.

## Technical Decisions

- Emscripten con `-pthread -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`, memoria inicial fija y sin crecimiento; sin LTO para objetos del juego. Un hilo ejecuta juego, PsyQ, Backend y callbacks. Stack estático y objetos enlazables por OT dentro de `[0, 16 MB)`. Si no se respeta el stack, registrar el fallback `-sPROXY_TO_PTHREAD`. Resolver firmas incompatibles de llamadas indirectas wasm. Sin ASYNCIFY ni JSPI.
- El hilo principal gestiona Shell, input, IndexedDB y Web Lock `dw1-game`. Un worker es dueño exclusivo de OPFS; abre el Pack antes del arranque y sirve lecturas por buzón SAB con `Atomics.wait`, sin espera activa. Compartir el C del constructor del Pack entre nativo y web.
- Presentar el renderer software como textura WebGL2 en un `OffscreenCanvas` del hilo del juego mediante `plat_present`. Medir presentación sin imponer todavía un umbral.
- El reloj virtual conserva el orden determinista de eventos; input latcheado por VBlank. El reloj de pared permite recuperar hasta tres VBlanks; al reanudar una pausa restablece el plazo sin contabilizar pérdidas por ella.
- AudioWorklet consume un ring SAB. El Host aplica volumen y remuestreo; el audio no modifica VBlank.
- IndexedDB usa `schema_version`; cada tarjeta se escribe en una transacción con `durability: 'strict'`. Enviar copia al hilo principal al completar escritura y en `pagehide`. Guardar controles y volumen en `PortSettings`.
- Service worker con caché versionada del programa; migrar Pack y opciones anteriores sin pedir nuevamente el disco ni perder tarjetas. Mantener cambios upstream detrás de `DW1_PORT`, documentados y compatibles con el build PS1.

## UX & Interaction Patterns

Bienvenida con aviso de copia propia → selección → progreso → «Listo para jugar» → «Jugar». Ese único gesto habilita audio y lleva al título. Ofrecer opciones, Memory cards, borrado y pausa manual; pausar automáticamente al ocultarse la pestaña. Informar específicamente si faltan aislamiento cross-origin, SAB u OPFS y bloquear el arranque; una segunda pestaña también recibe un mensaje y no arranca. Solicitar almacenamiento persistente; si se deniega, avisar y sugerir exportar tarjetas. Tras la primera carga, permitir recarga offline y activar actualizaciones al recargar conservando datos.

## Cross-Story Dependencies

El hilo y la presentación habilitan E/S y título; estos habilitan selección, empaquetado y arranque completo. Controles preceden al remapeo; almacenamiento y Shell sostienen tarjetas, opciones y offline. Los checks verifican el conjunto.

La épica 2 aporta Pack y título nativo determinista. La épica 3 aporta audio, tarjetas y referencia: comparar inicialmente el título web contra nativo no demuestra paridad con consola. La épica 5 incorpora mejoras gráficas y el umbral final de rendimiento en hardware de referencia. Servir con COOP `same-origin` y COEP `require-corp` desde desarrollo; la publicación corresponde a la épica 7.
