# Runner de Escenas

El runner conserva dos corridas y exige capturas P6 y trazas de reloj idénticas:

```sh
python3 -I -B tools/port/scenes/run.py title --build native-headless
python3 -I -B tools/port/scenes/run.py title --build native-asan --compare
```

`--compare` exige `reference_sha256` en el TOML. Ese hash identifica el archivo PPM completo
producido por `tools/port/reference/capture.py`; la comparación de píxeles usa cada canal
`>> 3` (RGB555). La referencia local debe corresponder al hash fijado y tener las mismas
dimensiones. Se busca en `build/reference/<id>/run-1.ppm` o
`$DW1_REFERENCE_DIR/<id>/run-1.ppm`. Si falta y el hash del port difiere, falla indicando
ambos hashes y cómo capturar la referencia; si los hashes coinciden, basta esa evidencia.

Excepciones opcionales (el título no tiene ninguna):

```toml
[[exceptions]]
region = [10, 20, 30, 40]
reason = "Motivo verificado de esta diferencia"
```

La región incluye ambos extremos `[x0, y0, x1, y1]`. Debe estar dentro del frame de referencia
y tener un motivo no vacío; se valida antes de ejecutar el juego. Sin referencia local no
se pueden validar regiones. Las regiones solapadas asignan el píxel a la primera declarada.
El informe lista cada región, motivo y cantidad descontada. Diferencias fuera de ellas salen
con 1 e imprimen `N pixels differ, box x0..x1 y0..y1`. Las diferencias se resaltan en magenta
sobre negro en `diff.ppm`, junto a las capturas bajo `build/port/<preset>/scenes/<id>/`;
se elimina el diff anterior al iniciar una comparación. No se suben imágenes a Git ni a CI.

`scene_title_compare` es clase B (77 sin disco); su fixture `reference_title` genera primero
la referencia. `native-disc` ejecuta ambos. `scene_title` conserva la verificación de
determinismo sin comparar. Los tests sintéticos del runner no necesitan disco.
