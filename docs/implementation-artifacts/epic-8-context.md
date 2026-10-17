# Epic 8 Context: Decomp, upstream y CI (continua)

<!-- Compilado a partir de los artefactos de planificación. Puede editarse libremente. Regenerar con compile-epic-context si cambia la planificación. -->

## Goal

Dar al proyecto evidencia automática y sostenible: métricas que se calculan solas, un pipeline de GitLab CI que separa jobs sin datos de jobs con datos del disco, un procedimiento para seguir `upstream/main` sin conflictos grandes, el registro de funciones no verificadas por objdiff y un entorno de testing en el homelab. Es una épica sin fin fijo: sus historias se retoman cuando cambian `upstream/main`, la disponibilidad de `cc_mips.dll` o el forge.

## Stories

- Story 8.1: Métricas automáticas en `STATUS.md`
- Story 8.2: Pipeline de GitLab CI con clases A y B
- Story 8.3: Merge periódico de `upstream/main`
- Story 8.4: Registro de funciones no verificadas y `make compare`
- Story 8.5: Entorno de testing en el homelab (se hace antes de la Historia 3.1)

## Requirements & Constraints

- Ningún byte con copyright (imagen del disco, archivos extraídos, Pack, ejecutables originales, `cc_mips.dll`, nada de CodeWarrior) entra al repo, a imágenes, al registry ni a los logs. Los manifests contienen solo hashes y tamaños. La clase B no sube artefactos derivados del disco: solo hashes, métricas y diffs numéricos.
- El repo de GitHub es público. Hosts y puertos reales llegan por variables de CI y viven solo en `docs/port/local-infra.md` (ignorado por git); los docs públicos usan marcadores como `<deploy-host>`.
- Los cambios en `src/` van detrás de `DW1_PORT`, son mínimos y están documentados fila por fila en `docs/port/src-changes.md`. La cantidad de bloques `DW1_PORT` debe coincidir con las filas; si no, el chequeo de métricas falla (NFR-10).
- Sin `cc_mips.dll` no se ejecuta el build PS1; las funciones nuevas se validan por comportamiento y se registran en `docs/port/non-matching.md`. Con `DW1_MWCC_DIR` disponible, `make` y `make compare` deben dar el binario idéntico.
- Un merge de `upstream/main` solo puede tocar archivos listados en `src-changes.md`; después deben seguir en verde L0a, L0b, L1 y las Escenas que pasaban.
- Push solo a `origin`; nunca a `upstream`; sin force-push ni reescritura de historia publicada sin aprobación.
- Ninguna historia se cierra sin evidencia automática (tests, logs, `make compare`).

## Technical Decisions

- **Forge (AD-16).** GitLab CI autohospedado del dueño, con runner Docker y registry propios. `origin` tiene dos push URL (GitHub y GitLab), así que cada `git push origin` llega a ambos.
- **Clase A (sin datos).** Corre en cualquier runner: L0a, L1, L3a, build web y tamaño NFR-3.
- **Clase B (con datos).** Corre solo en el runner con etiqueta `dw1-data`, en el servidor del dueño. Datos privados montados de solo lectura fuera del repo en `/private` (`DW1_DISC_DIR=/private/disc`, más `cc_mips.dll`). Incluye L0b, L2, L3b y el check NFR-4 de bytes del deploy. El runner es de instancia y no está marcado *protected* (desvío aprobado): la restricción la dan las reglas de disparo y que el proyecto es privado.
- **Disparo de la clase B.** Solo pipelines de ramas del propio proyecto (`CI_PIPELINE_SOURCE` en `push`, `web` o `schedule`); nunca merge requests desde forks. Se verifica con las `rules` y un MR de prueba.
- **Imagen.** Todo job corre dentro de la imagen `dw-port`, publicada en el registry del GitLab y fijada por digest. Toolchain solo de esa imagen para builds reproducibles.
- **Presets.** `native`, `native-headless`, `native-asan` y `web` comparten las mismas flags del juego (`-fsigned-char -fwrapv -fno-strict-aliasing -fno-common -DDW1_PORT -DLANGUAGE_C -DVERSION_US`).
- **Bytes en el deploy (AD-18).** Solo se admiten los bytes que reproduce el C upstream de `src/` e `include/`; el check usa ventanas de 64 bytes y debe fallar ante un caso de contaminación.
- **Pendiente del dueño.** Cómo llega `origin` a GitLab; mientras tanto las clases pueden correr a mano en el contenedor `dw-port` y los logs son la evidencia.
- **Story 8.5 (entorno de testing).**
  - Job de clase B en `dw1-data` que compila el preset `web`, arma el Pack desde `/private/disc` con `dw1-pack` y construye una imagen nginx (`port/web/testing/`) con COOP `same-origin`, COEP `require-corp`, MIME de wasm y la página de desarrollo.
  - Construye la imagen directamente en el daemon del host de deploy (`DOCKER_HOST=$HUB_DOCKER_HOST`, sin registry) y la despliega con healthcheck, rollback manual a la imagen anterior y `resource_group`.
  - El Pack va a un volumen Docker con nombre mediante un contenedor auxiliar (sin SSH ni rutas del host), solo si su SHA-256 difiere; se monta de solo lectura. La imagen no contiene bytes del disco (ni header del Pack ni hashes del Manifest) y el log tampoco.
  - La página copia el Pack a OPFS (`dw1/us.pack`) por streaming si falta o difiere, muestra el commit desplegado y arranca con `?pack=1`. Se prueba con el origen marcado como seguro (`unsafely-treat-insecure-origin-as-secure` en Chromium, `dom.securecontext.allowlist` en Firefox).
  - Playwright en Chromium contra la URL desplegada: la Escena `title` debe dar el mismo SHA-256 que `native-headless`; si falla el healthcheck o la prueba, se restaura la imagen anterior.

## Cross-Story Dependencies

- 8.2 es la base de 8.5 (runner `dw1-data`, imagen `dw-port`, registry) y de la ejecución de L0b en 8.4. 8.1 y 8.4 comparten la contabilidad de `src-changes.md` y `non-matching.md`; 8.3 depende de `src-changes.md` como lista de archivos permitidos en conflicto.
- Épica 7 (relacionada, no reemplazada): la Historia 7.1 define la imagen nginx de deploy con COOP/COEP, MIME de wasm y Brotli que sirve solo archivos del programa, y la 7.2 la publica por digest en el registry y la despliega en un entorno privado con smoke L3b. La imagen de 8.5 sirve también el Pack y es exclusivamente de desarrollo en la LAN; nunca debe usarse como deploy público (la 7.3 exige aprobación humana).
- La Historia 8.5 se hace antes de la 3.1. Reutiliza el web de la épica 4 (OPFS, `?pack=1`, aislamiento de origen) y la Escena `title` determinista de las épicas 2 y 4.
