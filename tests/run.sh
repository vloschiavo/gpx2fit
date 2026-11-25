#!/usr/bin/env bash
# tests/run.sh: end-to-end tests for gpx2fit (task 8.4).
#
# Usage: bash tests/run.sh [path/to/gpx2fit]      (default ./gpx2fit)
#
# Runs the binary on fixtures and checks exit codes, the summary text on
# stdout, the warnings on stderr, and that the output is a structurally valid
# FIT file (header, data size, "FIT" signature). Decoding the file with the
# SDK is the job of the encoder tests (7.11).
#
# Most fixtures are generated here, so the expected numbers are known:
#
#   Track      101 points straight north along lon -121.5, lat 38.000 to
#              38.100, 0.001 degree apart.            length 11.12 km
#   Elevation  rises 2 m per point to 100 m, then falls 1 m per point
#              to 50 m.                               ascent 100, descent 50
#   Waypoints  Water      on the track               3.34 km, offset   0.0 m
#              Camp       about  44 m east of it     7.78 km, offset  43.8 m
#              Caution    about 131 m east of it    10.01 km, offset 131.3 m
#              Old Cabin  about 438 m east of it     5.56 km, offset 437.8 m
#                         (beyond the default 250 m, so it is skipped)
#
# These values were computed with geo.c.

set -u
export LC_ALL=C

BIN=${1:-./gpx2fit}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ ! -x "$BIN" ]; then
    echo "run.sh: '$BIN' not found or not executable (run make first)" >&2
    exit 2
fi
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT

PASSES=0
FAILS=0
CURRENT=""
RC=0
OUT="$TMP/stdout"
ERR="$TMP/stderr"

# ---- harness ------------------------------------------------------------

begin() { CURRENT=$1; }
pass()  { PASSES=$((PASSES + 1)); }

show() {
    sed 's/^/    stdout: /' "$OUT" >&2
    sed 's/^/    stderr: /' "$ERR" >&2
}

fail() {
    FAILS=$((FAILS + 1))
    echo "FAIL [$CURRENT]: $1" >&2
    show
}

# run ARGS...: run the binary, capture stdout, stderr and the exit code.
run() {
    "$BIN" "$@" >"$OUT" 2>"$ERR"
    RC=$?
}

expect_rc() {
    if [ "$RC" -eq "$1" ]; then pass; else fail "exit code $RC, expected $1"; fi
}

expect_out() {
    if grep -Eq -- "$1" "$OUT"; then pass; else fail "stdout lacks /$1/"; fi
}

expect_err() {
    if grep -Eq -- "$1" "$ERR"; then pass; else fail "stderr lacks /$1/"; fi
}

expect_no_err_match() {
    if grep -Eq -- "$1" "$ERR"; then fail "stderr has unexpected /$1/"; else pass; fi
}

expect_silent() {
    if [ -s "$OUT" ]; then fail "stdout is not empty"; else pass; fi
    if [ -s "$ERR" ]; then fail "stderr is not empty"; else pass; fi
}

expect_no_file() {
    if [ -e "$1" ]; then fail "$1 should not exist"; else pass; fi
}

# check_fit FILE: a FIT file has a 14-byte header, ".FIT" at bytes 8..11, a
# little-endian data size at bytes 4..7, and a 2-byte CRC after the data.
check_fit() {
    local f=$1 size hdr magic b dsz
    if [ ! -s "$f" ]; then
        fail "$f is missing or empty"
        return
    fi
    size=$(wc -c <"$f" | tr -d ' ')
    hdr=$(od -An -tu1 -N1 "$f" | tr -d ' ')
    magic=$(dd if="$f" bs=1 skip=8 count=4 2>/dev/null)
    # shellcheck disable=SC2207
    b=($(od -An -tu1 -j4 -N4 "$f"))
    dsz=$((b[0] + (b[1] << 8) + (b[2] << 16) + (b[3] << 24)))
    if [ "$hdr" = 14 ] && [ "$magic" = ".FIT" ] && [ $((14 + dsz + 2)) -eq "$size" ]; then
        pass
    else
        fail "$f is not a well-formed FIT file (header $hdr, magic '$magic', data size $dsz, file size $size)"
    fi
}

# Names of the course point lines of the last run, in order, comma-joined.
cp_names() {
    grep -E '^  [^ ]' "$OUT" | awk '{ print $1 }' | paste -sd, -
}

# ---- fixtures -----------------------------------------------------------

