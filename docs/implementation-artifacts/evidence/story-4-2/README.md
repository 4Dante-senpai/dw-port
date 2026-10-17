# Historia 4.2: evidencia

Verificación realizada el 2026-10-17 dentro del contenedor, con Emscripten y
Playwright 1.64.0 instalados. Sin commits ni cambios en `src/`.

| Verificación | Resultado |
|---|---|
| Configurar y compilar `web`, `native-headless`, `native-asan` | Correcto |
| `ctest --preset web` sin disco | 16 pasan; 3 saltan con 77 |
| `ctest --preset native-headless` sin disco | 36 pasan; 4 saltan con 77 |
| `ctest --preset native-asan` sin disco | 34 pasan; 4 saltan con 77 |
| CTest `web_scene_title` con disco, incluidos sus fixtures | 3 pasan |
| CTest privado de ASan: Pack, tablas GTE, título y overlay | 4 pasan |
| `web_boot`, Chromium y Firefox | 8 casos pasan |
| `web_io`, Chromium y Firefox | 10 casos pasan |
| Título, Chromium y Firefox, OPFS normal y envenenado | 4 casos pasan |

`checks.json` registra comandos, nombres de pruebas y hashes. Los JSON por
navegador registran el hash del título y los contadores de E/S. Cada navegador
completó 96 pedidos con 96 esperas; cada lector verificó sus bytes contra la
fórmula sintética independiente. Se demoró 5 ms cada respuesta del worker para
comprobar el bloqueo, con límite de dos esperas por pedido. Los casos negativos
cubren Pack ausente, header corrupto, Manifest ajeno, índice corrupto y rangos
inválidos (incluido un offset de 64 bits fuera de rango).

Las cuatro capturas web coinciden con las dos nativas normales y las dos de ASan:
`14d5bb083e6cef60e669d0bf533560174a2efd7dc419db3657e324f12930e7e7`.
Esto verifica paridad con el port nativo; la comparación con consola sigue
correspondiendo a la épica 3.

Durante el desarrollo se observó un bloqueo del fixture de escritura en Firefox.
La versión verificada prepara OPFS desde una página vacía y un worker servido
desde el mismo origen, cierra el escritor y luego navega al runtime del juego.
El fixture transfiere por streaming; nunca incorpora el Pack al heap wasm.

Los Packs, PPM y logs privados quedan exclusivamente en `build/`. Esta carpeta
contiene solo resultados de pruebas, hashes y documentación; ningún byte del disco.

Revisión de los dos hallazgos: se eliminó el mutex nativo conservando la costura
posicional. El getter envenenado registra cada acceso de forma síncrona en el
hilo principal, junto al identificador del pthread. El control positivo
`?io-test=guard&poison=1` toca `navigator.storage` una vez desde cada lector;
los JSON `io-guard-*.json` muestran cuatro accesos desde cuatro pthreads distintos.
Los títulos y las lecturas normales registran cero accesos medidos.

En esta revisión se recompilaron únicamente `test_pack` nativo y `dw1` web,
y se ejecutaron solo las pruebas afectadas:

- `ctest --test-dir build/port/native-headless -R '^pack$' --output-on-failure`: 1 pasa.
- `python3 -I -B port/web/tests/run.py build/port/web io`: 12 pasan.
- `DW1_DISC_DIR=/dw/game_rom/extracted python3 -I -B port/web/tests/run.py build/port/web title`: 4 pasan; hashes iguales al nativo.

No se repitieron las suites completas en esta revisión.
