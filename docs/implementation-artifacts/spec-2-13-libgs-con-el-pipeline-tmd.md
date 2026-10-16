---
title: 'Historia 2.13: libgs con el pipeline TMD'
type: 'feature'
created: '2026-10-16'
baseline_commit: '1a0e552d94fbc76b2bfc87859c46ac76e641b9f9'
status: 'done'
route: 'dispatch'
review_loop_iteration: 0
context:
  - '{project-root}/docs/implementation-artifacts/epic-2-context.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** de las 35 funciones de `libgs` que usa el Juego solo existen `GsSetWorkBase` y
`GsGetWorkBase`; el resto, y las 27 `GsTMDfast*`/`GsTMDdiv*` que el Juego registra en `GsFCALL4`,
son stubs: ningún modelo 3D, sprite ni doble buffer llega a la OT.

**Approach:** traducir a C, desde `asm/us/main/psyq/libgs.s` y `libgte.s`, las 33 funciones de
`libgs` que faltan y las 27 `GsTMD*`, sobre el GTE del Backend (`port/backend/gte.h`) y `libgpu`,
con tests L1 cuyos esperados se calculan aparte del código.

## Boundaries & Constraints

**Always:** una función por entrada en `port/psyq/libgs.c` (las `GsTMD*` pueden ir en
`port/psyq/libgs_tmd.c`) con comentario `asm:asm/us/main/psyq/<lib>.s`, o `dw2-pc:psyq/libgs.c`
si se adapta de DW2-PC y coincide con el asm de DW1 (manda el asm); archivos adaptados con
`/* Adapted from DW2-PC (CC0): psyq/libgs.c @ 816ed0d */`; `clock_psyq_entry()` en cada entrada
que el Juego llama directamente; GTE solo vía `gte.h` (`gte_mtc2`/`gte_ctc2`/`gte_command`…);
enlaces de OT y de paquetes solo con `PS1_LINK`/`PS1_LINK_PTR`; internas del asm (`_make_packet`,
`gpu_init`, `valiable_init`, `GsSetDrawBuffOffset`, `GsSetDrawBuffClip`, `GsMulCoord2`,
`gte_set_lc`, `gte_read_lc`, `gte_rotate_z_matrix`, `gte_init`, `Gssub_make_matrix`,
`scale_view_param`, `len_param`) como `static`; `-Wall -Wextra -Werror` limpio en
`native-headless`, `native-asan` y `web`; sin cambios en `src/`/`include/`.

**Never:** funciones de `libgs` que el Juego no usa (`GsSortObject4` sin J, niebla `*FG`,
`GsDOBJ3/5`, `GsSortLine`, BG/mapas); comandos GTE nuevos en el Backend (el asm solo usa
`rtps`, `rtpt`, `nclip`, `avsz3`, `avsz4`, `nccs`, `ncct`, ya implementados); presentación o
ventana; tocar el Renderer salvo para leer VRAM en tests.

**Decisiones (autónomas):**
- Alcance: las 35 de `docs/research/psyq-usage.md` § libgs (las 33 que faltan) + las 27
  `GsTMD*` de § libgte. `GsFCALL4`, `GsWSMATRIX`, `GsLIGHTWSMATRIX` y `GsIDMATRIX` pasan de
  `port/psyq/libgs_data.c` a `libgs.c` con sus tipos de `libgs.h` y los mismos tamaños; el resto
  del estado interno de `libgs` (doble buffer, `GsORIGIN`, luces, `POSITION`…) queda `static`.
- Las `GsTMD*` solo las llama `GsSortObject4J` por `GsFCALL4`: no llevan `clock_psyq_entry()`.
- `GsLinkObject4` y `GsMapModelingData` guardan direcciones del port (punteros de 32 bits); se
  conserva lo que hace el asm con el bit de "ya mapeado". `[hypothesis]` que los TMD del disco
  llegan sin mapear; queda anotado en `libgs.c`.
