# Epic 4 Context: Jugar en el navegador con el disco propio (M4)

<!-- Compiled from planning artifacts. Edit freely. Regenerate with compile-epic-context if planning docs change. -->

## Goal

Llevar el mismo código nativo de Digimon World USA al navegador mediante Emscripten, con presentación, audio, controles y partidas persistentes. El jugador aporta su propio disco, lo valida y convierte localmente una sola vez y puede volver a jugar sin conexión ni instalaciones. Esta épica establece la plataforma web y su fidelidad respecto del port nativo.

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

- Aceptar `.bin` de pista única `MODE2/2352`, con o sin `.cue`, por selector o arrastre. Rechazar modos incompatibles, `.iso` de 2048 bytes/sector y versiones JP/PAL; identificar el archivo inválido y la edición requerida, SLUS-01032. Validar tamaños y SHA-256 contra el manifest, sin dejar un Pack parcial. Leer por partes con un máximo de 512 MB de memoria de pestaña durante la validación.
- El Pack web debe ser idéntico byte a byte al nativo, persistir entre sesiones y poder borrarse. Las actualizaciones deben conservar Pack y tarjetas y migrar formatos anteriores sin solicitar nuevamente el disco.
- Soportar Chrome y Firefox de escritorio, en sus dos últimas versiones estables; verificar en Chromium y Firefox. Safari y móviles quedan fuera de v1.
- Mantener el ritmo calibrado de 60,0087172527 Hz: desviación del conteo teórico de VBlanks inferior al 0,1 % en diez minutos. Este valor actualizado reemplaza las menciones antiguas de 59,94 Hz.
- La imagen de título a 1× debe coincidir píxel a píxel con nativo y la referencia; el PCM del AudioWorklet antes del remuestreo debe coincidir con nativo. Verificar diez minutos sin underruns; la aceptación final de rendimiento en hardware de referencia corresponde a la épica 5.
- Limitar el `.wasm` con Brotli a 4 MB y la primera descarga sin Pack a 6 MB. Dos builds del mismo commit en la imagen Docker del proyecto deben producir el mismo `.wasm`.
- Disco, Pack y tarjetas permanecen en el dispositivo; la red solo solicita archivos propios del deploy. No distribuir datos extraídos: únicamente se admiten los bytes ya reproducidos por el C de upstream. Comprobar filtraciones mediante ventanas de 64 bytes contra los objetos compilados del juego.

## Technical Decisions

- Compilar C a wasm32, sin ejecución de instrucciones MIPS. Mantener cambios del juego aislados con `DW1_PORT` y documentados; preservar el build PS1.
- Ejecutar juego, PsyQ, backend y callbacks en un único pthread; reservar el hilo principal para interfaz e input. Usar `-pthread -sSUPPORT_LONGJMP=wasm -fwasm-exceptions`, memoria inicial fija sin crecimiento y objetos del juego sin LTO. El stack y todo elemento enlazable en las OT deben quedar en los primeros 16 MB. Si no se respeta el stack estático, aplicar y registrar el fallback `-sPROXY_TO_PTHREAD`. Resolver incompatibilidades de firmas de punteros a función para wasm. No usar ASYNCIFY ni JSPI.
- Servir con COOP/COEP. Transferir `OffscreenCanvas` al hilo del juego y presentar el rasterizador software como textura WebGL2; las mejoras gráficas pertenecen a la épica 5.
- Un worker exclusivo posee OPFS, construye el Pack con el mismo C usado por la herramienta nativa y abre el archivo antes del arranque. El juego solicita sectores por un buzón en `SharedArrayBuffer` y espera con `Atomics.wait`, sin acceder directamente a OPFS.
- Conservar un único reloj virtual determinista; el reloj de pared regula el ritmo sin ordenar eventos. Muestrear input en el hilo principal y aplicarlo en el latch del VBlank correspondiente. El AudioWorklet consume un anillo en memoria compartida; volumen y remuestreo pertenecen al Host.
- Guardar opciones y controles en `PortSettings` con `schema_version` en IndexedDB. Cada tarjeta ocupa 131.072 bytes por ranura y se escribe mediante una transacción con `durability: 'strict'`: una interrupción conserva la versión anterior o la nueva. Enviar copias al hilo principal tras escrituras y en `pagehide`.
- Reservar la instancia con el Web Lock `dw1-game`. Solicitar almacenamiento persistente. Usar caché versionada del programa mediante service worker y migraciones de almacenamiento.
- Separar pruebas sin datos, con fixtures sintéticos, de las pruebas con disco real en runner privado protegido. Exigir evidencia automática para fidelidad, persistencia, ritmo, tamaño, reproducibilidad y privacidad.

## UX & Interaction Patterns

El recorrido inicial muestra bienvenida y aviso de copia propia, selección, progreso, estado listo y «Jugar». Ese único gesto debe habilitar audio y llevar al título. Al regresar con Pack válido, ofrecer «Jugar» directamente. Mostrar errores específicos por capacidad ausente —aislamiento, memoria compartida o almacenamiento— y por otra pestaña activa; impedir el arranque en esos casos.

Ofrecer pausa manual y automática al ocultar la pestaña, congelando VBlanks y retomando sin penalizar la pausa como frames perdidos. Incluir opciones de video, audio y controles, gestión de tarjetas y borrado de datos. Los 14 botones digitales deben funcionar por defecto con teclado y gamepad, admitir remapeo persistente y restauración; conectar o desconectar mandos no interrumpe el juego. El stick izquierdo puede actuar como cruceta.

Mostrar dos ranuras con importación/exportación `.mcd`/`.mcr` compatibles con DuckStation y PCSX-Redux; rechazar tamaños distintos de 131.072 bytes. Si se deniega persistencia, informar y sugerir exportar la tarjeta.

## Cross-Story Dependencies

La épica 2 aporta el build nativo y el constructor de Pack compartido; la épica 3 aporta referencias de video, audio y tarjetas. Hilos y presentación habilitan E/S y título; el almacenamiento sustenta preparación del disco, persistencia y funcionamiento offline. Audio e input dependen del modelo de hilos y reloj. Los checks finales integran esas garantías. Las mejoras gráficas y el umbral final de rendimiento quedan para la épica 5; la publicación en servidor corresponde a la épica 7.
