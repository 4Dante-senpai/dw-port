# Verificación 4.1 — 2026-10-17

Ejecutada dentro de este contenedor, sin commits ni capturas de pantalla.

- Build completo de `web`, `native-headless` y `native-asan`.
- Suites nativas completas: 39 y 37 tests registrados entonces, cero fallos; cuatro tests clase B omitidos en cada preset por falta de `DW1_DISC_DIR`/Pack. El nuevo `fnptr_scanner`, añadido después, también pasó en ambos presets.
- Tras corregir la salida de diagnóstico del hilo web, `frame0`, `watchdog` y `fnptr_scanner` volvieron a pasar en ambos presets nativos.
- `make -j8` y `make compare`: binarios PS1 idénticos.
- Inventario: 119 TU, 132 hallazgos de callbacks, 859 llamadas sin prototipo y 40 pares de firmas de wasm-ld; sin aridades directas pendientes ni drift.
- Suite web final completa: 14/14 CTest, cero fallos (198.48 s). `web_boot` agrupa ocho casos de navegador.
- Web: Playwright 1.64.0, Chromium (SwiftShader) y Firefox (Xvfb). Frame 0 real; patrón RGB15 independiente de tres frames; rechazo sin aislamiento; ejecución normal sin muestras. Cada presentación validada compara todos los píxeles RGBA y tiene cero diferencias.
- Una intermitencia inicial perdía el mensaje de pila durante el flush de stdio al retornar `main`. La salida web ahora se publica mediante `MAIN_THREAD_EM_ASM`; la suite web se repitió después de la corrección.
- El JS generado usa memoria compartida con initial=maximum=1024 páginas (64 MiB), sin crecimiento. El hilo comprueba una variable local en la ventana OT de 16 MiB sobre su pila estática de 4 MiB.

Los tiempos de `present-timing.json` incluyen inicialización en el primer frame y excluyen readback; no son una garantía de 59.94 Hz.

Pendiente de fidelidad: el a1 residual de `setActiveAnim(12)` requiere referencia; se hace trap si existe el NPC afectado. Sigue diferido el hallazgo de título monocromo con el sanitizador de funciones de la iteración anterior. Pack, almacenamiento, input y audio web quedan fuera de esta historia. No se probó una partida completa.
