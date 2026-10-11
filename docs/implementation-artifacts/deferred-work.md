# Trabajo diferido

- source_spec: `docs/implementation-artifacts/spec-2-1-esqueleto-del-build-del-port.md`
  summary: Fijar `SOURCE_DATE_EPOCH` en el build del port (AD-17) junto con el check de `.wasm` reproducible.
  evidence: AD-17 lo exige y no está en `CMakeLists.txt`; hoy ningún TU usa `__DATE__`/`__TIME__`, así que se verifica con NFR-7 en la Historia 4.10.
