# Epic 3 Context: Paridad nativa contra el Emulador de referencia (M3)

<!-- Compilado a partir de los artefactos de planificación. Puede editarse libremente. Regenerar con compile-epic-context si cambia la planificación. -->

## Goal

Lograr que el Build nativo se vea, suene y guarde como la consola, y demostrarlo con evidencia automática: video, audio, estado y ritmo se comparan contra un Emulador de referencia fijado, en puntos de medición y con tolerancias únicas. La épica aporta SPU, `libsnd`, XA, MDEC, Memory card, la ventana SDL3 y la calibración de costos, y deja el runner de Escenas que usarán las épicas 4 a 6.

## Stories

- Story 3.1: Emulador de referencia y captura automática
- Story 3.2: Runner de Escenas y pantalla de título exacta
- Story 3.3: Inputs por VBlank y comparación de estado
- Story 3.4: SPU emulado y `libspu`
- Story 3.5: `libsnd` de DW1 y traza de audio
- Story 3.6: XA y sectores Form 2
- Story 3.7: MDEC, `libpress` y videos STR
- Story 3.8: Memory card nativa
- Story 3.9: Preset `native` con ventana SDL3
- Story 3.10: Calibración de costos y ritmo nativo
- Story 3.11: Hipótesis pendientes de los overlays

## Requirements & Constraints

- Video: el área de display convertida a RGB555, a 1× y sin mejoras, debe coincidir de forma exacta; toda excepción se declara por Escena con región y motivo. Ante una diferencia, el runner sale con error, guarda fuera del repo una imagen de diferencias e imprime cantidad de píxeles y caja envolvente.
- MDEC: por frame, diferencia máxima por canal ≤ 4/255 y media ≤ 0,5/255; audio y video terminan con ≤ 1 frame de diferencia, y Start sale del video en el mismo VBlank.
- Audio: enmienda del dueño del 2026-10-19, historia 3.5, decisión 4 (AD-15): bloquea la igualdad ordenada de pares `(registro, valor)` de voces (`registro < 0x180`), incluidas duplicadas. En `title_audio`, tramo 1 entero; tramo 2 armado en el primer `SsSeqPlay` después de `gameLoop`, con origen en el comienzo del primer bundle de nota nueva confirmado por dirección de muestra y KON de la misma voz. Ambos capturadores usan el mismo detector por flush hasta KON alto; conservan el bundle completo. El cierre sigue siendo antes de la siguiente entrada pública a `libsnd` desde el juego, con `stop_on` intacto. Cada lado exige nueve segundos (396900 muestras); se compara el intervalo común `[0, min(fin_port, fin_referencia))`, sin búsquedas de prefijos ni ajustes. Regla B por registro/VBlank, desfases de muestras y SNR por ventanas de 5 s se calculan y reportan sin bloquear hasta 3.10. Timestamp: `(ciclo − ciclo del ancla) / 768`. Motivo: la decisión 3 capturaba recálculos de voces heredadas y exigía diez segundos donde el primer evento deja unos 9,3/9,5 s; los costos de CPU/CD sin calibrar desplazan el final. Se mantienen las decisiones 1 y 2 sobre costos de CPU y ciclos de VBlank/RCnt2; sincronización y estado heredado quedan para 3.10.
- Estado: las variables de `state_vars.txt` (por símbolo, tipadas) son idénticas en puntos de control; ante divergencia se nombra la primera variable y el VBlank. `rand` debe dar la misma secuencia.
- Ritmo: 0 VBlanks de diferencia en cada punto de control; 10 minutos nativos con reloj de pared difieren del teórico en < 1 VBlank. El SPU genera exactamente 44.100 × 1001/60000 muestras por VBlank, con acumulador exacto.
- Memory card: `.mcd` crudo de 131.072 bytes, cargable por PCSX-Redux; escritura por temporal + `rename`, de modo que una muerte por `SIGKILL` deja el estado anterior o el nuevo, nunca uno mezclado.
- Los stubs `stub-m2:` de `libsnd`/`libspu` y los de video de la Historia 2.14 deben desaparecer; el título sigue exacto con el video real.
- Sin disco, sin BIOS ni capturas de referencia en el repo: solo hashes, secuencias de inputs y métricas. Los cambios en `src/` van detrás de `DW1_PORT`.

## Technical Decisions

