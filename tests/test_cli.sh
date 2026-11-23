#!/usr/bin/env bash
# CLI parsing tests (task 1.4). Usage: tests/test_cli.sh [path/to/gpx2fit]
BIN=${1:-./gpx2fit}
fail=0

# A tiny GPX for real conversions (two trackpoints, one waypoint on the track).
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
GPX="$TMP/sport.gpx"
cat >"$GPX" <<'EOF'
<?xml version="1.0"?>
<gpx version="1.1" creator="test_cli.sh" xmlns="http://www.topografix.com/GPX/1/1">
  <wpt lat="38.000000" lon="-121.500000"><name>Water</name></wpt>
  <trk><name>Sport Test</name><trkseg>
    <trkpt lat="38.000000" lon="-121.500000"><ele>100</ele></trkpt>
    <trkpt lat="38.001000" lon="-121.500000"><ele>101</ele></trkpt>
  </trkseg></trk>
</gpx>
EOF

# expect_rc DESC EXPECTED_RC ARGS...
expect_rc() {
    local desc=$1 want=$2; shift 2
    "$BIN" "$@" >/dev/null 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then
        echo "ok   - $desc"
    else
        echo "FAIL - $desc (exit $got, want $want)"; fail=1
    fi
}

# expect_out DESC EXPECTED_LINE ARGS...  (checks --dump-options output)
expect_out() {
    local desc=$1 want=$2; shift 2
    if "$BIN" --dump-options "$@" 2>/dev/null | grep -qxF -- "$want"; then
        echo "ok   - $desc"
    else
        echo "FAIL - $desc (missing '$want')"; fail=1
    fi
}

expect_rc "--help exits 0"                   0 --help
expect_rc "-h exits 0"                       0 -h
expect_rc "minimal valid args"               0 --dump-options in.gpx out.fit
expect_rc "no args"                          2
expect_rc "one positional"                   2 in.gpx
expect_rc "three positionals"                2 a b c
expect_rc "unknown option"                   2 --bogus in.gpx out.fit
expect_rc "missing option value"             2 in.gpx out.fit --snap
expect_rc "--snap not a number"              2 --snap abc in.gpx out.fit
expect_rc "--snap negative"                  2 --snap -5 in.gpx out.fit
expect_rc "--snap nan"                       2 --snap nan in.gpx out.fit
expect_rc "--max-points 1"                   2 --max-points 1 in.gpx out.fit
expect_rc "--max-points junk"                2 --max-points 10x in.gpx out.fit
expect_rc "--track 0"                        2 --track 0 in.gpx out.fit
expect_rc "--track junk"                     2 --track x in.gpx out.fit

if "$BIN" --help 2>&1 | grep -q '^Usage:'; then
    echo "ok   - --help prints usage"
else
    echo "FAIL - --help prints usage"; fail=1
fi

expect_out "defaults: snap"      "snap=250"        in.gpx out.fit
expect_out "defaults: max_points" "max_points=0"   in.gpx out.fit
expect_out "positionals"         "input=in.gpx"    in.gpx out.fit
expect_out "positionals (out)"   "output=out.fit"  in.gpx out.fit
expect_out "--snap value"        "snap=100.5"      --snap 100.5 in.gpx out.fit
expect_out "--name"              "name=My Ride"    --name "My Ride" in.gpx out.fit
expect_out "--pace"              "pace=6:40"       --pace 6:40 in.gpx out.fit
expect_out "--start"             "start=2026-01-01T00:00:00Z" \
                                 --start 2026-01-01T00:00:00Z in.gpx out.fit
expect_out "--max-points"        "max_points=500"  --max-points 500 in.gpx out.fit
expect_out "--map"               "map=my.map"      --map my.map in.gpx out.fit
expect_out "--track"            "track=2"         --track 2 in.gpx out.fit
expect_out "defaults: track"     "track=0"         in.gpx out.fit
expect_out "--quiet"             "quiet=1"         --quiet in.gpx out.fit
expect_out "-q"                  "quiet=1"         -q in.gpx out.fit
expect_out "--strict"            "strict=1"        --strict in.gpx out.fit
expect_out "options after positionals" "snap=40"   in.gpx out.fit --snap 40
expect_out "--opt=value form"    "snap=7"          --snap=7 in.gpx out.fit

# ---- --sport (task 11.3) --------------------------------------------------

# Every documented name/alias, including case-insensitive spellings, must
# resolve to the pinned FIT sport/sub_sport numbers in --dump-options.
expect_sport() {
    local desc=$1 name=$2 sport=$3 sub=$4
    expect_out "$desc: sport"     "sport=$sport"   --sport "$name" in.gpx out.fit
    expect_out "$desc: sub_sport" "sub_sport=$sub" --sport "$name" in.gpx out.fit
}

if "$BIN" --help 2>&1 | grep -q -- '--sport TYPE'; then
    echo "ok   - --help documents --sport TYPE"
