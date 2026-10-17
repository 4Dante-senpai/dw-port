# Entorno de testing en la LAN

Historia 8.5: despliegue de desarrollo previo a 3.1. No es el deploy de la Épica 7.
nginx está fijado por versión y digest. La imagen contiene solo el programa; el Pack privado reside en el volumen
`dw1-testing-pack`, montado en `/pack` de solo lectura. No se publican artifacts.

En GitLab, proteger `main` y configurar `HUB_DOCKER_HOST` como variable Protected +
Masked, con el mismo valor que el template del runner. `DW1_TESTING_PORT` también
es Protected; si falta, se usa 8090. Los valores reales se documentan únicamente
en `docs/port/local-infra.md`, ignorado por git. El runner `dw1-data` debe alcanzar
la API Docker de ese host y tener el disco en `/private/disc`.

Cada push a la rama por defecto protegida ejecuta `testing`: construye nativo y
web, valida el título local, construye nginx directamente en el daemon destino,
sin registry, y sincroniza el Pack por la API mediante un auxiliar Alpine. Solo
copia el Pack si su hash difiere. El CLI Docker 27.3.1 se descarga y verifica por
SHA-256 antes de usarlo. Los jobs de despliegue y rollback comparten
`resource_group: dw1-testing` para evitar concurrencia.

Abrir `http://<deploy-host>:<port>/` en Chromium iniciado con:

```sh
chromium --unsafely-treat-insecure-origin-as-secure=http://<deploy-host>:<port>
```

En Firefox, añadir `http://<deploy-host>:<port>` a
`dom.securecontext.allowlist` en `about:config`. El origen debe ser confiable para
OPFS y SharedArrayBuffer; nginx añade COOP/COEP a todas las respuestas. Sin
aislamiento aparece `cross-origin isolation required`.

La página muestra el commit, el estado del Pack y los botones «Cargar Pack» y
«Jugar». La copia usa streaming y muestra bytes de progreso; solo al terminar se
escribe el sidecar `dw1/us.pack.sha256` en OPFS. Un sidecar distinto al servido
marca el Pack como desactualizado y requiere volver a copiarlo. «Jugar» abre
`/?pack=1&frames=300`: corre 300 frames sin ritmo y deja el último en pantalla (el título). Una corrida sin fin queda en blanco: el `OffscreenCanvas` del hilo del juego solo muestra un frame cuando ese hilo vuelve a su bucle de eventos, y hoy no vuelve hasta terminar. No incluye selector de disco, audio ni controles.

Después del healthcheck, Playwright Chromium valida contra la URL desplegada
el commit y el SHA-256 del título nativo, con el origen marcado como seguro.
Si falla la salud o la prueba remota, el job falla y restaura la imagen anterior
si existe. En el primer deploy no existe una imagen para restaurar.

Para rollback manual, ejecutar el job `testing-rollback` en la rama protegida.
También se puede ejecutar desde un entorno con Docker CLI:

```sh
# HUB_DOCKER_HOST y DW1_TESTING_PORT se suministran en el entorno privado.
tools/port/testing/deploy.sh --rollback
```

Se relanza `dw1-testing:previous`, se espera salud y se actualiza `current`.
Falla si no hay imagen anterior. El volumen es compartido: el rollback restaura
la imagen, conserva el Pack sincronizado y no restaura versiones del volumen.
No exponer este servicio fuera de la LAN.

## Comprobación local

```sh
cmake --preset native-headless && cmake --build --preset native-headless
cmake --preset web && cmake --build --preset web
DW1_DISC_DIR=<disc-dir> ctest --preset web
bash -n tools/port/testing/deploy.sh tools/port/testing/install-docker.sh
python3 -B tools/port/testing/test_context.py
python3 -B tools/port/testing/test_deploy.py
```

Para una URL existente, `DW1_WEB_URL=http://<deploy-host>:<port>` evita crear el
servidor local en `run.py`. `DW1_WEB_INSECURE_ORIGIN` pasa ese origen a Chromium.
Con esa variable se usa Chromium completo en modo headless: el headless-shell
fijado no habilita OPFS para el origen HTTP aunque reciba la flag.
La captura nativa debe existir en
`build/port/native-headless/scenes/title/run-1.ppm`.

El Manifest actual tiene seis columnas (el SHA-256 es la última); el chequeo
usa esa última columna. La referencia a columna 7 en el mapa de la spec no
coincide con el archivo real. El chequeo rechaza headers `DW1PACK`, hashes de
archivos del Manifest y enlaces simbólicos; solo imprime el resultado.
