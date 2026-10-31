#!/usr/bin/env bash
# Keep daemon addresses and Pack contents out of command output and error logs.
set +x
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
unset DOCKER_CONTEXT
export DOCKER_HOST=${HUB_DOCKER_HOST:?HUB_DOCKER_HOST is required}
port=${DW1_TESTING_PORT:-8090}
[[ $port =~ ^[0-9]+$ ]] && ((port > 0 && port < 65536)) || { echo 'invalid testing port' >&2; exit 1; }
# Docker errors may embed the daemon URL. Report the operation only.
d() {
    if ! docker "$@" 2>"$work/docker.err"; then
        echo "Docker operation failed: $1" >&2
        # Show the cause with the daemon address redacted.
        local host=${DOCKER_HOST#*://}; host=${host%%[:/]*}
        sed -e "s#$DOCKER_HOST#<docker-host>#g" -e "s#$host#<deploy-host>#g" "$work/docker.err" >&2
        return 1
    fi
}
image_exists() { d image inspect "$1" >/dev/null 2>&1; }
wait_healthy() {
    local state
    for ((i=0; i<60; i++)); do
        state=$(d inspect --format '{{.State.Health.Status}}' dw1-testing) || return 1
        [[ $state != healthy ]] || return 0
        [[ $state != unhealthy ]] || return 1
        sleep 1
    done
    echo 'testing healthcheck timed out' >&2
    return 1
}
start() {
    if d container inspect dw1-testing >/dev/null 2>&1; then d rm -f dw1-testing >/dev/null || return 1; fi
    d run -d --name dw1-testing --restart unless-stopped -p "$port:80" \
        -v dw1-testing-pack:/pack:ro "$1" >/dev/null || return 1
    wait_healthy
}
rollback() {
    image_exists dw1-testing:previous || { echo 'no previous testing image' >&2; return 1; }
    start dw1-testing:previous || return 1
    d tag dw1-testing:previous dw1-testing:current || return 1
    echo 'previous testing image restored'
}
if [[ ${1:-} == --rollback ]]; then rollback; exit; fi
build=${1:-$root/build/port/web}
sha=${CI_COMMIT_SHA:-$(git -C "$root" rev-parse HEAD)}
[[ $sha =~ ^[a-f0-9]{40}$ ]] || { echo 'invalid commit SHA' >&2; exit 1; }
mkdir "$work/context"
for file in dw1.html dw1.js dw1.wasm io_worker.js audio_worklet.js dw1_pack.js dw1_pack.wasm us.tsv seed.html seed_worker.js; do
    cp "$build/$file" "$work/context/$file"
done
printf '%s\n' "$sha" > "$work/context/version.txt"
cp "$root/port/web/testing/"{Dockerfile,nginx.conf} "$work/context/"
python3 "$root/tools/port/testing/check_context.py" "$work/context" "$root/tools/port/manifest/us.tsv"
d build --quiet --tag "dw1-testing:$sha" "$work/context" >/dev/null
# The Pack never enters the image context. Copy it through the Docker API into a volume.
pack_sha=$(sha256sum < "$build/us.pack"); pack_sha=${pack_sha%% *}
printf '%s\n' "$pack_sha" > "$build/us.pack.sha256"
d volume create dw1-testing-pack >/dev/null
d pull --quiet alpine:3.21.3 >/dev/null
helper=$(d create -v dw1-testing-pack:/pack alpine:3.21.3 sh -c \
    'mv /pack/us.pack.tmp /pack/us.pack && mv /pack/us.pack.sha256.tmp /pack/us.pack.sha256')
trap 'd rm -f "$helper" >/dev/null || true; rm -rf "$work"' EXIT
stored=$(d run --rm -v dw1-testing-pack:/pack:ro alpine:3.21.3 \
    sh -c 'test -s /pack/us.pack && cat /pack/us.pack.sha256' 2>/dev/null || true)
if [[ $stored == "$pack_sha" ]]; then
    echo 'Pack unchanged; copy skipped'
else
    d cp "$build/us.pack" "$helper:/pack/us.pack.tmp" >/dev/null
    d cp "$build/us.pack.sha256" "$helper:/pack/us.pack.sha256.tmp" >/dev/null
    d start -a "$helper" >/dev/null
    [[ $(d inspect --format '{{.State.ExitCode}}' "$helper") == 0 ]]
    echo "Pack synchronized: sha256 $pack_sha"
fi
if image_exists dw1-testing:current; then
    current_id=$(d image inspect --format '{{.Id}}' dw1-testing:current)
    new_id=$(d image inspect --format '{{.Id}}' "dw1-testing:$sha")
    if [[ $current_id != "$new_id" ]]; then
        d tag dw1-testing:current dw1-testing:previous
    fi
fi
if ! start "dw1-testing:$sha"; then
    rollback || true
    exit 1
fi
d tag "dw1-testing:$sha" dw1-testing:current
# current/previous keep their images; drop the per-commit tags so the host does not fill up.
for tag in $(d images --format '{{.Tag}}' dw1-testing); do
    [[ $tag =~ ^[a-f0-9]{40}$ && $tag != "$sha" ]] && d rmi "dw1-testing:$tag" >/dev/null || true
done
echo "testing image healthy: $sha"