- Esperados de los tests: campos clave de cada paquete (XY de pantalla, índice de OT, color,
  UV/CLUT/tpage, tag) calculados a mano en el test a partir del algoritmo de psx-spx/asm, más un
  SHA-256 de paquetes y OT por caso fijado en la primera corrida verde (regresión).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| TMD sintético | TMD con una primitiva de cada tipo que registra `initializeGsTMDMap` (F3/F4/G3/G4/NF4, T*, TN*), `GsMapModelingData` + `GsLinkObject4` + `GsSortObject4J` con luz y sin luz | paquetes y OT con campos a mano + SHA-256 fijo | N/A |
| Culling y clip | cara trasera (NCLIP ≤ 0), vértice detrás del plano cercano (FLAG bit 31) | no genera paquete; puntero de paquetes intacto | N/A |
| Subdivisión | primitiva con `GsDivMODE_DIV` (`attribute` del objeto) en una `GsTMDdiv*` | número de sub-polígonos y XY de esquinas según el asm | N/A |
| OT | `GsClearOt`, `GsSortOt` de una OT hija en la padre, `shift` ≠ 0 | enlaces y orden como el asm | N/A |
| 2D | `GsSortSprite` (escala/rotación), `GsSortFastSprite`, `GsSortBoxFill`, `GsSortPoly` | paquete idéntico al calculado | N/A |
| Matrices | `GsInitCoordinate2` + `GsGetLw`/`GsGetLs`/`GsGetLws`/`GsMulCoord0/3`, `GsSetRefView2`, `GsSetView2`, `GsSetFlatLight`, `GsSetLightMatrix`, `GsSetAmbient`, `GsSetProjection` | matrices y registros GTE calculados a mano | N/A |
| Doble buffer | `GsInitGraph` + `GsDefDispBuff` + 2 frames de `GsSortClear`/`GsDrawOt`/`GsSwapDispBuff` | `GsGetActiveBuff` alterna; cada frame pinta su buffer en VRAM y el display muestra el otro | N/A |
| Utilidades | `GsGetTimInfo`, `GsSetOrign`, `GsSetNearClip`, `GsSetLightMode` | campos/estado iguales al asm | N/A |

</frozen-after-approval>

## Code Map

- `asm/us/main/psyq/libgs.s` (35 de `libgs` + internas; `GsSortObject4J` en :3557, `GsLinkObject4` :3434) y `asm/us/main/psyq/libgte.s` (las 27 `GsTMD*`) -- fuente de verdad; en el checkout, no versionados.
- `reference/dw2-pc/psyq/libgs.c` -- 12 funciones (`GsInitGraph`, `GsInit3D`, `GsInitCoordinate2`, `GsSetLsMatrix`, `GsSetProjection`, `GsSetFlatLight`, `GsSetLightMode`, `GsSetAmbient`, `GsGetTimInfo`, `GsGetLs`, `GsSetRefView2` y la interna `GsGetLw`/`GsMulCoord2/3`) sobre `gte_core.h` (`Gte_Ctc2` → `gte_ctc2` del port); es Psy-Q 4.7, verificar contra el asm de DW1.
- `src/main/graphics.c:52-84` -- `initializeGsTMDMap`: qué `GsTMD*` usa el Juego y en qué casilla de `GsFCALL4`; el test lo replica.
- `external/psyq_headers/mw_lib41/include/libgs.h` -- tipos (`GsOT`, `GsOT_TAG`, `GsDOBJ2`, `GsCOORDINATE2`, `_GsFCALL`, `TMD_P_*`, `GsSPRITE`…) y prototipos; las `GsTMD*` toman `(primtop, vertop, nortop, pk, n, shift, ot, scratch)`.
- `port/psyq/gpu_types.h` + `port/psyq/tests/psyq_prototypes.c` -- patrón de tipos del port con `sizeof`/`offsetof` literales verificados contra `mw_lib41`; agregar los de `libgs.h` en un `gs_types.h` o en el mismo archivo.
- `port/psyq/libgs.c` (las 2 ya hechas), `port/psyq/libgs_data.c` (definiciones a mover), `port/psyq/libgpu.c` (`DrawOTag`, `ClearOTagR`, `PutDrawEnv`, `PutDispEnv`, `SetDefDispEnv` que llama `libgs`).
- `port/backend/gte.h` -- API del GTE; `port/psyq/libgte.c` -- estilo y `SetRotMatrix`/`SetLightMatrix`.
- `port/psyq/ps1mem.h` -- `PS1_LINK`, `PS1_LINK_PTR`, `PS1_LINK_END`.
- `port/psyq/tests/test_libgpu.c` + `CMakeLists.txt:151-155` -- patrón de test (`dw1_psyq`, SHA-256 de `port/host/pack/sha256.c`, VRAM vía `gpu.h`).
- `tools/port/psyq/gen_stubs.py` -- contador `stubs=` (hoy 112).

