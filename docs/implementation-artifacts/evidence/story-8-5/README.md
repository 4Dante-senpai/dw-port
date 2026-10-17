# Evidencia local — historia 8.5

Fecha: 2026-10-17. Implementación sin commit. No se incluyen bytes del disco,
capturas, IPs ni credenciales. La historia sigue pendiente de validación en el
homelab: esta evidencia no representa un pipeline de GitLab.

## Comprobaciones ejecutadas dentro del contenedor

- `cmake --preset native-headless` y `cmake --build --preset native-headless`: OK.
- `cmake --preset web` y `cmake --build --preset web`: OK.
- `DW1_DISC_DIR=<disc-dir> ctest --preset web`: 21/21, sin omisiones. Incluye
  `web_boot`, `web_io`, `web_launcher`, `testing_context`, `pack_disc`,
  `native_title_capture` y `web_scene_title`. Esta ejecución se inició antes de
  registrar `testing_deploy`; ese test se ejecutó por separado.
- `ctest --preset native-headless`: 36 aprobados y 4 omitidos por no suministrar
  `DW1_DISC_DIR` en esa ejecución; 0 fallos.
- Pruebas finales `web_launcher` y `testing_deploy`: 2/2. Lanzador: cuatro casos
  en total entre Chromium y Firefox; ausencia, copia, persistencia, versión vieja,
  navegación a Jugar e invalidación del sidecar ante un reemplazo fallido.
- `testing_deploy`: doble de Docker aislado; primer deploy, omisión de copia por
  hash igual, montaje de solo lectura, rollback sin imagen previa, redeploy,
  restauración por fallo de salud y por fallo de arranque, rollback manual.
  Comprueba que la salida no contiene el endpoint privado ni cabeceras del Pack.
- Contexto real armado con los seis archivos web, versión, Dockerfile y config:
  limpio. Archivo sintético con cabecera `DW1PACK`: rechazado. También se prueba
  contaminación por hash del Manifest y por enlace simbólico.
- Docker CLI 27.3.1: descarga real y verificación SHA-256 correctas. Índice nginx
  1.28.0-alpine consultado en el registry oficial y fijado por digest.
- `bash -n`, sintaxis Python/JavaScript, YAML de CI y sintaxis shell de ambos jobs:
  OK. `git diff --check`: OK. Archivos versionados: sin direcciones IPv4 de LAN.
- `DW1_WEB_URL` contra un servidor HTTP separado, en una interfaz distinta de
  loopback dentro del contenedor, sin `DW1_DISC_DIR`: Chromium completo, 2/2
  títulos iguales al nativo; commit servido verificado. Ejercita la flag y la
  ruta de `run.py` que no crea servidor local. No es nginx ni el host del homelab.

## Hallazgos corregidos durante las pruebas

- Botón de copia habilitado antes de terminar la inicialización: ahora permanece
  deshabilitado hasta tener el hash remoto y haber consultado OPFS.
- El headless-shell fijado aceptaba aislamiento pero no habilitaba el contexto
  seguro ni OPFS para HTTP con la flag. Chromium completo sí lo hace; el modo
  remoto usa `channel: chromium`. La repetición del título remoto pasó.
- El Manifest real tiene seis columnas; el hash está en la última. El chequeo
  usa esa columna, pese a que el mapa de la spec menciona columna 7.

## Pendiente y límites

No hay socket Docker ni `HUB_DOCKER_HOST` configurado en este contenedor. No se
pudieron construir/ejecutar la imagen nginx ni comprobar sus respuestas HTTP con
un daemon real. El pipeline de `main`, el destino LAN, la sincronización real del
volumen, el healthcheck real y el rollback real siguen sin verificarse. Los
escenarios del script se verificaron con el doble de Docker.

El rollback restaura la imagen anterior; conserva el volumen del Pack actual.
No se hicieron commits, push ni deploy público. No se ejecutó el build matching:
no hubo cambios en `src/` ni `include/` y esta tarea no realiza un merge.

## Tras la revisión (orquestador)

- Patches de la revisión: `previous` solo se actualiza si cambia el ID de imagen; el doble de
  Docker modela IDs y verifica la imagen restaurada; el job `testing` corre también
  `web_launcher`, `testing_deploy` y `testing_context`.
- `DW1_DISC_DIR=<disc-dir> ctest --preset web` en la imagen `dw-port`: 22/22 aprobados, 0 omitidos
  (incluye `pack_disc`, `native_title_capture`, `web_scene_title`, `web_launcher`,
  `testing_deploy`, `testing_context`).
