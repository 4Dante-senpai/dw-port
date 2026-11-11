---
title: "Propuesta de cambio de sprint: jugabilidad primero"
date: 2026-11-11
status: approved
approved: 2026-11-11 (dueño; delegó la aplicación de los cambios al orquestador)
scope: major
mode: batch
---

# Propuesta de cambio de sprint — 2026-11-11

## 1. Resumen del problema

**Disparador:** revisión del enfoque pedida por el dueño al cerrar la Épica 4 (M4). No lo dispara
una historia: es un desajuste entre el plan y lo que el dueño espera del producto.

**Tipo:** malentendido de requisitos y prioridad equivocada, no un enfoque técnico fallido.

**Problema:** el plan ordena el trabajo como si el producto fuera un emulador de referencia. Cada
historia cierra con paridad exacta contra PCSX-Redux, y jugar de verdad queda para la Épica 6,
detrás del Renderer WebGL2. Lo que el dueño quiere es un juego jugable en el navegador con
comodidades (velocidad ×2, autosave, escala ×2), y dos de esas tres no están en el alcance de la v1.

**Evidencia:**

- En 26 días (2026-10-11 a 2026-11-05) se cerraron M0 a M4. El proyecto no está trabado.
- Tamaño medido el 2026-11-11: unas 16.500 líneas de port, 14.000 de tests, 10.600 de
  herramientas y 50.000 de documentación.
- Escenas verificadas: 10 en nativo y 3 en web. Todas son de título, partida nueva, carga de mapa,
  nombres, película y Memory card. Ninguna cubre combate, entrenamiento ni mundo.
- `BTL_drawTMD` caía en web al escribir en el scratchpad (`0x1F800078`). Afectaba a todos los
  efectos de `BTL`, `STD`, `VS` y a la pesca, y se detectó recién al cerrar la Épica 4.
  `docs/STATUS.md` todavía dice «Falta confirmarlo en partida real».
- PRD §8.2: el speed-up quedó para v2. Autosave y save states no figuran en el PRD ni en las épicas.
- La escala de resolución (Historia 5.2) depende del Renderer WebGL2 (Historia 5.1).

## 2. Análisis de impacto

### Checklist

| Ítem | Estado | Nota |
|---|---|---|
| 1.1 Historia disparadora | [N/A] | Revisión del dueño al cierre de la Épica 4 |
| 1.2 Problema definido | [x] | Prioridad y alcance, no técnica |
| 1.3 Evidencia | [x] | Sección 1 |
| 2.1 Épica actual | [x] | Épica 4 cerrada; no cambia |
| 2.2 Cambios de épicas | [x] | Épica 6 redefinida, Épica 5 recortada, Épica 9 nueva |
| 2.3 Épicas restantes | [x] | Épica 7 sin cambios; Épica 8 sin cambios |
| 2.4 Épicas nuevas u obsoletas | [x] | Épica 9 nueva; ninguna obsoleta |
| 2.5 Orden | [x] | 6 → 9 → 5 → 7 |
| 3.1 PRD | [x] | FR-5, FR-12, NFR-8, §8, §9, §10; FR-18 a FR-20 nuevos |
| 3.2 Arquitectura | [x] | Enmiendas a AD-8, AD-14 y AD-15 |
| 3.3 UX | [N/A] | No hay documento de UX; las pantallas salen de FR-15 |
| 3.4 Otros artefactos | [x] | `sprint-status.yaml`, `STATUS.md`, `CLAUDE.md`; CI con job programado de soak |
| 4.1 Ajuste directo | Viable | Elegido. Esfuerzo medio, riesgo bajo |
| 4.2 Rollback | No viable | No hay nada que revertir: lo hecho se usa tal cual |
| 4.3 Revisión del MVP | Viable | Parcial: sube speed-up, save states y escala entera a v1 |

### Épicas