else
    echo "FAIL - --help documents --sport TYPE"; fail=1
fi

expect_out "default sport"     "sport=17" in.gpx out.fit
expect_out "default sub_sport" "sub_sport=0" in.gpx out.fit

sport_names=(
    "hiking:17:0"
    "running:1:0"
    "trail-running:1:3" "trail_running:1:3"
    "cycling:2:0"
    "road-cycling:2:7" "road_cycling:2:7"
    "gravel:2:46" "gravel-cycling:2:46" "gravel_cycling:2:46"
    "mtb:2:8" "mountain-biking:2:8" "mountain_biking:2:8"
    "other:0:0" "generic:0:0"
    "HIKING:17:0" "RuNnInG:1:0" "TRAIL-RUNNING:1:3"
    "CYCLING:2:0" "ROAD-CYCLING:2:7" "GrAvEl:2:46" "MTB:2:8"
    "OTHER:0:0"
)
for spec in "${sport_names[@]}"; do
    IFS=: read -r name sport sub <<<"$spec"
    expect_sport "--sport $name" "$name" "$sport" "$sub"
done

expect_rc "--sport unknown value"      2 --sport walk in.gpx out.fit
expect_rc "--sport empty value"        2 --sport "" in.gpx out.fit
expect_rc "missing --sport value"      2 in.gpx out.fit --sport

# A bad sport is a usage error: no work, no output file, clear message.
rm -f "$TMP/bad-sport.fit"
"$BIN" --sport walk "$GPX" "$TMP/bad-sport.fit" >"$TMP/out" 2>"$TMP/err"
rc=$?
if [ "$rc" -eq 2 ] && [ ! -e "$TMP/bad-sport.fit" ] &&
        grep -qF "invalid --sport value 'walk'" "$TMP/err"; then
    echo "ok   - bad --sport fails early with no output file"
else
    echo "FAIL - bad --sport (exit $rc, output file: $([ -e "$TMP/bad-sport.fit" ] && echo yes || echo no))"
    sed 's/^/    /' "$TMP/err"
    fail=1
fi

# The stdout summary names the chosen activity, and --quiet hides it.
if "$BIN" --sport trail-running "$GPX" "$TMP/summary.fit" >"$TMP/out" 2>"$TMP/err" &&
        grep -qxF 'Activity: trail-running' "$TMP/out"; then
    echo "ok   - summary shows the chosen activity"
else
    echo "FAIL - summary shows the chosen activity"; fail=1
fi
if "$BIN" "$GPX" "$TMP/default.fit" >"$TMP/out" 2>"$TMP/err" &&
        grep -qxF 'Activity: hiking' "$TMP/out"; then
    echo "ok   - summary shows the default activity"
else
    echo "FAIL - summary shows the default activity"; fail=1
fi
if "$BIN" --quiet --sport running "$GPX" "$TMP/quiet.fit" >"$TMP/out" 2>"$TMP/err" &&
        [ ! -s "$TMP/out" ]; then
    echo "ok   - --quiet hides the activity line"
else
    echo "FAIL - --quiet hides the activity line"; fail=1
fi

# Decode the actual course message bytes of a CLI-produced FIT file. The
# signature is the course definition written by fit_write.c: local message 1,
# global 31, fields 4 (sport), 5 (91-byte name) and 7 (sub_sport), then its
# data message. Full SDK decoding of these same values is in test_fit_decode.sh.
COURSE_SIG=4100001f0003040100055b0707010001
decode_course() {
    local hex prefix off
    hex=$(od -An -v -tx1 "$1" | tr -d ' \n') || return 1
    prefix=${hex%%"$COURSE_SIG"*}
    [ "$prefix" != "$hex" ] || return 1
    off=${#prefix}
    printf '%d %d\n' "0x${hex:off+32:2}" "0x${hex:off+216:2}"
}

decode_sports=(
    "hiking:17:0" "running:1:0" "trail-running:1:3" "cycling:2:0"
    "road-cycling:2:7" "gravel:2:46" "mtb:2:8" "other:0:0"
)
for spec in "${decode_sports[@]}"; do
    IFS=: read -r name sport sub <<<"$spec"
    fit="$TMP/decoded-$name.fit"
    rm -f "$fit"
    if ! "$BIN" --quiet --sport "$name" "$GPX" "$fit" >"$TMP/out" 2>"$TMP/err"; then
        echo "FAIL - --sport $name conversion (exit $?)"; sed 's/^/    /' "$TMP/err"
        fail=1
        continue
    fi
    got=$(decode_course "$fit") || got="(no course message)"
    if [ "$got" = "$sport $sub" ]; then
        echo "ok   - decoded --sport $name: sport=$sport sub_sport=$sub"
    else
        echo "FAIL - decoded --sport $name: got '$got', want '$sport $sub'"; fail=1
    fi
done

exit $fail
