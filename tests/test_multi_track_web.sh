#!/usr/bin/env bash
# T1: multi-track stitching / split downloads in the Web/WASM UI.
# Usage: tests/test_multi_track_web.sh [path/to/gpx2fit]
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail=0
expect() {
    local desc=$1; shift
    if "$@"; then
        echo "ok   - $desc"
    else
        echo "FAIL - $desc"; fail=1
    fi
}

INDEX="$ROOT/web/index.html"
APP="$ROOT/web/app.js"
BRIDGE="$ROOT/src/wasm_api.c"

expect "UI exposes the stitch checkbox" \
    grep -Fq 'id="stitch-tracks"' "$INDEX"
expect "UI exposes a container for per-track downloads" \
    grep -Fq 'id="download-list"' "$INDEX"
expect "checkbox is read when options are collected" \
    grep -Fq 'stitchCheckbox.checked' "$APP"
expect "JS sends track and stitch to the bridge" \
    grep -Fq 'options.stitch ? 1 : 0' "$APP"
expect "bridge accepts the two new arguments" \
    grep -Fq 'long track,int stitch' "$BRIDGE"

if ! command -v node >/dev/null 2>&1; then
    echo "SKIP - node is not installed; browser multi-track tests skipped"
elif [ ! -f "$ROOT/web/dist/gpx2fit.js" ]; then
    echo "SKIP - web/dist/gpx2fit.js is not built; run make wasm to enable browser multi-track tests"
else
    for test in test_wasm.mjs test_web.mjs; do
        if (cd "$ROOT" && node "tests/$test"); then
            echo "ok   - browser $test"
        else
            echo "FAIL - browser $test"; fail=1
        fi
    done
fi

exit "$fail"