# track_points WITH_ELE FIRST LAST: the <trkpt> lines described at the top.
track_points() {
    awk -v ele="$1" -v a="$2" -v b="$3" 'BEGIN {
        for (i = a; i <= b; i++) {
            e = (i <= 50) ? 2 * i : 100 - (i - 50)
            printf "<trkpt lat=\"%.6f\" lon=\"-121.500000\">", 38 + i * 0.001
            if (ele) printf "<ele>%d</ele>", e
            printf "</trkpt>\n"
        }
    }'
}

# make_gpx FILE WITH_ELE [name:lat:lon ...]
make_gpx() {
    local f=$1 ele=$2 w name lat lon
    shift 2
    {
        printf '<?xml version="1.0" encoding="UTF-8"?>\n'
        printf '<gpx version="1.1" creator="run.sh" '
        printf 'xmlns="http://www.topografix.com/GPX/1/1">\n'
        for w in "$@"; do
            IFS=: read -r name lat lon <<<"$w"
            printf '<wpt lat="%s" lon="%s"><name>%s</name></wpt>\n' \
                "$lat" "$lon" "$name"
        done
        printf '<trk><name>Test Track</name><trkseg>\n'
        track_points "$ele" 0 100
        printf '</trkseg></trk>\n</gpx>\n'
    } >"$f"
}

WATER="Water:38.030000:-121.500000"
CAMP="Camp:38.070000:-121.499500"
CAUTION="Caution:38.090000:-121.498500"
CABIN="Old Cabin:38.050000:-121.495000"

STD="$TMP/std.gpx"
make_gpx "$STD" 1 "$WATER" "$CAMP" "$CAUTION" "$CABIN"

# make_outback_gpx FILE: 21 trackpoints north from 38.000 to 38.020, then back.
# Aid is on the shared path (two passes); Flag is just beyond the turnaround
# (one pass at the turn vertex).
make_outback_gpx() {
    local f=$1
    {
        printf '<?xml version="1.0" encoding="UTF-8"?>\n'
        printf '<gpx version="1.1" creator="run.sh" '
        printf 'xmlns="http://www.topografix.com/GPX/1/1">\n'
        printf '<wpt lat="38.010000" lon="-121.500000"><name>Aid</name></wpt>\n'
        printf '<wpt lat="38.021000" lon="-121.500000"><name>Flag</name></wpt>\n'
        printf '<trk><name>Out and Back</name><trkseg>\n'
        awk 'BEGIN {
            for (i = 0; i <= 20; i++)
                printf "<trkpt lat=\"%.6f\" lon=\"-121.500000\"></trkpt>\n", 38 + i * 0.001
            for (i = 19; i >= 0; i--)
                printf "<trkpt lat=\"%.6f\" lon=\"-121.500000\"></trkpt>\n", 38 + i * 0.001
        }'
        printf '</trkseg></trk>\n</gpx>\n'
    } >"$f"
}
OUTBACK="$TMP/outback.gpx"
make_outback_gpx "$OUTBACK"

# ---- basic run ----------------------------------------------------------

begin "basic conversion"
rm -f "$TMP/basic.fit"
run "$STD" "$TMP/basic.fit"
expect_rc 0
expect_out "^Read 'Test Track': 101 trackpoints, 4 waypoints$"
expect_out '^Track: 11\.12 km, ascent 100 m, descent 50 m, 101 points$'
expect_out '^  Water +3\.34 km  offset +0\.0 m$'
expect_out '^  Camp +7\.78 km  offset +4[34]\.[0-9] m$'
expect_out '^  Caution +10\.01 km  offset +13[01]\.[0-9] m$'
expect_out '^Snapped 3 of 4 waypoints \(1 skipped\)$'
expect_out "^Wrote '.*basic\\.fit' \\(3 course points\\)$"
expect_err "warning: waypoint 'Old Cabin' is 4[34][0-9] m from the track \\(limit 250 m\\); skipped"
if [ "$(cp_names)" = "Water,Camp,Caution" ]; then pass
else fail "course points not in distance order: $(cp_names)"; fi
check_fit "$TMP/basic.fit"

begin "same input gives the same file"
run "$STD" "$TMP/basic2.fit"
expect_rc 0
if cmp -s "$TMP/basic.fit" "$TMP/basic2.fit"; then pass
else fail "two runs produced different files"; fi

begin "--snap widens the limit"
run --snap 1000 "$STD" "$TMP/snap.fit"
expect_rc 0
expect_out '^Snapped 4 of 4 waypoints \(0 skipped\)$'
expect_out '^  Old Cabin +5\.56 km  offset +43[0-9]\.[0-9] m$'
expect_no_err_match 'from the track'
expect_err "no type for 'Old Cabin'"
check_fit "$TMP/snap.fit"

