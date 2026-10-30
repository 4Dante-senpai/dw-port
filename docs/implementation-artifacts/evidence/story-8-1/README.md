# Evidencia de la Historia 8.1

Verificación del 2026-10-30, en el contenedor del proyecto y con el disco, sobre el commit
`a993286` de la rama de la historia. Lo implementó una sesión cloud sin disco, sin `asm/` y sin
`cc_mips.dll`; esta corrida cubre lo que esa sesión no pudo verificar.

| Comando | Resultado |
|---|---|
| `DW_IMAGE=dw-build:latest tools/port/remote_run.sh sh -c 'make -j20 && make compare'` | exit 0, `Compiled binaries match with target binaries` |
| `ctest --preset native-headless -R status_table` | 2/2 |
| `python3 -m unittest tools/port/metrics/test_status_table.py` | 18 OK |
| `python3 tools/port/metrics/status_table.py` | 2741 / 2741 funciones, PsyQ 187 / 202, `DW1_PORT` 117, filas 117 |
| `ctest --preset native -R '^scene_title' --output-junit` | 6/6 |
| `ctest --preset web -R '^web_scene_' --output-junit` | 3/3 |
| `status_table.py --junit <nativo> --junit <web>` | nativo 2 / 10 (`title`, `title_audio`), web 1 / 10 (`title`) |

El denominador de 10 Escenas viene de `tools/port/scenes/*.toml`. Los filtros de `ctest` limitaron
la corrida a las Escenas de título, así que esos conteos prueban el parseo de JUnit y no son el
estado del proyecto. Por eso `docs/STATUS.md` sigue marcando las Escenas como `sin verificar`
hasta que corra la suite completa.
