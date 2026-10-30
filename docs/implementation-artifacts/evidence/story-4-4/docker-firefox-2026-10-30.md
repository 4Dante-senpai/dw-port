# Intento de Docker, Firefox y `ctest --preset web` (2026-10-30)

Resultado: **Firefox y la imagen `dw-port` siguen sin verificar.** Nada de esta página
cambia el estado de la historia 4.4 (`review`).

## Docker

- `dockerd` arrancó en la sesión (`Docker daemon ... version=29.9.2`, API en
  `/var/run/docker.sock`).
- `tools/port/docker_build.sh` (imagen base `dw-build` del `Dockerfile` raíz) falló en
  `RUN pip3 install -r requirements.txt`:
  `SSLError(SSLCertVerificationError(1, '[SSL: CERTIFICATE_VERIFY_FAILED] certificate verify
  failed: self-signed certificate in certificate chain'))` para `https://pypi.org/simple/colorama/`.
  El proxy de salida de la sesión re-firma TLS con su propia CA, que la imagen no conoce.
- Para evitarlo sin tocar archivos del repo se intentó etiquetar localmente un `ubuntu:noble`
  con esa CA. `docker pull ubuntu:noble` (tres intentos, 20 s entre cada uno) respondió:
  `unexpected status from HEAD request to
  https://registry-1.docker.io/v2/library/ubuntu/manifests/noble: 429 Too Many Requests`.
  No quedó ninguna imagen local, así que no se pudo seguir.
- Aunque la base se construyera, `tools/port/Dockerfile` ejecuta
  `npx playwright install --with-deps chromium firefox`, que descarga de `cdn.playwright.dev`
  (ver abajo).

## Firefox

- `npx playwright install firefox` (Playwright 1.64.0): `Download failed: server returned code
  403 body 'request blocked: no rule or allowlist entry allows host "cdn.playwright.dev"'`.
- Proxy de la sesión: `cdn.playwright.dev:443` y `ftp.mozilla.org:443` → `connect_rejected`.
- Se resuelve agregando esos hosts en *Network access* del entorno o corriendo en la imagen
  `dw-port` del PC del dueño o del runner de CI.

## `ctest --preset web` sin Docker (emsdk 6.0.10 local, Chromium preinstalado)

43 tests: 84 % pasan, 7 fallan, 4 se saltan (clase B sin `DW1_DISC_DIR`).

- `web_boot`, `web_launcher`, `web_shell`, `web_disc`, `web_io`: **todos los casos Chromium
  pasan** (4, 2, 10, 19 y 6); los 23 fallos son el proyecto Firefox:
  `browserType.launch: Executable doesn't exist at /opt/ms-playwright/firefox-1555/firefox/firefox`.
- `reference_capture_unit`: `/usr/bin/xvfb-run: 184: pcsx-redux: not found` (fuera de la imagen).
- `setjmp`: falló con un artefacto obsoleto del primer build de la sesión (el que corrió
  antes de inicializar `external/psyq_headers`). Tras recompilar `test_setjmp` desde cero
  pasa 5/5, igual que en el commit base `3115031` compilado aparte.
