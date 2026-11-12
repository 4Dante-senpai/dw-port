# Epic 6 Context: Partida completa jugable (M6)

<!-- Compiled from planning artifacts. Edit freely. Regenerate with compile-epic-context if planning docs change. -->

## Goal

Que Digimon World USA se pueda jugar de principio a fin en el port sin caídas, en nativo y en el navegador. La épica deja de medir paridad contra el emulador de referencia y pasa a medir jugabilidad: un playtest automático recorre los 16 overlays durante horas, una suite de Escenas los cubre con capturas propias y una partida grabada en el port llega a los créditos. Se ejecuta antes que el Renderer WebGL2 por decisión del dueño del 2026-11-11.

## Stories

- Story 6.9: Playtest automático: grabador, bot y soak (va primero)
- Story 6.10: Partida real en testing y triage de caídas (va segunda)
- Story 6.1: Definición de la suite de Escenas
- Story 6.2: Mundo, combate y evolución
- Story 6.3: Entrenamiento, tiendas y objetos
- Story 6.4: Minijuegos y modo VS
- Story 6.5: Videos, eventos y mapas especiales
- Story 6.6: Final y créditos
- Story 6.7: Partida grabada completa
- Story 6.8: Regresión completa en CI

## Requirements & Constraints

- El jugador puede jugar una partida nueva hasta los créditos. Los 16 overlays (`BTL`, `STD`, `FISH`, `EVL`, `KAR`, `VS`, `MOV`, `DOO2`, `DOOA`, `TRN`, `SHOP`, `DGET`, `TRN2`, `MURD`, `ENDI`, `EAB`) tienen al menos una Escena de regresión.
- Criterio de jugabilidad de toda Escena nueva: termina sin caída, `PORT_ASSERT`, reporte de ASan/UBSan ni cuelgue; dos corridas dan la misma captura y el mismo estado; `native-headless` y `web` dan el mismo estado; el hash de la captura se fija después de revisarla a ojo.
- La referencia de PCSX-Redux no es criterio de aceptación para Escenas nuevas. Se captura solo para diagnosticar un bug visible. Las Escenas que ya tienen referencia siguen comparándose con ella.
- El playtest automático nocturno debe acumular horas de juego virtual sin fallos y visitar los 16 overlays. La meta inicial es de 20 horas por corrida y se ajusta al medir la velocidad del build headless.
- Una partida completa grabada en el port se reproduce en nativo hasta los créditos con el mismo estado en dos corridas. Que la reproducción con inputs grabados sea determinista de punta a punta sigue siendo un supuesto.
- Ningún dato del juego entra al repo: Memory cards de entrada, grabaciones de inputs de partidas reales y capturas viven fuera, en el almacenamiento del runner privado. El repo guarda hashes, semillas e inputs sintéticos.
- Todo cambio en `src/` va detrás de `DW1_PORT`, con su fila en `docs/port/src-changes.md`, y el build PS1 sigue siendo idéntico al original.
- Ninguna historia cierra sin evidencia automática.

## Technical Decisions

- El reloj virtual es el único dueño del tiempo y es determinista. En headless nunca espera al reloj de pared, así que una corrida avanza tan rápido como el CPU permita. Los inputs se muestrean una vez por VBlank y se aplican en el latch de ese VBlank.
- El formato de inputs es una línea por cambio con VBlank y máscara de botones en hexadecimal; el Host ya lo reproduce contando desde el frame 0 o desde una marca por símbolo. El grabador escribe ese mismo formato.
- El estado se compara por las variables nombradas por símbolo de la lista de variables de estado, con punteros traducidos (arena a dirección PS1, global nativo a símbolo más offset), para que nativo, ASan y web sean comparables.
- Niveles de test: L1 unitario sin datos; L2 Escenas con datos; L3 Playwright en Chromium y Firefox; L5 playtest automático con inputs grabados y bot con semilla sobre `native-headless` y `native-asan`, más una corrida corta en web.
- Los tests con datos del juego son clase B: corren en el runner privado con el disco montado de solo lectura y se omiten con código 77 si falta. El CI publica solo hashes, conteos y horas.
- El bot no modifica el motor: genera archivos de inputs desde una semilla. Un fallo se reproduce con semilla, inputs, Memory card de entrada y último VBlank.
- Los overlays están todos linkeados y sus datos se restauran al cargarlos; un detector de aliasing registra accesos a datos de un overlay que no está cargado. Las Escenas deben dar cero eventos o listarlos con motivo.

## Cross-Story Dependencies

- 6.9 entrega el grabador, el bot y el runner que usan 6.10 (triage), 6.7 (partida grabada) y 6.8 (regresión en CI).
- 6.10 necesita el entorno de testing del homelab y confirma con una Escena de combate la corrección del scratchpad hecha al cerrar la Épica 4.
- 6.1 define la suite que completan 6.2 a 6.6.
- La acción de retrospectiva pendiente «antes de la Épica 6, dividir `port/web/dev/index.html`» sigue abierta en `sprint-status.yaml`; el grabador web de 6.9 toca ese archivo.
- La Épica 9 (speed-up y save states) reutiliza el grabador y la reproducción de inputs.