- **Épica 6 (M6):** pasa a ser la siguiente. Cambia de «sin divergencias contra la referencia» a
  «jugable de principio a fin». Suma dos historias que van primero: playtest automático (6.9) y
  partida real con triage de caídas (6.10).
- **Épica 9 (nueva):** comodidades del navegador: escala entera de pantalla, speed-up, save
  states y autosave. Va después de la Épica 6.
- **Épica 5 (M5):** va después de la Épica 9. Conserva el Renderer WebGL2, la resolución interna,
  sin wobble y 16:9. La medición de rendimiento (5.6) no depende del Renderer WebGL2 y se puede
  adelantar.
- **Épica 7 (M7):** sin cambios.

### Lo que no cambia

- Regla «no es un emulador», build matching, divergencia mínima con upstream y datos fuera del repo.
- Las 10 Escenas que ya se comparan contra PCSX-Redux siguen comparándose: ese costo ya está pagado.
- El reloj virtual determinista (AD-8). Es lo que hace baratos el playtest automático y el speed-up.

## 3. Enfoque recomendado

**Ajuste directo con revisión parcial del MVP.**

1. **Jugar primero.** Épica 6 antes que la Épica 5.
2. **Bajar la vara de paridad para lo nuevo.** Una Escena nueva cierra si no cae, es determinista,
   da el mismo estado en nativo y en web, y su captura fue revisada a ojo antes de fijar el hash.
   La comparación contra PCSX-Redux queda como herramienta de diagnóstico ante un bug visible.
3. **Subir a v1** el speed-up, los save states con autosave y la escala entera de pantalla.
4. **Automatizar el playtest**, para no depender de que el dueño juegue a mano.

**Esfuerzo:** medio. **Riesgo:** bajo para 1, 2 y 4; alto para los save states (ver 4.3).
**Efecto en el plazo:** adelanta el momento en que el juego se puede jugar completo; atrasa el
Renderer WebGL2.

### Playtest automático (Historia 6.9)

Se apoya en lo que ya existe: `dw1 --headless --inputs FILE --vblanks N --card1 FILE`, ASan/UBSan
y el reloj virtual, que en headless no espera al reloj de pared.

- **Grabador de inputs.** El port graba los inputs por VBlank en el mismo formato que lee
  `--inputs`, en `native` con ventana y en web (descarga de archivo). Una sesión del dueño queda
  como test de regresión.
- **Bot.** Un script genera archivos de inputs a partir de una semilla (botones con pesos y
  duraciones). No toca el motor. Arranca desde el frame 0 o desde Memory cards guardadas en
  distintos puntos de la partida, que viven fuera del repo.
- **Qué cuenta como fallo:** señal o código de salida distinto de cero, `PORT_ASSERT`, reporte de
  ASan o UBSan, y cuelgue (el contador de VBlanks no avanza dentro de un tiempo de pared).
- **Qué cuenta como aviso:** display apagado o frame sin cambios durante un tramo largo.
- **Reproducción.** Un fallo deja la semilla, el archivo de inputs, la Memory card usada y el
  último VBlank. Como el port es determinista, esos datos lo reproducen.
- **Oráculo sin emulador.** La misma corrida da el mismo hash de estado en `native-headless`,
  `native-asan` y web.
- **CI.** Una corrida corta en cada merge (clase B) y un soak nocturno programado con varias
  semillas y Memory cards.

## 4. Cambios detallados

### 4.1 PRD (`prd.md`)

