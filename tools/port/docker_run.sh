#!/bin/sh
# Runs a command in the port image (or $DW_IMAGE) with the repo mounted at /dw, as the host user.
cd "$(git rev-parse --show-toplevel)"
if [ -t 0 ]; then TTY=-it; else TTY=; fi
# Capped like codex-run.sh so heavy runs cannot starve the host.
exec docker run $TTY --rm --cpus "${DW_CPUS:-2.5}" --memory "${DW_MEMORY:-9g}" --memory-swap "${DW_MEMORY_SWAP:-10g}" --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD":/dw -v dw_venv:/dw/.venv "${DW_IMAGE:-dw-port:latest}" "$@"