## Tasks & Acceptance

**Execution:**
- [x] `port/psyq/libgs.c` (+ `libgs_tmd.c` si se separa) -- 33 entradas de `libgs` y 27 `GsTMD*` con sus internas `static`; globals movidos desde `libgs_data.c`.
- [x] `port/psyq/psyq.h`, tipos de `libgs.h` y `port/psyq/tests/psyq_prototypes.c` -- prototipos, `sizeof`/`offsetof` contra `mw_lib41`.
- [x] `port/psyq/tests/test_libgs.c` -- la matriz I/O completa.
- [x] `CMakeLists.txt` -- fuentes nuevas y test `libgs`.
- [x] `docs/STATUS.md` (métrica de stubs) y `docs/implementation-artifacts/evidence/story-2-13.log`.

**Acceptance Criteria:**
- Given `native-headless` y `native-asan` sin `DW1_DISC_DIR`, when `ctest`, then todo pasa (`libgs`, `libgpu`, `frame0` incluidos) y gen_stubs reporta 60 stubs menos (52), sin ninguno de `libgs` ni `GsTMD*`.
- Given el preset `web`, when se construye, then compila sin errores.
- Given `libgs.c`/`libgs_tmd.c`, when se buscan accesos al Backend, then solo usan `backend/gte.h` y las funciones públicas de `libgpu`.
- Given L0a, when `preproc_check.sh`, then OK.
- Given `DW1_DISC_DIR` en el contenedor, when corren los tests de disco, then siguen pasando.

## Implementation Notes

- `port/psyq/libgs.c`: las 33 entradas de `libgs` y las 27 `GsTMD*` en un solo archivo. Los
  kernels comparten `tmd_fast` (proyección, NCLIP, AVSZ, color por modo) y `tmd_div`
  (subdivisión `RCpoly*` con recorte contra H/2 y la pantalla); `GsSortObject4J` despacha por
  `GsFCALL4` como el asm. Globals movidos desde `libgs_data.c` con sus tipos.
- `port/psyq/gs_types.h`: tipos de `libgs.h`; `psyq_prototypes.c` verifica `sizeof`/`offsetof`
  contra `mw_lib41`. gen_stubs: `stubs=52` (antes 112).
- Test `libgs`: las 27 entradas con campos calculados aparte + 27 SHA-256 de paquetes/OT
  (enlaces normalizados), subdivisión, OT, 2D, matrices, luces, caché entre frames, TIM y doble
  buffer en VRAM.
- Revisión: 1 high (`tmd_div` descartaba caras por FLAG bit 31, el asm no) y 4 huecos de test,
  corregidos reanudando la sesión Codex del implementador; 1 diferido (TMD del disco sin mapear).

## Spec Change Log

## Review Triage Log

