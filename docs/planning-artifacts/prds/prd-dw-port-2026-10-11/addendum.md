# Addendum técnico al PRD: DW1 Web Port

Contenido que no es requisito pero condiciona la arquitectura. Fuente: `docs/research/`
(mediciones del 2026-10-11). La arquitectura (`bmad-architecture`) decide; esto es insumo.

## Datos medidos que acotan el problema

- Código del juego USA: 2741 funciones, 931.800 bytes, 100 % en C. Solo PsyQ (612 funciones,
  144.348 bytes) sigue en asm y se reemplaza (`decomp-status.md`).
- 97/120 archivos C del juego compilan a wasm32 sin cambios; los 23 restantes fallan solo por
  macros GTE inline (11 macros, 181 usos) y el crt0 (`web-constraints.md` §8).
- 202 funciones PsyQ usadas, 2976 llamadas; DW2-PC cubre 81 (`psyq-usage.md`).
- 0 accesos a registros de hardware desde el juego; 484 al scratchpad; ~10 direcciones
  `0x80xxxxxx` fijas (`hardware-access.md`).
- 16 overlays con regiones solapadas, recarga con reinicio de `.data`/`.bss` (`overlays.md`).

## Opciones técnicas consideradas (para ADRs)

| Tema | Opción preferida por la investigación | Alternativas |
|---|---|---|
| Funciones no descompiladas | No hace falta `port/recomp/` en USA (0 pendientes) | Híbrido MIPS→C solo si aparecen en otras versiones |
| Capa PsyQ | Propia, partiendo de DW2-PC (CC0) y traduciendo desde `asm/us/main/psyq/*.s` | PsyCross (licencia a verificar) |
| Hilos web | `-sPROXY_TO_PTHREAD` + COOP/COEP | JSPI (si Firefox lo estabiliza), ASYNCIFY |
| Memoria | Arena de 2 MB `PS1_RAM()`, OTs de 24 bits en los primeros 16 MB de la memoria wasm | Traducir enlaces de OT con tabla |
| Overlays | Todo linkeado; `.data`/`.bss` por overlay agrupados y restaurados en `loadDynamicLibrary` | Lista generada de variables por overlay |
| Renderer | GPU PS1 software de DW2-PC → textura WebGL; WebGL2 propio después | — (SDL_GPU no tiene backend WebGL) |
| Audio | SPU emulado + `libsnd` traducido de DW1 → AudioWorklet por ring buffer | Audio de alto nivel (descartado por fidelidad) |
| Pack | Índice por ruta ISO9660 + SHA-256 + Form 2 crudo para XA/STR; mismo código C en nativo y web | Formato de DW2-PC por id (no aplica: DW1 usa `CdSearchFile`) |
| Almacenamiento | Pack en OPFS (lectura síncrona desde el worker); Memory cards/settings en IndexedDB u OPFS | Todo en IndexedDB |
| Datos del exe que PsyQ necesita | Leerlos del `SLUS_010.32` dentro del Pack en tiempo de ejecución o generarlos | `.incbin` (prohibido por regla 1) |

## Determinismo

`rand` de PsyQ (154 llamadas) y el timing de VBlank tienen que replicarse exactamente para que
FR-5 (partida grabada sin divergencia) sea verificable.
