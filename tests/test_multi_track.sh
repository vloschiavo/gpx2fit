#!/usr/bin/env bash
# T6: end-to-end multi-track stitching and one-FIT-per-track output.
# Usage: tests/test_multi_track.sh [path/to/gpx2fit]   (run from the repo root)
BIN=${1:-./gpx2fit}
cd "$(dirname "$0")/.."
D=tests/data
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
fail=0

ok()  { echo "ok   - $1"; }
bad() { echo "FAIL - $1"; fail=1; }

expect_rc() {
    local desc=$1 want=$2; shift 2
    "$BIN" "$@" >/dev/null 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then ok "$desc"; else
        bad "$desc (exit $got, want $want)"
    fi
}

expect_out() {
    local desc=$1 needle=$2; shift 2
    local out
    out=$("$BIN" "$@" 2>/dev/null)
    if [[ $out == *"$needle"* ]]; then ok "$desc"; else
        bad "$desc (output lacked '$needle')"
    fi
}

JAR=${FITCSVTOOL:-tests/tools/FitCSVTool.jar}
have_java=1
if ! command -v java >/dev/null 2>&1 || [ ! -f "$JAR" ]; then
    echo "SKIP - FITCSVTool decoding (java or $JAR missing)"
    have_java=0
fi

# decode FIT -> CSV; fail on any decoder problem message.
decode() {
    local fit=$1 csv=$2 name=$3 log="$TMP/$3.decode.log"
    java -jar "$JAR" -b "$fit" "$csv" -i >"$log" 2>&1
    if grep -Eiq 'error|warning|problem' "$log"; then
        bad "$name: FITCSVTool reported problems"
        sed 's/^/    /' "$log"
        return 1
    fi
    if [ ! -s "$csv" ]; then
        bad "$name: no decoded CSV"
        return 1
    fi
    return 0
}

# Semicircle longitudes of the FIT record messages, in file order.
record_lons() {
    awk -F, '$1 == "Data" && $3 == "record" {
        gsub(/"/, "", $8); printf "%s ", $8
    }' "$1"
}

# Elevations of the FIT record messages, in file order.
record_alts() {
    awk -F, '$1 == "Data" && $3 == "record" {
        gsub(/"/, "", $17); printf "%s ", $17
    }' "$1"
}

course_names() {
    awk -F, '$1 == "Data" && $3 == "course_point" {
        gsub(/"/, "", $23); printf "%s ", $23
    }' "$1"
}

record_count() {
    awk -F, '$1 == "Data" && $3 == "record" { n++ } END { print n+0 }' "$1"
}

# ---- 1. --stitch with a "Start" waypoint ---------------------------------
expect_out "stitch joins all tracks" "9 trackpoints, 3 waypoints" \
    --snap 2000 --stitch "$D/multi_track.gpx" "$TMP/stitch.fit"
expect_out "stitch names the course after the anchored track" \
    "Read 'Head': 9 trackpoints" --snap 2000 --stitch \
    "$D/multi_track.gpx" "$TMP/stitch.fit"
expect_out "stitch snaps waypoints along the joined course" \
    "Wrote '$TMP/stitch.fit' (3 course points)" --snap 2000 --stitch \
    "$D/multi_track.gpx" "$TMP/stitch.fit"

if [ "$have_java" -eq 1 ] && decode "$TMP/stitch.fit" "$TMP/stitch.csv" stitch; then
    # Head is stored backwards in the GPX; it must be reversed, then Middle,
    # then Tail: longitudes 0.00 .. 0.08 in order.
    want="0 119305 238609 357914 477219 596523 715828 835133 954437 "
    got=$(record_lons "$TMP/stitch.csv")
    if [ "$got" = "$want" ]; then ok "stitch orders/reverses the legs in the FIT"; else
        bad "stitch FIT longitudes '$got', want '$want'"
    fi
    alts=$(record_alts "$TMP/stitch.csv")
    case "$alts" in
        "0.0 1.0 2.0 "*) ok "reversed leg keeps its elevations in order" ;;
        *) bad "reversed leg elevations '$alts'" ;;
    esac
    names=$(course_names "$TMP/stitch.csv")
    if [ "$names" = "Start Water Camp " ]; then ok "stitch course points follow the joined course"; else
        bad "stitch course point names '$names'"
    fi
fi

# Decimation runs on the joined track: its reported original count is 9 and
# all course points survive.
expect_out "stitch decimates the joined track" "5 points (reduced from 9)" \
    --snap 2000 --stitch --max-points 5 "$D/multi_track.gpx" "$TMP/stitch5.fit"
