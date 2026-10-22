# Memory card nativa (3.8)

`port/psyq/libmcrd.c` traduce el asm USA `asm/us/main/psyq/libmcrd.s`:
comandos pendientes, pila UserFunc de cuatro niveles, reintentos, eventos,
callback en la ranura VSync 7 y consumo único del resultado por `MemCardSync`.
Cada función traducida identifica su origen con `asm:`; algunos helpers
comparten las variantes de lectura/escritura o eventos. El borrador m2c se
generó como evidencia local, sin incorporarlo al código distribuido.

`bios_card.c` adapta el dispositivo `bu`, asignación de bloques, directorio,
tabla de sectores rotos y eventos SwCARD/HwCARD de OpenBIOS (MIT). Origen:
PCSX-Redux `2068784a552b598940191e8431585b7f96408b3f`, submódulo `src/mips`
`361f3f6a6f8d8bb98548900d8a231e2d2a2c1dc4`, archivos
`openbios/card/{backupunit,device}.c`, `openbios/sio0/card.c` y
`openbios/kernel/events.c`. La licencia se conserva en el código adaptado.
No se incorpora código GPL de `src/core/`. Las APIs de archivos se llaman
`bios_bu_*` para evitar interceptar el `open/read/write` del Host.

No hay driver SIO0 ni parches en RAM. `_patch_card`, `_patch_card2`,
`_copy_memcard_patch`, `_dsr_start`, `_check_dsr` y `_patch_wait_start` son
operaciones sin efecto: sus modificaciones de instrucciones MIPS no aplican
al port compilado. `InitCARD/StartCARD/StopCARD`, `_bu_init` y los trampolines
de tarjeta se resuelven en la capa BIOS del port.

## Imagen y persistencia

`card_image.c` mantiene dos imágenes de 131.072 bytes: 1.024 sectores de 128
bytes y 15 bloques de datos de 8.192 bytes. El formato sigue Memory Card Data
Format de psx-spx: cabecera MC, directorio, tabla de sectores rotos y XOR de
sectores. La imagen nueva se contrastó byte a byte con una tarjeta que creó
el Redux fijado al abrir una ruta inexistente, sin arrancar ningún disco.
SHA-256: `78b6d4ac9ab4d23caf7e5f04f83539bf5d994cccfb0a709d14ac53d05c8e21ef`.
El test L1 fija ese hash; no se distribuye la imagen.

El ejecutable acepta `--card1 /ruta/slot1.mcd` y `--card2 /ruta/slot2.mcd`.
Sin opción la ranura está ausente. Una ruta inexistente crea una tarjeta
formateada. El Host acepta imágenes crudas y VGS de 131.136 bytes, omitiendo
sus 64 bytes iniciales. Un tamaño distinto, falta de lectura o archivo no
regular produce stderr y tarjeta ausente, sin modificar el archivo ni abortar.

Al completar Create/Delete/Format/Unformat o WriteFile/WriteData se persiste
la imagen completa antes de publicar el éxito o llamar al callback. Se usa
un temporal en el mismo directorio, escritura completa, fsync, close, rename
y fsync del directorio. VGS se convierte a crudo al guardar. Un error de
commit devuelve error de tarjeta. `DW1_CARD_TEST_KILL=during-temp` o
`before-rename` mata el proceso con SIGKILL; se usa únicamente en pruebas.
Puede quedar un temporal tras SIGKILL; el archivo principal permanece íntegro.
Las tarjetas de escenas se mantienen fuera del repositorio.

## Reloj y límites

Cada sector termina mediante un evento de `clock.c`; los accesos síncronos
esperan ese evento, y los asíncronos avanzan desde callbacks. Los costos
`CLOCK_CARD_COMMAND_CYCLES=1024` y `CLOCK_CARD_SECTOR_CYCLES=33869` son
hipótesis para calibrar en 3.10. No se afirma precisión temporal SIO0.
Se modela un comando de tarjeta activo; los sectores no interpretan registros
ni bytes de un protocolo de consola.

En web las dos ranuras permanecen ausentes; no hay IndexedDB ni importación
o exportación (Épica 4). El guardado en partida mediante `openSaveMachine`
queda fuera del alcance de esta historia.

## Evidencia automática

`card` verifica el hash nuevo, ausencia, arbitraje de comandos, eventos,
Create/Write/Read/Delete/GetDirentry/Format, tarjeta llena, cadenas de bloques,
Open/Data, ambas ranuras y un payload de 0x2000 bytes. `card_host` prueba
archivos inválidos, permisos, VGS y recuperación tras SIGKILL en ambos puntos.
Web verifica ausencia y eventos, sin persistencia.

`card_newgame` y `card_continue` usan once declaraciones de estado propias
(584 valores por muestra) y cuatro checkpoints con ventana de un VBlank.
New Game escribe el save de 0x2000 bytes en la ranura 1; Continue recibe la
tarjeta final del port tanto en Redux como en el port. El runner exige
determinismo de dos corridas, igualdad del estado con Redux, igualdad de los
131.072 bytes finales y coincidencia de los valores restaurados con New Game
después de `loadSavegame`. Se observó en ambas escenas SHA-256 final
`18509839ea2e902ff1edc0c00db558d9cd2a98ec63ee13c7bf69f858263886dc`.
Los frames de estas escenas son informativos; la aceptación compara estado
y tarjeta. No se modificó `src/` ni el archivo global de variables.

La continuación de verificación del 2026-10-22 corrigió la comparación de
metadatos de audio: `_card_build` solo selecciona el directorio de tarjetas y
no invalida una referencia compartida entre nativo y ASan. Las comprobaciones
de identidad y hashes permanecen activas. `title_audio`, XA y `movie0` completo
pasan en ambos builds; también las cuatro pruebas de tarjeta por build.
ASan/UBSan se compiló con `CMAKE_C_FLAGS_DEBUG='-g -O1'`; el preset versionado
no cambió. Los logs de esta continuación están en
`build/verification/spec-3-8/` (ignorado por Git).
El resultado consolidado es 26/27 pruebas de clase B y 3/3 pruebas unitarias
adicionales de tarjeta en cada build, sin omisiones; `results.json` enumera
las 30 pruebas distintas de cada uno. El grupo de título de ASan se repitió
completo y pasó tras una captura anterior de Redux bloqueada.

Bloqueo de aceptación: `scene_movie0_skip_compare` falla en ambos builds
porque port y Redux retornan en el VBlank 122 (delta 0), mientras la decisión
congelada 7 de la historia 3.7 exige exactamente +1, incluso rechaza 0.
Los controles de retorno, píxeles, registros de audio y sincronía A/V pasan.
No se modificaron los costos ni ese gate; retirar la excepción requiere
la decisión del dueño. Por tanto, todavía no se declara verde toda la clase B.

Resolución (dueño, 2026-10-22, decisión 2 de la spec 3.8): se acepta la
igualdad exacta. `movie0_skip.toml` ya no tiene `skip_vblank_delta` y el test
exige el mismo VBlank que Redux. Con las tarjetas presentes igual que en la
referencia, la salida coincide (hipótesis no verificada: el polling de `MemCardExist` del menú cambia la fase de los VBlanks
previos al video). Los costos de `ClearImage` tras `CdlPause` y la fase de
`PadRead` siguen sin calibrar; los calibra la Historia 3.10.
