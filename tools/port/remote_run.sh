#!/bin/sh
# Like docker_run.sh, but on a remote Docker daemon ($DW_REMOTE context, default "windows").
# The remote daemon cannot see this filesystem, so the repo is rsynced into the volume
# dw_remote_src before the run and changed files are rsynced back afterwards.
set -e
cd "$(git rev-parse --show-toplevel)"
d="docker --context ${DW_REMOTE:-windows}"
if ! timeout 10 $d info >/dev/null 2>&1; then
    echo "remote_run: ${DW_REMOTE:-windows} unreachable, running locally" >&2
    DOCKER_CONTEXT=default exec tools/port/docker_run.sh "$@"
fi
image="${DW_IMAGE:-dw-port:latest}"
$d image inspect "$image" >/dev/null 2>&1 || docker --context default save "$image" | $d load
if [ "$($d container inspect -f '{{.State.Running}}' dw_sync 2>/dev/null)" != true ]; then
    $d rm -f dw_sync >/dev/null 2>&1 || true
    $d run -d --name dw_sync -v dw_remote_src:/dw alpine:3.20 \
        sh -c 'apk add -q rsync && touch /ready && sleep infinity' >/dev/null
    until $d exec dw_sync test -e /ready; do sleep 1; done
fi
sync="rsync -a --blocking-io --exclude=/.venv --exclude=/.git/credentials"
$sync --delete -e "$d exec -i" ./ dw_sync:/dw/
if [ -t 0 ]; then TTY=-it; else TTY=; fi
status=0
$d run $TTY --rm --cpus "${DW_CPUS:-20}" --memory "${DW_MEMORY:-12g}" --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v dw_remote_src:/dw -v dw_venv:/dw/.venv "$image" "$@" || status=$?
# ponytail: -u keeps local files edited during the run; a file changed on both sides keeps the local copy.
$sync -u -e "$d exec -i" dw_sync:/dw/ ./
exit $status
