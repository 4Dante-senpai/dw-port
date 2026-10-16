# Trabajo diferido

- source_spec: `docs/implementation-artifacts/spec-2-1-esqueleto-del-build-del-port.md`
  summary: Fijar `SOURCE_DATE_EPOCH` en el build del port (AD-17) junto con el check de `.wasm` reproducible.
  evidence: AD-17 lo exige y no está en `CMakeLists.txt`; hoy ningún TU usa `__DATE__`/`__TIME__`, así que se verifica con NFR-7 en la Historia 4.10.

- source_spec: `docs/implementation-artifacts/spec-2-6-linkeo-y-frame-0.md`
  summary: `rand`/`srand` con la semántica de PsyQ (RAND_MAX 0x7FFF) en vez de la libc del host.
  evidence: `randomLimit` (`src/main/math.c`) hace `(limit * rand()) >> 15`; con glibc el resultado sale de rango. Historia 2.9.
- source_spec: `docs/implementation-artifacts/spec-2-6-linkeo-y-frame-0.md`
  summary: Stubs que devuelven NULL para funciones que reservan memoria o devuelven punteros (`malloc3`, `CdSearchFile`).
  evidence: el Juego recibe NULL sin aviso; se resuelve con `malloc3` real (2.9) y stubs con comportamiento definido (2.14).
- source_spec: `docs/implementation-artifacts/spec-2-6-linkeo-y-frame-0.md`
  summary: Inventario de llamadas por puntero con tipo distinto antes del build web.
  evidence: UBSan `-fsanitize=function` en `world_object.c:101`; wasm `call_indirect` aborta. Historia 4.1.

- source_spec: `docs/implementation-artifacts/spec-2-7-manifest-y-herramienta-nativa-del-pack.md`
  summary: Test del hook pre-commit y del `.gitignore` para todos los patrones prohibidos (incluidos `*.pack` y `*.pack.tmp`).
  evidence: ningún test corre `tools/port/hooks/pre-commit`; quitar `|pack` del regex o volver a `pack/` en `.gitignore` deja todo verde.
- source_spec: `docs/implementation-artifacts/spec-2-7-manifest-y-herramienta-nativa-del-pack.md`
  summary: Confirmar que el SHA-256 de las filas `F2` (2336 B con EDC de Form 2) acepta dumps de otras herramientas.
  evidence: medium sin verificar; se resuelve validando un segundo dump (otra herramienta) de un disco USA contra `us.tsv`.
- source_spec: `docs/implementation-artifacts/spec-2-7-manifest-y-herramienta-nativa-del-pack.md`
  summary: Verificar si el Juego lee archivos `F1` en `CdlModeSize1` (2340 B), que el Pack no puede servir sin subheader ni EDC/ECC.
  evidence: medium sin verificar; Historia 2.8 (trazar los `CdControl(CdlSetmode)` del Juego).
- source_spec: `docs/implementation-artifacts/spec-2-12-renderer-software-y-libgpu.md`
  summary: Verificar contra el Emulador de referencia el orden de copia de filas de GP0 80 (`MoveImage`) con solape, incluida la envoltura 511→0.
  evidence: `gpu_soft.c` elige el orden por `dy > sy` (semántica memmove); si el hardware copia siempre de arriba hacia abajo, las copias solapadas hacia abajo difieren. Se resuelve con un caso sintético en DuckStation (Historia 3.1).