| Sección | Antes | Después |
|---|---|---|
| FR-5 | Partida grabada sin divergencia de estado respecto del Emulador de referencia | Partida grabada en el port que llega a los créditos, determinista y con el mismo estado en nativo y web. La referencia queda como diagnóstico |
| FR-12 | Escala de resolución (una sola) | Sin cambios de texto; se aclara que es la resolución interna y que la escala de pantalla es FR-20 |
| FR-18 (nuevo) | — | Speed-up ×2 y avance rápido |
| FR-19 (nuevo) | — | Save states y autosave |
| FR-20 (nuevo) | — | Escala entera de pantalla y pantalla completa |
| NFR-8 | Toda Escena de la suite coincide píxel a píxel con la referencia | Vale para las Escenas con referencia ya capturada; las nuevas usan captura propia revisada |
| §8.2 | Speed-up fuera de v1 | Speed-up dentro de v1 |
| §9 | SM-1 a SM-5 | Suma SM-6: horas de juego virtual del soak sin fallos |
| §10 | Pregunta 1 resuelta: no | Pregunta 1 reabierta y resuelta: sí. Pregunta 6 nueva: mecanismo de save states |

### 4.2 Épicas (`epics.md`)

- Orden de ejecución declarado: 6 → 9 → 5 → 7.
- Épica 6 renombrada «Partida completa jugable (M6)». Historias 6.2 a 6.6: el criterio «pasan
  frames, estado y audio contra la referencia» se reemplaza por el criterio de jugabilidad.
  Historia 6.7: la partida se graba en el port, no en PCSX-Redux. Historias 6.9 y 6.10 nuevas.
- Épica 9 nueva con las historias 9.1 a 9.4.
- Épica 5: la Historia 5.2 aclara que la escala de pantalla ya existe (9.1).

### 4.3 Arquitectura (`ARCHITECTURE-SPINE.md`)

- **AD-8:** el reloj de pared admite un factor de velocidad. No cambia el tiempo virtual ni el
  orden de eventos. Con factor distinto de 1 la contrapresión de audio se apaga.
- **AD-14:** el nivel L2 acepta Escenas con captura propia; nivel L5 nuevo para el playtest
  automático.
- **AD-15:** la comparación exacta contra la referencia aplica a las Escenas que ya la tienen.

**Riesgo de los save states [hipótesis sin validar].** Una copia de la memoria lineal de WASM no
alcanza: la pila de llamadas del hilo del Juego (variables locales y direcciones de retorno) vive
en la máquina virtual, fuera de esa memoria, y el Juego está bloqueado dentro de `VSync` con una
pila distinta en cada momento. La Historia 9.3 es un spike que elige entre:

1. **Reproducción determinista:** el estado es la Memory card de partida más los inputs grabados
   desde el arranque; cargar es volver a correr a máxima velocidad hasta ese VBlank. No agrega
   código al motor, pero el tiempo de carga crece con la duración de la sesión.
2. **Asyncify en el hilo del Juego:** vuelca la pila a memoria y permite la copia real. Aumenta
   el tamaño del `.wasm` (NFR-3) y cuesta rendimiento.
3. **Solo autosave sobre la Memory card:** si las dos anteriores no sirven, se documenta el límite.

El spike mide las dos primeras antes de comprometer la 9.4.

### 4.4 Otros

- `sprint-status.yaml`: Épica 6 antes que la 5, historias 6.9 y 6.10 al principio, Épica 9 nueva.
- `docs/STATUS.md`: sección «En curso» y hito siguiente.
- `CLAUDE.md`: objetivo, orden de hitos y regla 5.

## 5. Traspaso

**Alcance: mayor** (cambia el alcance de la v1 y el orden de las épicas). El dueño lo aprobó el
2026-11-11 y delegó la aplicación al orquestador, así que no hay escalamiento pendiente.

| Quién | Qué |
|---|---|
| Orquestador | Aplica los cambios de esta propuesta a los artefactos |
| Desarrollo (workflow `bmad-build`) | Historia 6.9 y luego 6.10 |
| Dueño | Juega en el entorno de testing con el grabador de inputs cuando la 6.9 lo entregue |

**Criterios de éxito:**

- El soak nocturno acumula horas de juego virtual sin caídas en los 16 overlays.
- El dueño juega una sesión completa en el entorno de testing sin caídas.
- Speed-up ×2, escala ×2 y reanudación de la partida disponibles en la Shell.
