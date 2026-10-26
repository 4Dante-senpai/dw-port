# Epic 3 Context: Paridad nativa contra el Emulador de referencia (M3)

<!-- Compilado a partir de los artefactos de planificación. Puede editarse libremente. Regenerar con compile-epic-context si cambia la planificación. -->

## Goal

Lograr que el Build nativo se vea, suene y guarde como la consola, y demostrarlo con evidencia automática: video, audio, estado y ritmo se comparan contra PCSX-Redux (fijado por SHA, con OpenBIOS) usando los mismos puntos de medición y tolerancias únicas, en la clase B de CI. La épica aporta SPU, `libsnd`, XA, MDEC, Memory card, la ventana SDL3 y la calibración de costos del reloj virtual, y deja el runner de Escenas que usan las épicas 4 a 6.

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

- **Video:** el área de display convertida a RGB555, a 1× y sin mejoras, coincide exacta; las excepciones se declaran por Escena con región y motivo. Ante una diferencia, el runner sale con error, guarda fuera del repo una imagen de diferencias e imprime la cantidad de píxeles distintos y la caja envolvente. Dos corridas de la referencia dan el mismo SHA-256.
- **MDEC:** por frame, diferencia máxima por canal ≤ 4/255 y media ≤ 0,5/255 (supuesto a calibrar); al final del video, audio y video difieren en ≤ 1 frame; Start sale del video en el mismo VBlank que la referencia.
- **Audio (gate vigente, decisión 4 del dueño del 2026-10-19):** bloquea la igualdad de la secuencia ordenada de pares `(registro, valor)` de voces (`registro < 0x180`), conservando duplicadas. En `title_audio` el tramo 1 se compara entero; el tramo 2 arranca en la primera entrada a `SsSeqPlay` posterior a `gameLoop`, anclado en la primera escritura del bundle de una nota nueva confirmada por dirección de muestra y KON de la misma voz (ambos capturadores agrupan escrituras por flush hasta KON alto `0x18a`). Cierra antes de la siguiente entrada pública a `libsnd` desde el juego, con el mismo `stop_on`; un cierre antes de confirmar el ancla falla. Cada captura exige ≥ 9 s (396.900 muestras) y se compara el intervalo común `[0, min(fin_port, fin_referencia))`, sin búsquedas de prefijos ni ajustes. Timestamp: `(ciclo − ciclo del inicio del tramo) / 768`.
- **Audio, métricas no bloqueantes hasta 3.10:** regla B (valores por registro y VBlank), desfase en muestras (máximo y media) y SNR del PCM a 44,1 kHz alineado por correlación cruzada en ventanas de 5 s (objetivo ≥ 40 dB, pendiente de calibrar). Se calculan y se reportan, junto con la ventana común y las escrituras excluidas. El video entre `unloadNewGameScene` y `gameLoop` queda fuera de `title_audio`, con orígenes independientes.
- **Estado:** las variables de `state_vars.txt` (por símbolo, tipadas) son idénticas en los puntos de control, con punteros traducidos (arena → `0x80000000 | offset`; global nativo → símbolo + offset). Una divergencia nombra la primera variable distinta y su VBlank; `rand` da la misma secuencia en ambos lados.
- **Ritmo:** 0 VBlanks de diferencia en cada punto de control de `title`, `movie0` y una carga de mapa; `movie0_skip` exacto (vigente desde 3.8; 3.10 calibra además el llenado de `ClearImage` tras `CdlPause` y la fase de `PadRead`). 10 minutos de `native` con reloj de pared difieren del teórico en < 1 VBlank. El SPU genera exactamente 44.100 × 1001/60000 muestras por VBlank, con acumulador exacto.
- **Memory card:** `.mcd` crudo de 131.072 bytes que PCSX-Redux carga y muestra; escritura por temporal + `rename`, de modo que un `SIGKILL` deja el estado anterior o el nuevo, nunca uno mezclado.
- Desaparecen los stubs `stub-m2:` de `libsnd`/`libspu` y los de video de la Historia 2.14; el título sigue exacto con el video real.
- Sin disco, BIOS, capturas, audio ni tarjetas de referencia en el repo: solo hashes, secuencias de inputs y métricas. Los cambios en `src/` van detrás de `DW1_PORT`.

## Technical Decisions

