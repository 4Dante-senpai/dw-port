#!/bin/sh
# Private homelab soak; install scheduling outside the repository. No CI job.
set -eu
cd "$(git rev-parse --show-toplevel)"
output="build/port/playtest/soak/$(date -u +%Y%m%dT%H%M%SZ)"
exec tools/port/remote_run.sh sh -c '
    set -eu
    output=$1
    shift
    cmake --preset native-headless-release
    cmake --build --preset native-headless-release
    cmake --preset native-asan
    cmake --build --preset native-asan
    export ASAN_OPTIONS=detect_stack_use_after_return=0
    export DW1_DISC_DIR=/dw/game_rom/extracted
    ctest --test-dir build/port/native-headless-release -R "^pack_disc$" --output-on-failure
    if [ ! -f build/port/native-headless-release/us.pack ]; then
        echo "soak: native-headless-release/us.pack missing after pack_disc; check DW1_DISC_DIR" >&2
        exit 1
    fi
    exec python3 -I -B tools/port/playtest/run.py --soak \
        --build native-headless-release --build native-asan \
        --prefix tools/port/scenes/newgame.toml \
        --vblanks 540079 --jobs 4 --output "$output" "$@"
' sh "$output" "$@"
