#!/usr/bin/env bash
# Install only the static CLI, verified before extraction.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL https://download.docker.com/linux/static/stable/x86_64/docker-27.3.1.tgz -o "$work/docker.tgz"
printf '%s  %s\n' 9b4f6fe406e50f9085ee474c451e2bb5adb119a03591f467922d3b4e2ddf31d3 "$work/docker.tgz" | sha256sum -c -
tar -xzf "$work/docker.tgz" -C "$work" docker/docker
mkdir -p "${1:?destination directory required}"
cp "$work/docker/docker" "$1/docker"