# ---- --quiet ------------------------------------------------------------

begin "--quiet prints nothing on success"
rm -f "$TMP/quiet.fit"
run --quiet "$STD" "$TMP/quiet.fit"
expect_rc 0
expect_silent
check_fit "$TMP/quiet.fit"

begin "-q is the short form"
run -q "$STD" "$TMP/quiet2.fit"
expect_rc 0
expect_silent

begin "--quiet still prints errors"
run --quiet "$TMP/does-not-exist.gpx" "$TMP/never.fit"
expect_rc 1
expect_err 'error'
expect_no_file "$TMP/never.fit"

# ---- --strict -----------------------------------------------------------

begin "--strict fails on a skipped waypoint"
rm -f "$TMP/strict.fit"
run --strict "$STD" "$TMP/strict.fit"
expect_rc 1
expect_err '--strict'
expect_no_file "$TMP/strict.fit"

begin "--strict passes when nothing is skipped"
run --strict --snap 1000 "$STD" "$TMP/strict_ok.fit"
expect_rc 0
check_fit "$TMP/strict_ok.fit"

# ---- --max-points -------------------------------------------------------

begin "--max-points reduces the track"
run --max-points 20 "$STD" "$TMP/mp.fit"
expect_rc 0
expect_out '^Track: 11\.12 km, ascent 100 m, descent 50 m, 20 points \(reduced from 101\)$'
expect_out '^Snapped 3 of 4 waypoints \(1 skipped\)$'
check_fit "$TMP/mp.fit"

begin "--max-points leaves course points unchanged"
run "$STD" "$TMP/full.fit"
grep -E '^  [^ ]|^Snapped' "$OUT" >"$TMP/cp_full.txt"
run --max-points 20 "$STD" "$TMP/mp2.fit"
grep -E '^  [^ ]|^Snapped' "$OUT" >"$TMP/cp_reduced.txt"
if cmp -s "$TMP/cp_full.txt" "$TMP/cp_reduced.txt"; then pass
else fail "course point lines differ with --max-points"; fi

begin "--max-points smaller than the track needs warns"
run --snap 1000 --max-points 3 "$STD" "$TMP/mp3.fit"
expect_rc 0
expect_out '6 points \(reduced from 101\)'
expect_err 'warning: --max-points 3 is less than the 6 points'
check_fit "$TMP/mp3.fit"

begin "--max-points smaller than the track needs fails under --strict"
rm -f "$TMP/mp4.fit"
run --snap 1000 --max-points 3 --strict "$STD" "$TMP/mp4.fit"
expect_rc 1
expect_err '--strict'
expect_no_file "$TMP/mp4.fit"

begin "--max-points at or above the point count changes nothing"
run --max-points 101 "$STD" "$TMP/mp5.fit"
expect_rc 0
expect_out ' 101 points$'
run --max-points 5000 "$STD" "$TMP/mp6.fit"
expect_rc 0
expect_out ' 101 points$'

begin "--max-points --quiet is silent"
run --quiet --max-points 3 --snap 1000 "$STD" "$TMP/mp7.fit"
expect_rc 0
expect_silent

# ---- elevation ----------------------------------------------------------

begin "track without elevation"
make_gpx "$TMP/noele.gpx" 0 "$WATER"
run "$TMP/noele.gpx" "$TMP/noele.fit"
expect_rc 0
expect_out '^Track: 11\.12 km, no elevation data, 101 points$'
check_fit "$TMP/noele.fit"

# ---- names --------------------------------------------------------------

begin "--name overrides the GPX name"
run --name "My Course" "$STD" "$TMP/name.fit"
expect_rc 0
expect_out "^Read 'My Course': "

begin "a course name over 90 bytes is truncated with a warning"
LONG=$(printf 'x%.0s' $(seq 1 100))
run --name "$LONG" "$STD" "$TMP/longname.fit"
expect_rc 0
expect_err 'longer than 90 bytes'
check_fit "$TMP/longname.fit"

begin "a long name under --quiet gives no warning"
run --quiet --name "$LONG" "$STD" "$TMP/longname2.fit"
expect_rc 0
expect_silent

begin "UTF-8 waypoint names survive"
make_gpx "$TMP/utf8.gpx" 1 "Water Caf$(printf '\303\251'):38.030000:-121.500000"
run "$TMP/utf8.gpx" "$TMP/utf8.fit"
expect_rc 0
expect_out "^  Water Caf$(printf '\303\251') +3\\.34 km"
check_fit "$TMP/utf8.fit"

