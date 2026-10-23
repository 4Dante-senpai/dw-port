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
