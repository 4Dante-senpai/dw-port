#!/bin/sh
# Builds the upstream decomp image, then the port image on top of it.
set -e
cd "$(git rev-parse --show-toplevel)"
docker build --tag dw-build:latest .
docker build --tag dw-port:latest --file tools/port/Dockerfile .