# ---- --map --------------------------------------------------------------

begin "a name matching no rule warns"
make_gpx "$TMP/banana.gpx" 1 "Banana:38.030000:-121.500000"
run "$TMP/banana.gpx" "$TMP/banana.fit"
expect_rc 0
expect_err "no type for 'Banana'"

begin "--map rule removes that warning"
printf '# test map\nbanana = water\n' >"$TMP/my.map"
run --map "$TMP/my.map" "$TMP/banana.gpx" "$TMP/banana2.fit"
expect_rc 0
expect_no_err_match "no type for"
check_fit "$TMP/banana2.fit"

begin "a default rule needs no --map"
run "$STD" "$TMP/default_map.fit"
expect_no_err_match "no type for"

begin "a bad line in the map file warns with its line number"
printf 'banana = water\nthis is not a rule\n' >"$TMP/bad.map"
run --map "$TMP/bad.map" "$TMP/banana.gpx" "$TMP/badmap.fit"
expect_rc 0
expect_err ':2:'

begin "--map with a missing file is an error"
rm -f "$TMP/nomap.fit"
run --map "$TMP/no-such.map" "$STD" "$TMP/nomap.fit"
expect_rc 1
expect_err 'error'
expect_no_file "$TMP/nomap.fit"

# ---- tracks and segments ------------------------------------------------

begin "two tracks: longest by default, --track selects"
{
    printf '<?xml version="1.0"?>\n'
    printf '<gpx version="1.1" creator="run.sh" '
    printf 'xmlns="http://www.topografix.com/GPX/1/1">\n'
    printf '<trk><name>Short</name><trkseg>\n'
    track_points 1 0 9
    printf '</trkseg></trk>\n<trk><name>Long</name><trkseg>\n'
    track_points 1 0 100
    printf '</trkseg></trk>\n</gpx>\n'
} >"$TMP/two.gpx"
run "$TMP/two.gpx" "$TMP/two.fit"
expect_rc 0
expect_out "^Read 'Long': 101 trackpoints"
run --track 1 "$TMP/two.gpx" "$TMP/two1.fit"
expect_rc 0
expect_out "^Read 'Short': 10 trackpoints"

begin "two segments are joined"
{
    printf '<?xml version="1.0"?>\n'
    printf '<gpx version="1.1" creator="run.sh" '
    printf 'xmlns="http://www.topografix.com/GPX/1/1">\n'
    printf '<trk><name>Seg</name><trkseg>\n'
    track_points 1 0 49
    printf '</trkseg><trkseg>\n'
    track_points 1 50 100
    printf '</trkseg></trk>\n</gpx>\n'
} >"$TMP/seg.gpx"
run "$TMP/seg.gpx" "$TMP/seg.fit"
expect_rc 0
expect_out "^Read 'Seg': 101 trackpoints"
expect_out '^Track: 11\.12 km, '

# ---- bad input ----------------------------------------------------------

begin "missing input file"
rm -f "$TMP/m.fit"
run "$TMP/missing.gpx" "$TMP/m.fit"
expect_rc 1
expect_err 'error'
expect_no_file "$TMP/m.fit"

begin "malformed XML"
printf '<gpx><trk><trkseg><trkpt lat="1" lon="2">\n' >"$TMP/bad.gpx"
rm -f "$TMP/bad.fit"
run "$TMP/bad.gpx" "$TMP/bad.fit"
expect_rc 1
expect_err 'error'
expect_no_file "$TMP/bad.fit"

begin "track with no points"
printf '<?xml version="1.0"?>\n<gpx version="1.1" creator="x" xmlns="http://www.topografix.com/GPX/1/1"><trk><name>E</name><trkseg></trkseg></trk></gpx>\n' >"$TMP/empty.gpx"
rm -f "$TMP/empty.fit"
run "$TMP/empty.gpx" "$TMP/empty.fit"
expect_rc 1
expect_err 'error'
expect_no_file "$TMP/empty.fit"

begin "output in a directory that does not exist"
run "$STD" "$TMP/no-such-dir/out.fit"
expect_rc 1
expect_err 'cannot write'

# ---- command line -------------------------------------------------------

begin "--help"
run --help
expect_rc 0
expect_out '^Usage: '
expect_out 'both \(alias: all'

begin "no arguments"
run
expect_rc 2
expect_err 'expected exactly 2 arguments'

begin "one argument"
run "$STD"
expect_rc 2

