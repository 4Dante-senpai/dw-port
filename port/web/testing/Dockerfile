FROM nginx:1.28.0-alpine@sha256:30f1c0d78e0ad60901648be663a710bdadf19e4c10ac6782c235200619158284
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY dw1.html dw1.js dw1.wasm io_worker.js dw1_pack.js dw1_pack.wasm us.tsv seed.html seed_worker.js version.txt /usr/share/nginx/html/
HEALTHCHECK --interval=2s --timeout=2s --start-period=5s --retries=15 CMD wget -q -O /dev/null http://127.0.0.1/version.txt || exit 1
