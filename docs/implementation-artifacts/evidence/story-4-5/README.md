# Evidencia de la historia 4.5

Verificación del 2026-10-30 en el contenedor del proyecto, con Chromium, Firefox y el disco
real. Reemplaza a la de la sesión en la nube, que no tuvo disco ni Firefox. Los JSON contienen
solo valores de `PadRead`, VBlanks, tasas y memoria.

- `input-{keyboard,gamepad,hotplug,latency}-<navegador>.json`: 18 teclas y 14 botones de gamepad
  llegan a `PadRead`; mando conectado y desconectado sin resincronizaciones ni pausas; 120/120
  muestras aplicadas en el VBlank siguiente a su escritura, en ambos navegadores.
- `long-<navegador>.json`: L4 de presentación, 10 minutos con el Pack real en la imagen `dw-port`
  y software GL (no es el hardware de referencia; la L4 de rendimiento sigue en la 5.6). Muestra
  de título y demo en bucle, sin partida.

| | Chromium | Firefox |
|---|---|---|
| VBlanks / teóricos | 36.060 / 36.059,7 | 36.062 / 36.063,4 |
| Resincronizaciones | 0 | 0 |
| Frames pintados / presentados | 18.805 / 18.878 | 18.781 / 18.879 |
| Memoria, segundo y último minuto | 878 y 936 MiB | 1.026 y 1.007 MiB |
| VBlanks con el hilo principal bloqueado 2 s | 120 | 0 |

- `frame-leak-probe-chromium.jsonl`: sondas de 40 s, modos 0 (`ImageBitmap` mostrado), 1 (cerrado
  en el hilo del juego), 2 (sin transferir) y 3 (cerrado en el hilo principal), con y sin
  headless shell. Antes del cambio, la misma L4 midió 972 a 6.077 MiB en Chromium.

## Pendiente

- **Mando físico**: solo se probó un gamepad simulado con el mapeo estándar W3C.
- **Crecimiento de Chromium**: unos 6 MiB por minuto, en `deferred-work.md`.