begin "three arguments"
run "$STD" "$TMP/a.fit" "$TMP/b.fit"
expect_rc 2

begin "unknown option"
run --bogus "$STD" "$TMP/o.fit"
expect_rc 2

begin "invalid option values are usage errors"
run --snap -1 "$STD" "$TMP/o.fit";          expect_rc 2
run --snap abc "$STD" "$TMP/o.fit";         expect_rc 2
run --max-points 1 "$STD" "$TMP/o.fit";     expect_rc 2
run --max-points x "$STD" "$TMP/o.fit";     expect_rc 2
run --track 0 "$STD" "$TMP/o.fit";          expect_rc 2
run --pass sideways "$STD" "$TMP/o.fit";    expect_rc 2
run --pace fast "$STD" "$TMP/o.fit";        expect_rc 2
expect_err 'invalid --pace'
run --start yesterday "$STD" "$TMP/o.fit";  expect_rc 2
expect_err 'invalid --start'

begin "a usage error does no work"
rm -f "$TMP/o.fit"
run --pace fast "$STD" "$TMP/o.fit"
expect_no_file "$TMP/o.fit"

begin "valid --pace, --start and --pass are accepted"
run --pace 6:40 --start 2025-06-01 --pass ordered "$STD" "$TMP/ok1.fit"
expect_rc 0
check_fit "$TMP/ok1.fit"
run --pace 2.5 --start 2025-06-01T08:30:00Z --pass last "$STD" "$TMP/ok2.fit"
expect_rc 0
check_fit "$TMP/ok2.fit"

# ---- --pass both --------------------------------------------------------

begin "--pass both emits one course point per pass"
run --pass both "$OUTBACK" "$TMP/both.fit"
expect_rc 0
expect_out "^Read 'Out and Back': 41 trackpoints, 2 waypoints$"
expect_out '^  Aid +1\.11 km  offset +0\.0 m$'
expect_out '^  Flag +2\.22 km  offset +11[01]\.[0-9] m$'
expect_out '^  Aid +3\.34 km  offset +0\.0 m$'
expect_out '^Snapped 2 of 2 waypoints \(0 skipped\)$'
expect_out '^Emitted 3 course points for 2 waypoints$'
expect_out "^Wrote '.*both\\.fit' \\(3 course points\\)$"
if [ "$(cp_names)" = "Aid,Flag,Aid" ]; then pass
else fail "course points not in track order: $(cp_names)"; fi
check_fit "$TMP/both.fit"

begin "--pass all is an alias for both"
run --pass all "$OUTBACK" "$TMP/all.fit"
expect_rc 0
if [ "$(cp_names)" = "Aid,Flag,Aid" ]; then pass
else fail "--pass all gave: $(cp_names)"; fi
check_fit "$TMP/all.fit"

begin "--max-points keeps a trackpoint for every pass"
run --pass both "$OUTBACK" "$TMP/both_full.fit"
grep -E '^  [^ ]|^Snapped|^Emitted' "$OUT" >"$TMP/both_full.txt"
run --pass both --max-points 5 "$OUTBACK" "$TMP/both_mp.fit"
expect_rc 0
expect_out '^Track: [0-9.]+ km, no elevation data, 5 points \(reduced from 41\)$'
grep -E '^  [^ ]|^Snapped|^Emitted' "$OUT" >"$TMP/both_mp.txt"
if cmp -s "$TMP/both_full.txt" "$TMP/both_mp.txt"; then pass
else fail "course points changed with --max-points"; fi
if [ "$(cp_names)" = "Aid,Flag,Aid" ]; then pass
else fail "--max-points lost a pass: $(cp_names)"; fi
check_fit "$TMP/both_mp.fit"

# ---- the CalTopo sample (if present) ------------------------------------

SAMPLE="$HERE/data/caltopo_sample.gpx"
if [ -f "$SAMPLE" ]; then
    begin "caltopo_sample.gpx"
    run "$SAMPLE" "$TMP/sample.fit"
    expect_rc 0
    expect_out '^Track: [0-9.]+ km, '
    expect_out '^Snapped [0-9]+ of [0-9]+ waypoints \([0-9]+ skipped\)$'
    expect_out "^Wrote '.*sample\\.fit' "
    check_fit "$TMP/sample.fit"

    begin "caltopo_sample.gpx has a waypoint beyond 250 m"
    run --strict "$SAMPLE" "$TMP/sample_strict.fit"
    expect_rc 1
fi

# ---- result -------------------------------------------------------------

echo "run.sh: $PASSES checks passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