- Emulador de referencia: PCSX-Redux compilado desde un SHA fijo, con OpenBIOS (sin BIOS de Sony), modo `-no-ui -dofile` con scripting Lua para capturar frames, audio y RAM. La historia lo pide disponible en la imagen `dw-port` (fijada por digest); la evaluación previa (`docs/port/setup.md`) sugería una imagen separada porque compilarlo arrastra dependencias y exige contexto OpenGL (Xvfb en headless). La forma concreta (etapa de build separada que solo copia el binario, o imagen aparte) se decide en la spec de 3.1. DuckStation `regtest` fue descartado por requerir BIOS real con copyright.
- Frame 0: retorno del primer `VSync` llamado desde `main`; ambos lados lo detectan por símbolo (en la referencia, con breakpoint). Los inputs se aplican en el latch del VBlank k. La captura toma el área de display según GP1, tras despachar los eventos del VBlank k y antes de volver al juego.
- Escena: `tools/port/scenes/<id>.toml` con id estable, overlay, punto de entrada (inputs por VBlank desde el frame 0, o Memory card guardada fuera del repo), frames a capturar, SHA-256 esperado y excepciones justificadas. `capture.py` y `run.py` son los scripts de `tools/port/`.
- Método de comparación: la referencia se captura una vez y su hash queda en el `.toml`; `run.py --build native-headless --compare` captura el port en el mismo punto y compara RGB555. Dos corridas de la referencia deben dar el mismo SHA-256.
- CI clase B: runner con etiqueta `dw1-data`, disco montado de solo lectura en `/private` (`DW1_DISC_DIR=/private/disc`), solo en pipelines de ramas propias (`push`, `web`, `schedule`), nunca merge requests de forks. No sube artefactos derivados del disco: solo hashes, métricas y diffs numéricos. Imágenes, audio, tarjetas de referencia y diffs viven en el almacenamiento privado del runner. L1 (SPU/ADPCM, XA sintético, reloj, corte de escritura) corre sin datos.
- SPU y XA adaptados de `dw2-pc` con atribución; el SPU corre hasta el tiempo virtual antes de cada escritura de registro. `libsnd` (21 funciones, tick por root counter `SS_TICK240`) y `libmcrd` (11) se traducen del asm con comentario `asm:`. Las tablas VLC de MDEC se leen del Pack.
- Resampleo del Host por nivel de buffer (AD-10); preset `native` con SDL3 3.4.16 i686, verificado en CI con drivers `offscreen` y `dummy`.
- Constantes de costo de CD, GPU y MDEC en `port/psyq/clock_costs.h`, con la Escena que las calibró.

## Hipótesis a validar contra la referencia

- OpenBIOS equivale a un BIOS real para estas capturas: el dueño repite una vez la captura con su BIOS fuera del repo y el resultado (iguales o diferencias listadas) va a `docs/port/reference.md`. Es la pregunta abierta 3 del PRD; hasta entonces la referencia con OpenBIOS es provisional.
- Tolerancias de MDEC (≤ 4/255, ≤ 0,5/255) y SNR ≥ 40 dB: supuestos a calibrar en esta épica. La decisión 4 del 2026-10-19 modifica únicamente el gate de audio de 3.5 descrito en AD-15; las demás reglas de fidelidad se conservan.
- Constantes de costo de AD-8 (CD, GPU, MDEC): se calibran aquí.
- `SHOP_REL.BIN` se usa en USA (`docs/research/overlays.md`, pregunta abierta 5 del PRD): se cierra con evidencia en 3.11.
- Cualquier dato de hardware no verificado se marca como hipótesis y se contrasta con la referencia antes de depender de él.

## Cross-Story Dependencies

3.1 habilita todo: sin captura de referencia no hay hashes. 3.2 y 3.3 construyen el runner que usan 3.4 a 3.8, 3.10 y 3.11. 3.4 precede a 3.5 y 3.6 (SPU antes de secuenciador y XA); 3.7 reemplaza los stubs de video de 2.14 y 3.8 verifica la tarjeta cargándola en PCSX-Redux. 3.9 reutiliza el audio de 3.4 a 3.6. 3.10 necesita las Escenas `title`, `movie0` y de carga de mapa.

La épica 2 aporta el Build nativo headless y el título determinista. La épica 4 hereda audio, tarjetas y referencia: comparar el título web con el nativo no demuestra paridad con consola.
