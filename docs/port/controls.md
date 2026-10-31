# Controles nativos (historia 3.9)

El preset `native` usa el mismo juego y renderer software que `native-headless`.
La ventana inicial mide 640×480; presenta el display original con filtro nearest
y barras para conservar su proporción. No hay remapeo ni configuración persistente.

| PlayStation | Teclado | SDL_Gamepad |
|---|---|---|
| D-pad | Flechas o W/A/S/D | Cruceta |
| Cross | K | South |
| Circle | L | East |
| Square | J | West |
| Triangle | I | North |
| Start | Enter | Start |
| Select | Backspace | Back |
| L1 / R1 | Q / E | Hombros izquierdo / derecho |
| L2 / R2 | 1 / 3 | Gatillos izquierdo / derecho (>50 %) |

Se usa el primer gamepad disponible, con conexión y desconexión en caliente.
Los botones de teclado y gamepad se combinan. El teclado requiere foco en la
ventana. El host publica la máscara de botones y el hilo del juego la aplica
una vez por VBlank mediante `port_pad_latch`. Con `--inputs` manda la escena,
incluso si su archivo está vacío. `--headless` desactiva ventana, audio e input
en vivo sin cambiar el parser ni la ruta de captura.

El audio usa PCM estéreo s16 del SPU a 44.100 Hz, un ring SPSC de 8192 frames
y remuestreo lineal en C puro. Espera 2208 frames al iniciar y tras un underrun;
la salida se desvanece hacia silencio si el productor se detiene. El controlador
proporcional tiene una banda muerta de 384 frames, ganancia 5·10⁻⁵ por frame de
error y un ajuste máximo de ±2 %.
Solo cambia el consumo del host: nunca el reloj virtual ni la generación del SPU.

La frecuencia declarada del dispositivo se convierte directamente (por ejemplo,
47.520, 48.000 o 48.480 Hz). Un error **no declarado** del reloj del dispositivo
se compensa con el ratio hasta ±2 %. `test_audio_rate` lo verifica al 99 % y al
101 % durante 600,6 s simulados: 0 underruns y el nivel dentro de la banda.

## Navegador (historia 4.5)

El build `web` usa el mismo mapeo por defecto. El teclado se identifica por
`KeyboardEvent.code` (posición física, igual con cualquier distribución) y los
mandos por el mapeo estándar W3C de la Gamepad API.

| PlayStation | Bit de cable | Teclado (`code`) | Gamepad estándar (índice) |
|---|---|---|---|
| Select | 0 | `Backspace` | 8 (Back) |
| Start | 3 | `Enter` | 9 (Start) |
| Arriba | 4 | `ArrowUp` o `KeyW` | 12 |
| Derecha | 5 | `ArrowRight` o `KeyD` | 15 |
| Abajo | 6 | `ArrowDown` o `KeyS` | 13 |
| Izquierda | 7 | `ArrowLeft` o `KeyA` | 14 |
| L2 | 8 | `Digit1` | 6 (gatillo, valor > 0,5) |
| R2 | 9 | `Digit3` | 7 (gatillo, valor > 0,5) |
| L1 | 10 | `KeyQ` | 4 |
| R1 | 11 | `KeyE` | 5 |
| Triangle | 12 | `KeyI` | 3 (North) |
| Circle | 13 | `KeyL` | 1 (East) |
| Cross | 14 | `KeyK` | 0 (South) |
| Square | 15 | `KeyJ` | 2 (West) |

Se combinan el teclado y todos los mandos conectados con `mapping === 'standard'`;
los mandos sin mapeo estándar se ignoran (en `native`, SDL usa solo el primer mando
con mapeo de `SDL_Gamepad`). Las teclas con Ctrl, Alt o Meta, las que van a un control
enfocado de la Shell (por ejemplo, Enter sobre «Pausa») y las posteriores al fin de la
corrida no se envían al juego. Con «Stick izquierdo como
cruceta» (Opciones, desactivada por defecto) el eje izquierdo fuera de la zona muerta de 0,5 actúa como la cruceta. El hilo
principal publica la máscara en cada evento de teclado y en cada
`requestAnimationFrame`; el hilo del juego la aplica con `port_pad_latch` en cada
VBlank (`port/host/web/input.c`). Perder el foco suelta las teclas. Conectar o
desconectar mandos no pausa el juego. Las corridas de captura de Escena no aplican
input en vivo, igual que `native-headless` (el preset `native` con ventana sí lo
aplica salvo con `--inputs` o `--headless`). Un VBlank en que PsyQ no consulta el pad
(`pad_poll_missed`) conserva el paquete anterior y no cuenta como muestra consumida.

## Remapeo y `PortSettings` (historia 4.6)

En Opciones, «Reasignar» toma la próxima tecla (`KeyboardEvent.code`) o el próximo
botón de un gamepad estándar (índice W3C, pulsado con `value > 0,5`, igual que los
gatillos) y la asigna a ese botón PlayStation; reemplaza las asignaciones previas de
ese dispositivo para ese botón y la quita de cualquier otro botón. `Escape` cancela.
Los botones ya pulsados al empezar no cuentan; `Tab` y los atajos con Ctrl, Alt o Meta
siguen siendo del navegador. «Restaurar valores por defecto»
vuelve a la tabla de arriba y desmarca el stick. El remapeo se hace en el lanzador,
antes de «Jugar».

Se guarda en IndexedDB (base `dw1`, almacén `settings`, clave `port_settings`) al
instante, como JSON con claves `snake_case`:

```json
{"schema_version": 3, "stick_dpad": false, "volume": 1,
 "controls": {"keyboard": {"cross": ["KeyK"], "up": ["ArrowUp", "KeyW"]},
              "gamepad": {"cross": [0], "l2": [6]}}}
```

Botones: `select`, `start`, `up`, `right`, `down`, `left`, `l2`, `r2`, `l1`, `r1`,
`triangle`, `circle`, `cross`, `square`. La versión 1 (`{"schema_version": 1,
"stick_dpad": bool, "keyboard": {"KeyK": "cross"}}`) **no se publicó**: se definió
junto con la 2 como formato anterior para la cadena de migraciones y su test. Cada
migración `n → n + 1` conserva los campos que no conoce. Un registro de una versión
más nueva, ilegible o con forma inválida no se sobrescribe nunca: la Shell avisa y
usa los valores por defecto en memoria (los cambios de esa sesión no se guardan).


## Volumen (historia 4.7)

El volumen general va de 0 a 100 % en Opciones y pasa a la pantalla del juego al
arrancar. El cambio se oye en vivo mediante un `GainNode`; se guarda al soltar el
control. El control pierde foco después de cambiar para dejar las teclas al juego.
0 % silencia la salida y sigue consumiendo PCM. Restaurar controles conserva el
volumen. `PortSettings` v3 agrega `volume` numérico y finito en [0, 1]; la migración
v2 → v3 agrega 1 y conserva controles y campos desconocidos. Un volumen inválido
protege el registro completo, usa valores por defecto y muestra un aviso.

«Jugar» habilita el audio con un gesto. Pausa manual y pestaña oculta suspenden el
contexto y conservan el anillo; reanudar continúa sin contar underruns por la pausa.
Un fallo al crear el contexto o cargar el worklet muestra un aviso y permite jugar
sin audio.
