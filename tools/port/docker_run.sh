#!/bin/bash
# Runs a command in the port image (or $DW_IMAGE) with the repo mounted at /dw, as the host user.
cd "$(git rev-parse --show-toplevel)"
gui=()
if [ "${1:-}" = --gui ]; then
    shift
    found=0
    x11=0
    for socket in /tmp/.X11-unix/X*; do
        if [ -S "$socket" ]; then x11=1; break; fi
    done
    if [ "$x11" = 1 ]; then
        gui+=(-v /tmp/.X11-unix:/tmp/.X11-unix:ro -e "DISPLAY=${DISPLAY:-:0}")
        found=1
    fi
    runtime=${XDG_RUNTIME_DIR:-/mnt/wslg/runtime-dir}
    wayland=${WAYLAND_DISPLAY:-wayland-0}
    pulse=${PULSE_SERVER:-unix:/mnt/wslg/PulseServer}
    if [ -S "$runtime/$wayland" ]; then
        gui+=(-v "$runtime:$runtime:ro" -e "XDG_RUNTIME_DIR=$runtime" -e "WAYLAND_DISPLAY=$wayland")
        found=1
    fi
    if [[ "$pulse" == unix:* ]] && [ -S "${pulse#unix:}" ]; then
        gui+=(-v "${pulse#unix:}:${pulse#unix:}:ro" -e "PULSE_SERVER=$pulse")
        found=1
    fi
    # WSLg's runtime-dir contains symlinks into /mnt/wslg.
    if [ "$found" = 1 ] && [ -d /mnt/wslg ]; then gui+=(-v /mnt/wslg:/mnt/wslg:ro); fi
    if [ "$found" = 0 ]; then echo 'docker_run: no X11, Wayland or PulseAudio sockets found' >&2; fi
    if [ -d /dev/input ]; then
        gui+=(-v /dev/input:/dev/input --device-cgroup-rule 'c 13:* rmw')
    fi
    if [ -d /run/udev ]; then gui+=(-v /run/udev:/run/udev:ro); fi
fi
if [ -t 0 ]; then TTY=-it; else TTY=; fi
# Capped like codex-run.sh so heavy runs cannot starve the host.
exec docker run $TTY "${gui[@]}" --rm --cpus "${DW_CPUS:-2.5}" --memory "${DW_MEMORY:-9g}" --memory-swap "${DW_MEMORY_SWAP:-10g}" --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD":/dw -v dw_venv:/dw/.venv "${DW_IMAGE:-dw-port:latest}" "$@"
