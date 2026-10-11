---
title: "Product Brief: DW1 Web Port"
status: final
created: 2026-10-11
updated: 2026-10-11
---

# Product Brief: DW1 Web Port

## Resumen

DW1 Web Port lleva *Digimon World* (PlayStation, 1999, versión USA) al navegador como un
programa nativo: el código C del juego, descompilado por el proyecto comunitario `dw_decomp`, se
compila a WebAssembly y corre sobre una reimplementación propia de las librerías de Sony (PsyQ)
y de los chips de la consola. No hay emulador ni CPU MIPS simulada. El jugador abre una URL,
elige la imagen de su propio disco y juega a 59,94 Hz con video, audio, controles y partidas
guardadas.

El momento es bueno por dos datos medidos el 2026-10-11: el código del juego de la versión USA
está **100 % en C** (2741 funciones, 931.800 bytes; solo quedan en asm las librerías PsyQ, que el
port reemplaza igual), y **97 de sus 120 archivos C ya compilan a wasm32 sin cambios**. Además
existe una referencia probada del mismo camino: DW2-PC, el port nativo de *Digimon World 2*
(CC0), con capa PsyQ, GPU/SPU/XA/MDEC emulados y pack de datos verificado por hashes.

## El problema

Hoy, para jugar *Digimon World* hay que tener la consola o instalar y configurar un emulador
(BIOS, plugins, controles, memory cards). El emulador reproduce el hardware con sus límites
(resolución de 320×240, polígonos que tiemblan, texturas sin perspectiva) y no ofrece una forma
simple de jugar desde cualquier computadora sin instalar nada. Los ports nativos hechos a partir
de una descompilación resuelven eso en escritorio, pero ninguno corre en el navegador.

## La solución

Una página web que:

1. Pide la imagen del disco del jugador (`.bin/.cue`), la valida archivo por archivo contra un
   manifest de hashes y arma localmente un pack de datos que queda guardado en el navegador
   (no se pide el disco otra vez).
2. Ejecuta el juego completo —título, mundo, combates, menús, videos, voces XA, música y
   efectos— con la lógica original intacta.
3. Acepta teclado y gamepad con remapeo, guarda partidas de forma persistente e importa/exporta
   memory cards `.mcd` compatibles con emuladores.
4. Ofrece mejoras de video opcionales: escala de resolución, sin wobble y, si es viable, 16:9.
5. Funciona offline después de la primera carga.

Nada del juego viaja por la red: el servidor sirve solo el programa (código propio y el C
descompilado de upstream); los datos salen siempre del disco del usuario.

## Qué lo hace distinto

- **No es un emulador**: es el C del juego compilado, con la plataforma reemplazada. Eso
  permite mejoras que un emulador hace con trucos (sin wobble, mayor resolución) y un `.wasm`
  chico sin JIT.
- **Sin instalación**: navegador de escritorio, una URL.
- **Ventaja de ejecución, no tecnológica**: el trabajo difícil (la descompilación) ya lo hizo la
  comunidad, y DW2-PC muestra la arquitectura. Lo nuevo es la capa PsyQ que DW1 usa y DW2 no
  (pipeline TMD de `libgs`, secuenciador de `libsnd`, `CdSearchFile`, `PadRead`: 121 de las 202
  funciones PsyQ que usa el juego no existen en DW2-PC) y la plataforma web (hilos, audio,
  almacenamiento, renderer WebGL2).

## A quién sirve

- **Primario:** jugadores que tienen su copia de *Digimon World* (USA) y quieren jugarla en una
  PC sin instalar ni configurar un emulador. Éxito para ellos: abrir la URL, elegir el archivo una
  vez y jugar de principio a fin con sus saves a salvo.
- **Secundario:** jugadores que ya usan emuladores y quieren llevar su partida (`.mcd`) al port y
  de vuelta.
- **Terciario:** la comunidad de `dw_decomp` y de modding de DW1, como banco de pruebas del
  decomp fuera de la consola (sin que el port acepte mods).

## Restricciones

- **Legales (no negociables):** sin datos del juego en el repositorio ni en el deploy (imagen,
  archivos extraídos, ejecutable original, bytes del exe embebidos en el binario). Los manifests
  contienen solo hashes y tamaños. Sin compilador CodeWarrior en el repo.
- **Upstream:** los cambios en `src/` son mínimos, detrás de `#ifdef DW1_PORT`, sin alterar el
  build PS1, y se registran para poder mergear `upstream/main`.
- **Sin compilador matching hoy** (`cc_mips.dll` no disponible): lo que se toque en `src/` se
  valida por comportamiento, no por `make compare`.
- **Plataforma:** Chrome y Firefox de escritorio. Se apunta a hilos con `SharedArrayBuffer`, que
  exige servir con COOP/COEP desde el servidor propio.

## Criterios de éxito

- Pantalla de título en nativo (Linux) con renderer software, y después en el navegador.
- El juego completo jugable de inicio a fin en el navegador, verificado con tests de regresión
  por escena.
- 59,94 Hz estables en Chrome y Firefox de escritorio en hardware medio
  [ASSUMPTION: "hardware medio" se define en el PRD con un equipo de referencia concreto].
- Frames idénticos al emulador de referencia a 1× sin mejoras, en las escenas de prueba.
- Un save exportado del port carga en DuckStation/PCSX-Redux y viceversa.
- Cero bytes del juego en el repo y en el deploy, verificado automáticamente.

## Alcance

**Dentro:** versión USA (SLUS-01032), juego completo, selección y validación del disco en el
navegador, pack local persistente, teclado y gamepad con remapeo, saves persistentes e
import/export `.mcd`, opciones de video (escala, sin wobble, 16:9 si es viable), offline tras la
primera carga, build nativo Linux para depurar, deploy en servidor propio.

**Fuera:** multijugador (incluido el modo VS entre dos jugadores
[ASSUMPTION: el modo VS local contra la CPU sí entra, por ser parte del juego base]), mods o
datos alternativos, versiones JP y PAL, móviles y tablets
[ASSUMPTION: no se descartan, pero no son objetivo], hosting de datos del juego, cuentas o
saves en la nube.

## Riesgos principales

Detallados en `docs/research/risks.md`. Los críticos: el tamaño de la capa PsyQ nueva, el modelo
de hilos del navegador para un loop que bloquea, y mantener los datos del juego fuera del
binario (DW2-PC sí embebe bytes del exe; aquí no se permite).

## Visión

Si funciona, el port es la forma más simple de jugar *Digimon World* en una computadora con la
copia propia, con mejor imagen que el original y sin romper su lógica. A futuro, el mismo
código sirve de base para las otras versiones del disco que `dw_decomp` ya soporta, y la capa
PsyQ web puede servir a otros ports desde descompilaciones de PS1.
