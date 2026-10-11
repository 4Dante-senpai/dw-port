#!/bin/sh
# Builds loop.c as PROXY_TO_PTHREAD, ASYNCIFY and JSPI, runs each in node, prints sizes.
# Run in the port image: tools/port/docker_run.sh tools/port/env_check/web_models/run.sh
set -e
D=tools/port/env_check/web_models
O=build/port/web_models
mkdir -p $O
COMMON="-O2 -sSUPPORT_LONGJMP=wasm -fwasm-exceptions"
emcc $D/loop.c -o $O/thread.js $COMMON -DMODEL_THREAD -pthread -sPROXY_TO_PTHREAD -sEXIT_RUNTIME
emcc $D/loop.c -o $O/asyncify.js -O2 -sASYNCIFY -sSUPPORT_LONGJMP=emscripten
emcc $D/loop.c -o $O/jspi.js $COMMON -sJSPI
for m in thread asyncify jspi; do
	flags=
	[ $m = jspi ] && flags=--experimental-wasm-jspi # node 24 still gates JSPI behind a flag
	printf '%s: wasm=%s bytes, ' $m "$(stat -c %s $O/$m.wasm)"
	node $flags $O/$m.js 2>&1 | tail -1
done