- **Origen y captura:** el frame 0 es el retorno del primer `VSync` llamado desde `main`, detectado por símbolo (en la referencia, por breakpoint). Los inputs se aplican en el latch del VBlank k; la captura toma el área de display según GP1, después de despachar los eventos del VBlank k y antes de volver al juego.
- **Emulador de referencia:** PCSX-Redux compilado desde un SHA fijo, con OpenBIOS, en modo `-no-ui -dofile` con Lua para capturar frames, audio y RAM. Se ejecuta con la imagen `dw-port`, fijada por digest. Script: `tools/port/reference/capture.py`.
- **Escena:** `tools/port/scenes/<id>.toml` con id estable, overlay, punto de entrada (inputs por VBlank desde el frame 0, o Memory card guardada fuera del repo), frames a capturar, SHA-256 esperado y excepciones justificadas. La referencia se captura una vez y su hash queda en el `.toml`; `run.py <id> --build native-headless --compare` (o `--compare-audio`) captura el port en el mismo punto.
- **CI clase B:** runner `dw1-data`, disco montado de solo lectura (`DW1_DISC_DIR=/private/disc`), solo en pipelines de ramas propias (`push`, `web`, `schedule`), nunca en merge requests de forks. Publica solo hashes, métricas y diffs numéricos. Los L1 (SPU/ADPCM, XA sintético, reloj, corte de escritura de la tarjeta) corren sin datos.
- **Reloj (AD-8):** `port/psyq/clock.c` es el único dueño del tiempo (ciclos a 33.868.800 Hz y una cola de eventos). Cada entrada PsyQ avanza un costo fijo y las esperas saltan al próximo evento. Costo de CD: 75/150 sectores/s según `CdlModeSpeed`, con acumulador fraccional y seek fijo. Costo de GPU: proporcional a lo dibujado o transferido, nunca redondeado a VBlank. Costo de MDEC: por macrobloque. Se pueden ajustar los números, no la forma; las constantes van en `port/psyq/clock_costs.h` con la Escena que las calibró. Hasta 3.10, solo VBlank y RCnt2 caen en el ciclo exacto (sin costos de CPU modelados). El reloj de pared solo pausa en `VSync`, recupera hasta 3 VBlanks y cuenta frames perdidos; en headless nunca pausa.
- **Audio (AD-10):** SPU en `port/backend/spu.{h,c}` desde `dw2-pc/backend/psxspu.c`, con atribución. Antes de cada escritura de registro y al cerrar cada VBlank, el SPU avanza hasta el tiempo virtual. `libsnd` (21 funciones, tick por root counter `SS_TICK240`) y `libspu` se traducen del asm de `mw_lib41`, con comentario `asm:`. XA se decodifica en `port/backend/xa.{h,c}` y entra al SPU por la entrada de CD, desde sectores F2 del Pack. El Host es el único dueño de la deriva: remuestrea según el nivel del buffer, con banda muerta, y el juego nunca ajusta el VBlank por audio.
- **MDEC:** `port/backend/mdec.{h,c}` y `libpress` traducida; las tablas VLC se leen de `SLUS_010.32` en el Pack (no se embeben).
- **Memory card:** las 11 funciones de `libmcrd` se traducen del asm; el `.mcd` lo escribe `port/host`.
- **Preset `native`:** SDL3 3.4.16 i686 en la imagen `dw-port`, con la ventana en el hilo principal y el juego en su hilo. En CI se verifica con `SDL_VIDEODRIVER=offscreen` y `SDL_AUDIODRIVER=dummy`, contra la captura de `native-headless`.

## Hipótesis a validar contra la referencia

- OpenBIOS equivale a un BIOS real: el dueño repite la captura una vez con su BIOS, fuera del repo, y anota el resultado en `docs/port/reference.md`. Hasta entonces, la referencia es provisional.
- Las tolerancias de MDEC, el objetivo de SNR ≥ 40 dB y las constantes de costo de CD/GPU/MDEC se calibran en esta épica.
- `SHOP_REL.BIN` se usa en USA: 3.11 lo cierra con un breakpoint en su carga y registra en `docs/research/overlays.md` si se carga y desde qué función.

## Cross-Story Dependencies

- 3.1 habilita todo, porque sin captura de referencia no hay hashes. 3.2 y 3.3 construyen el runner que usan de 3.4 a 3.8, 3.10 y 3.11.
- 3.4 precede a 3.5 y 3.6. 3.7 reemplaza los stubs de video de 2.14. 3.8 verifica la tarjeta con `capture.py` en PCSX-Redux. 3.9 reutiliza el audio de 3.4 a 3.6 y el resampleo del Host.
- 3.10 necesita las Escenas `title`, `movie0`, `movie0_skip` y una carga de mapa, y desbloquea las métricas de audio que hoy no bloquean (sincronización y estado heredado de 3.5).
- La épica 2 aporta el Build nativo headless y el título determinista. La épica 4 hereda el audio, las tarjetas y la referencia: comparar web contra nativo no demuestra paridad con la consola.
