#!/usr/bin/env bash
# test_fit_course_points.sh: end-to-end check of the expanded default map
# (T0). A GPX with waypoints named after new default-map entries is
# converted, and the resulting FIT is decoded with the Garmin SDK tool
# FitCSVTool.jar (tools/FitCSVTool.jar, or set FITCSVTOOL=path). Decoded
# course_point (name=type) pairs are compared with the FIT SDK enum values.
# Skipped when java or the jar is missing.
#
# Usage: bash tests/test_fit_course_points.sh ./gpx2fit   (run from repo root)

set -euo pipefail

BIN=${1:-./gpx2fit}
JAR=${FITCSVTOOL:-}

if [ -z "$JAR" ]; then
    for c in tools/FitCSVTool.jar tests/tools/FitCSVTool.jar; do
        if [ -f "$c" ]; then JAR=$c; break; fi
    done
fi

fail=0
checks=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); fail=$((fail + 1)); echo "FAIL: $1" >&2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Track shape from tests/run.sh: 101 points north, lon -121.5, lat 38.000..38.100.
# make_gpx FILE NAME:LAT ... writes one waypoint per pair on the track.
make_gpx() {
    local f=$1; shift
    local w name lat
    {
        printf '<?xml version="1.0" encoding="UTF-8"?>\n'
        printf '<gpx version="1.1" creator="test_fit_course_points.sh" '
        printf 'xmlns="http://www.topografix.com/GPX/1/1">\n'
        for w in "$@"; do
            IFS=: read -r name lat <<<"$w"
            printf '<wpt lat="%s" lon="-121.500000"><name>%s</name></wpt>\n' \
                "$lat" "$name"
        done
        printf '<trk><name>Course Points</name><trkseg>\n'
        awk 'BEGIN {
            for (i = 0; i <= 100; i++)
                printf "<trkpt lat=\"%.6f\" lon=\"-121.500000\"></trkpt>\n", 38 + i * 0.001
        }'
        printf '</trkseg></trk>\n</gpx>\n'
    } >"$f"
}

GPX="$TMP/cps.gpx"
make_gpx "$GPX" \
    "Aid Station:38.010" \
    "Energy Gel:38.030" \
    "Sports Drink:38.050" \
    "First Aid:38.060" \
    "HC Climb:38.070" \
    "Shower:38.080" \
    "Gear:38.090" \
    "Navaid:38.095" \
    "Transport:38.098" \
    "Transition:38.010" \
    "Checkpoint:38.030" \
    "Meeting Spot:38.050" \
    "Coffee Shops:38.070" \
    "Community:38.090"

FIT="$TMP/cps.fit"
if ! "$BIN" --quiet "$GPX" "$FIT" 2>"$TMP/err"; then
    echo "FAIL: gpx2fit did not convert $GPX" >&2
    exit 1
fi
[ ! -s "$TMP/err" ] && ok || bad "gpx2fit wrote stderr: $(cat "$TMP/err")"
[ -s "$FIT" ] && ok || bad "FIT file $FIT is missing or empty"

have_java=1
if [ -z "$JAR" ] || ! command -v java >/dev/null 2>&1; then
    echo "SKIP: java or FitCSVTool.jar not found (set FITCSVTOOL=path)"
    have_java=0
fi

if [ "$have_java" -eq 1 ]; then
    # FitCSVTool fails (or prints problems) on a broken FIT file.
    java -jar "$JAR" -b "$FIT" "$TMP/cps.csv" -i >"$TMP/javac.log" 2>&1 || true
    if grep -Ei 'error|warning|problem' "$TMP/javac.log"; then
        bad "FitCSVTool reported problems: $(cat "$TMP/javac.log")"
    else
        ok
    fi
    [ -s "$TMP/cps.csv" ] && ok || bad "no CSV produced by FitCSVTool"

    # course_point Data rows as "name=type" pairs (type = numeric FIT value).
    awk -F, '$0 ~ /^Data,[0-9]+,course_point/ {
        n = ""; t = ""
        for (i = 4; i + 1 <= NF; i += 3) {
            v = $(i + 1); gsub(/"/, "", v)
            if ($i == "type") t = v
            else if ($i == "name") n = v
        }
        if (n != "" && t != "") print n "=" t
    }' "$TMP/cps.csv" >"$TMP/pairs"

    # name=type pairs expected from the new default-map rules (FIT SDK values).
    for expect in "Aid Station=28" "Energy Gel=32" "Sports Drink=33" \
                  "First Aid=9" "HC Climb=14" "Shower=40" "Gear=41" \
                  "Navaid=50" "Transport=51" "Transition=49" "Checkpoint=35" \
                  "Meeting Spot=37" "Coffee Shops=0" "Community=0"; do
        if grep -qxF "$expect" "$TMP/pairs"; then ok
        else bad "decoded course points lack $expect"; fi
    done
fi

if [ "$fail" -ne 0 ]; then
    echo "test_fit_course_points.sh: $fail of $checks checks FAILED" >&2
    exit 1
fi
if [ "$have_java" -eq 1 ]; then
    echo "test_fit_course_points.sh: all $checks checks passed"
else
    echo "test_fit_course_points.sh: FITCSVTool checks skipped; others passed"
fi
