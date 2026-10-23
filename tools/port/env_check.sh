#!/bin/sh
# Builds and runs tools/port/env_check natively (Clang) and for the web (Emscripten, run in node).
# Run inside the port image: tools/port/docker_run.sh tools/port/env_check.sh
set -e
SRC=tools/port/env_check
OUT=build/port/env_check
clang --version | head -1
cmake --version | head -1
ninja --version
emcc --version | head -1
CC=clang cmake -S $SRC -B $OUT/native -G Ninja -DCMAKE_PREFIX_PATH="$SDL3_NATIVE_PREFIX"
cmake --build $OUT/native
LD_LIBRARY_PATH="$SDL3_NATIVE_PREFIX/lib" $OUT/native/env_check
CC=clang cmake -S $SRC -B $OUT/native32 -G Ninja -DCMAKE_C_FLAGS=-m32 -DCMAKE_PREFIX_PATH="$SDL3_NATIVE32_PREFIX"
cmake --build $OUT/native32
LD_LIBRARY_PATH="$SDL3_NATIVE32_PREFIX/lib" $OUT/native32/env_check
emcmake cmake -S $SRC -B $OUT/web -G Ninja -DCMAKE_PREFIX_PATH="$SDL3_WEB_PREFIX" -DCMAKE_FIND_ROOT_PATH="$SDL3_WEB_PREFIX"
cmake --build $OUT/web
node $OUT/web/env_check.js
echo "env_check: OK"
