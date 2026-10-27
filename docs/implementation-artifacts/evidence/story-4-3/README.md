# Evidencia de la historia 4.3

Verificación del 2026-10-27 dentro del contenedor del proyecto, sin commits ni
capturas de pantalla. Los JSON contienen exclusivamente metadatos: resultados,
hashes, memoria y tiempos. No contienen bytes de la imagen ni del Pack.

`verification.json` registra los comandos y resultados automáticos. La suite
sintética final contiene 26 casos Playwright, repartidos entre Chromium y
Firefox: selector, arrastre, errores del C, selección ambigua, persistencia
denegada, recarga, borrado y conservación del Pack anterior. También inyecta
fallas en la escritura del Pack y del sidecar; comprueba que no quedan temporales
y que se conservan el hash del archivo anterior y su sidecar.

`disc-real-{chromium,firefox}.json` compara el SHA del Pack nativo con el sidecar
y con una nueva lectura del archivo comprometido en OPFS. Los tres hashes deben
ser `c01e5c3680ffdf50d0342f16ec978c6d107a33529688dae6a817bd63947f0cf6`.
SM-4 se registra en milisegundos desde la selección hasta «Listo para jugar»,
antes de releer el archivo para la comprobación independiente. Es una medición
en este contenedor, con instrumentación de memoria; no una garantía de tiempo
en el hardware del usuario.

La API `performance.measureUserAgentSpecificMemory()` se muestrea en Chromium
con el navegador completo y `ForceEagerMeasureMemory`. La prueba sintética pausa
el worker antes de entrar al constructor C para asegurar que la muestra incluye
su heap wasm de 64 MiB; la imagen real se mide periódicamente durante la
validación. Se exige una muestra de al menos 64 MiB y un máximo de 512 MiB.
Firefox no expone esa API y registra `null`, sin sustituirlo por una estimación.

Las primeras suites nativas encontraron cinco fallas por referencias privadas
almacenadas en `build/` cuyos archivos externos de audio o tarjetas ya no estaban
disponibles. Para verificar el recorrido sin disco se apartaron temporalmente
los `us.pack` nativos y se usó `DW1_REFERENCE_DIR=/tmp/dw43-empty-reference`.
Se repitieron primero los casos afectados y luego ambas suites completas. Los
Packs originales se restauraron al terminar; no se alteraron las herramientas
de escenas ni se fabricaron referencias. Las pruebas privadas de esas escenas
se saltan con 77 en ese recorrido. El constructor nativo del disco real y su
paridad con el navegador se verifican por separado en esta misma evidencia.

El módulo comparte `pack_build`, el parser del Manifest y los errores con la CLI
nativa. Solo se añadió `sha256 <file>` a la CLI; su resultado sobre el Pack
sintético se comparó también con `hashlib` de Python. `pack.c`, `pack.h` y `src/`
no cambiaron. El reemplazo conserva el archivo anterior hasta publicar el
sidecar y lo restaura si esa publicación falla. La exclusión entre pestañas y
la recuperación después de cerrar el navegador abruptamente quedan fuera de
esta historia, como establece la spec.
