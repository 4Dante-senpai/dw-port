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