if [ "$have_java" -eq 1 ] && decode "$TMP/stitch5.fit" "$TMP/stitch5.csv" stitch5; then
    [ "$(record_count "$TMP/stitch5.csv")" -eq 5 ] \
        && ok "stitched decimation writes 5 records" \
        || bad "stitched decimation record count $(record_count "$TMP/stitch5.csv")"
    names=$(course_names "$TMP/stitch5.csv")
    if [ "$names" = "Start Water Camp " ]; then ok "stitched decimation keeps all course points"; else
        bad "stitched decimation course points '$names'"
    fi
fi

# ---- 2. --stitch without a "Start" waypoint -------------------------------
expect_out "stitch falls back without a start waypoint" \
    "9 trackpoints, 2 waypoints" --snap 2000 --stitch \
    "$D/multi_track_no_start.gpx" "$TMP/no_start.fit"
if [ "$have_java" -eq 1 ] && decode "$TMP/no_start.fit" "$TMP/no_start.csv" no-start; then
    # Track 1 (Tail) starts the chain; Middle is reversed, Head is forward.
    want="715828 835133 954437 596523 477219 357914 238609 119305 0 "
    got=$(record_lons "$TMP/no_start.csv")
    if [ "$got" = "$want" ]; then ok "fallback chaining is deterministic"; else
        bad "fallback FIT longitudes '$got', want '$want'"
    fi
fi

# ---- 3. --split-tracks: one valid FIT per track ---------------------------
rm -f "$TMP/course.fit" "$TMP/course_1.fit" "$TMP/course_2.fit" "$TMP/course_3.fit"
expect_out "split writes course 1" "Wrote '$TMP/course_1.fit' (1 course points)" \
    --snap 2000 --split-tracks "$D/multi_track.gpx" "$TMP/course.fit"
for n in 2 3; do
    expect_out "split writes course $n" "Wrote '$TMP/course_$n.fit' (1 course points)" \
        --snap 2000 --split-tracks "$D/multi_track.gpx" "$TMP/course.fit"
done
if [ -e "$TMP/course.fit" ]; then bad "split must not write the combined path"; else
    ok "split leaves the combined output path untouched"
fi

split_names=(Tail Middle Head)
split_wpts=(Camp Water Start)
for n in 1 2 3; do
    fit="$TMP/course_$n.fit"
    if [ ! -s "$fit" ]; then bad "split file $fit missing"; continue; fi
    if [ "$have_java" -eq 0 ] || ! decode "$fit" "$TMP/course_$n.csv" "course_$n"; then
        continue
    fi
    [ "$(record_count "$TMP/course_$n.csv")" -eq 3 ] \
        && ok "split course $n has 3 records" \
        || bad "split course $n record count $(record_count "$TMP/course_$n.csv")"
    name=$(awk -F, '$1 == "Data" && $3 == "course" {
        gsub(/"/, "", $8); print $8; exit
    }' "$TMP/course_$n.csv")
    if [ "$name" = "${split_names[$((n-1))]}" ]; then ok "split course $n name $name"; else
        bad "split course $n name '$name'"
    fi
    names=$(course_names "$TMP/course_$n.csv")
    if [ "$names" = "${split_wpts[$((n-1))]} " ]; then
        ok "split course $n snaps only its own waypoint"
    else
        bad "split course $n course points '$names'"
    fi
done

# ---- 4. existing single-track behavior must be unchanged -----------------
default_err=$("$BIN" "$D/multi_track.gpx" "$TMP/default.fit" 2>&1 >/dev/null)
if [[ $default_err == *"--track N"* ]]; then ok "default still warns about several tracks"; else
    bad "default warning lacked '--track N': $default_err"
fi
expect_out "default still reads one track" "Read 'Middle': 3 trackpoints" \
    "$D/multi_track.gpx" "$TMP/default.fit"
expect_out "--track still selects a specific track" "Read 'Head': 3 trackpoints" \
    --track 3 "$D/multi_track.gpx" "$TMP/selected.fit"

# ---- 5. option parsing and error paths -----------------------------------
expect_rc "split needs at least one track" 1 --split-tracks \
    "$D/empty_track.gpx" "$TMP/empty_split.fit"
expect_out "--dump-options: stitch" "stitch=1" --dump-options --stitch in.gpx out.fit
expect_out "--dump-options: split"  "split=1"  --dump-options --split-tracks in.gpx out.fit
expect_out "--dump-options: defaults" "split=0" --dump-options in.gpx out.fit
expect_rc "stitch + split is a usage error" 2 --stitch --split-tracks \
    "$D/multi_track.gpx" "$TMP/bad.fit"
expect_rc "stitch + track is a usage error" 2 --stitch --track 1 \
    "$D/multi_track.gpx" "$TMP/bad.fit"
expect_rc "split + track is a usage error" 2 --split-tracks --track 1 \
    "$D/multi_track.gpx" "$TMP/bad.fit"

exit $fail
