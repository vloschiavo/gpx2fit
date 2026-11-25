#!/usr/bin/env bash
# Sample-route tests (task T3). Usage: tests/test_web_sample.sh [path/to/gpx2fit]
# Checks that the web bundle ships the reference Rio Del Lago GPX as its sample
# and that the same file converts cleanly through the CLI.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-./gpx2fit}
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

fail=0
expect() {
    local desc=$1; shift
    if "$@"; then
        echo "ok   - $desc"
    else
        echo "FAIL - $desc"; fail=1
    fi
}

APP="$ROOT/web/app.js"
INDEX="$ROOT/web/index.html"
SW="$ROOT/web/sw.js"
REFERENCE="$ROOT/reference/Rio_Del_Lago_100mi_2025.gpx"
SAMPLE="$ROOT/web/Rio_Del_Lago_100mi_2025.gpx"

expect "web bundle has the Rio Del Lago sample" test -s "$SAMPLE"
expect "bundled sample matches the reference GPX" cmp -s "$REFERENCE" "$SAMPLE"
expect "sample loader fetches the Rio Del Lago file" \
    grep -Fq "fetchImpl('./Rio_Del_Lago_100mi_2025.gpx'" "$APP"
expect "sample loader names the source after the Rio Del Lago file" \
    grep -Fq "}, 'Rio_Del_Lago_100mi_2025.gpx');" "$APP"
expect "sample loader no longer fetches the CalTopo route" \
    bash -c "! grep -Fq 'caltopo_sample.gpx' '$APP'"
expect "Load sample button names the Rio Del Lago route" \
    grep -Fq 'Load sample Rio Del Lago route' "$INDEX"
expect "Load sample button no longer advertises CalTopo" \
    bash -c "! grep -Fq 'Load sample CalTopo route' '$INDEX'"
expect "service worker precaches the Rio Del Lago sample" \
    grep -Fq "'./Rio_Del_Lago_100mi_2025.gpx'," "$SW"
expect "service worker does not precache the old web sample" \
    bash -c "! grep -Fq \"'./caltopo_sample.gpx',\" '$SW'"

# The Makefile must be able to refresh the checked-in copy from reference/.
mkdir -p "$TMP/mk/web" "$TMP/mk/reference"
cp "$ROOT/Makefile" "$TMP/mk/"
cp "$REFERENCE" "$TMP/mk/reference/"
if make -C "$TMP/mk" web-sample >"$TMP/make.log" 2>&1; then
    echo "ok   - make web-sample stages the reference route"
else
    echo "FAIL - make web-sample stages the reference route"; cat "$TMP/make.log"; fail=1
fi
expect "make web-sample copies the reference GPX" \
    cmp -s "$REFERENCE" "$TMP/mk/web/Rio_Del_Lago_100mi_2025.gpx"

# Convert the reference file with the binary under test.
if [ ! -x "$BIN" ]; then
    echo "FAIL - '$BIN' not found or not executable (run make first)"; exit 1
fi
"$BIN" "$REFERENCE" "$TMP/rdl.fit" >"$TMP/out" 2>"$TMP/err"
rc=$?
expect "Rio Del Lago converts successfully" test "$rc" -eq 0
expect "summary reports the reference route" \
    grep -Fxq "Read 'RDL 100 2025': 8409 trackpoints, 11 waypoints" "$TMP/out"
expect "summary reports all course points snapped" \
    grep -Fxq 'Snapped 11 of 11 waypoints (0 skipped)' "$TMP/out"
expect "summary reports 11 course points" \
    grep -Fxq "Wrote '$TMP/rdl.fit' (11 course points)" "$TMP/out"
expect "no waypoint names are truncated" \
    bash -c "! grep -Eqi 'truncat' '$TMP/err'"
expect "no waypoints are skipped" \
    bash -c "! grep -Eqi 'skipped' '$TMP/err'"

if [ -s "$TMP/rdl.fit" ]; then
    size=$(wc -c <"$TMP/rdl.fit" | tr -d ' ')
    header=$(od -An -tu1 -N1 "$TMP/rdl.fit" | tr -d ' ')
    magic=$(dd if="$TMP/rdl.fit" bs=1 skip=8 count=4 2>/dev/null)
    # shellcheck disable=SC2207
    bytes=($(od -An -tu1 -j4 -N4 "$TMP/rdl.fit"))
    data_size=$((bytes[0] + (bytes[1] << 8) + (bytes[2] << 16) + (bytes[3] << 24)))
    expect "output is a well-formed FIT file" \
        test "$header" = 14 -a "$magic" = .FIT -a $((14 + data_size + 2)) -eq "$size"
else
    echo "FAIL - output FIT file is missing or empty"; fail=1
fi

exit "$fail"