| # | Hallazgo | Veredicto | Evidencia / ruta |
|---|---|---|---|
| 1 | `tmd_div` descarta la cara con FLAG bit 31; el asm sigue hasta `RCpoly*` (blind, edge claim) | high | Verificado en `GsTMDdivTF3NL`/`TF4L`/`TNG4` (libgte.s): solo `RotAverageNclip*` + `blez`; el FLAG no se lee y `RCpoly*` recorta contra H/2. Las caras que cruzan el plano cercano desaparecían. **patch**: quitar el descarte + test. |
| 2 | Índice de OT fuera de la tabla si `z < offset` (blind, edge) | low | El asm calcula `subu`/`srlv`/`sll` sin límites, igual que el port; el Juego pasa OT válidas. Arreglo = guardas nuevas. Rechazado. |
| 3 | Sin límite del área de paquetes, incluida la subdivisión (blind, edge) | low | El asm tampoco lo comprueba; el área la dimensiona el Juego. Rechazado. |
| 4 | Contador de grupo truncado a 16 bits con > 65 535 primitivas (blind, edge) | low | El asm de `GsLinkObject4` también guarda el contador con `sh`; ningún TMD real se acerca. Rechazado. |
| 5 | Hipótesis de TMD sin mapear no validada con el disco (blind) | maybe-false | Si un TMD del disco llegara mapeado, las direcciones PS1 apuntarían fuera del port (medium). Se resuelve leyendo el bit 0 de los TMD del Pack. **defer**. |
| 6 | La subdivisión a dos niveles no fija paquetes completos (blind) | low | Cubre tamaño, último enlace y esquinas; el nivel 1 sí se verifica campo a campo. Arreglo = más tests. Rechazado. |
| 7 | Caché de coordenadas sin cambio de frame (blind, verification gap) | medium | Pre-verificado (gap): `matrices()` no llama a `GsSwapDispBuff`. **patch**: test de dos frames con ancestro invalidado. |
| 8 | Sprite rotado sin pivote (blind, verification gap) | medium | Pre-verificado (gap): el caso rotado usa `mx=my=0`; `renderGameClock` usa (3,13). **patch**: test con pivote. |
| 9 | Los hashes se pueden regenerar con `--record` (blind) | low | Los campos clave de cada paquete se calculan aparte; el hash es solo regresión, como decidió la spec. Rechazado. |
| 10 | Excepción de doble cara de `tmd_div` sin test (blind) | medium | Rama real sin aserción. **patch**: cara trasera con y sin el flag. |
| 11 | `GsSetLightMatrix` solo probado con identidad (verification gap) | medium | Pre-verificado (gap). **patch**: hueso rotado + luz asimétrica, LLM y color del paquete. |
| 12 | `GsSortOt` con ciclo o con la OT destino igual a la fuente (edge) | low | El asm recorre igual sin detección; el Juego no lo hace. Rechazado. |
| 13 | `GsClearOt` con `length` ≥ 31 (edge) | false | El Juego usa longitudes pequeñas fijas y el asm desplaza igual; sin consecuencia alcanzable. |
| 14 | Coordenada con el centinela `SCREEN` (edge) | false | `grep` en `src/`/`include/`: el Juego nunca usa `SCREEN` como coordenada. |
| 15 | Jerarquía > 101 coordenadas con `NDEBUG` (edge) | low | `PORT_ASSERT` registra en release por diseño (`port.h`); las jerarquías del Juego son de decenas. Rechazado. |
| 16 | Grupo de longitud 0 o mayor que el restante con `NDEBUG` (edge) | low | Solo con un TMD corrupto; en debug aborta. Rechazado. |
| 17 | Entrada de despacho nula con `NDEBUG` (edge) | low | En la PS1 también saltaría a 0; en debug aborta. Rechazado. |
| 18 | `scratch[0]` fuera de 1..7 en un kernel div (edge) | false | `GsSortObject4J` lo escribe desde `(attribute>>9)&7` y solo despacha a `div` cuando es ≠ 0. |
| 19 | Índices de vértice/normal fuera del TMD (edge) | low | El asm no valida índices; TMD corrupto. Rechazado. |
| 20 | Componente de cámara `INT32_MIN` en `scale_view_param` (edge) | low | Coordenadas de cámara del Juego muy lejos de ese valor. Rechazado. |
| 21 | `calloc` nulo en `tables()` del test (edge) | low | Solo en el test, con tablas pequeñas. Rechazado. |

## Verification

**Commands:**
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'for p in native-headless native-asan; do cmake --preset $p && cmake --build --preset $p && env -u DW1_DISC_DIR ctest --preset $p; done'` -- expected: verde
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh -c 'cmake --preset web && cmake --build --preset web'` -- expected: exit 0
- `DOCKER_CONTEXT=default tools/port/docker_run.sh sh tools/port/ci/preproc_check.sh` -- expected: OK
