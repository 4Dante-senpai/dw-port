#!/bin/sh
# Runs a command in the port image (or $DW_IMAGE) with the repo mounted at /dw, as the host user.
cd "$(git rev-parse --show-toplevel)"
if [ -t 0 ]; then TTY=-it; else TTY=; fi
exec docker run $TTY --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD":/dw -v dw_venv:/dw/.venv "${DW_IMAGE:-dw-port:latest}" "$@"
