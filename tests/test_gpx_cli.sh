#!/usr/bin/env bash
# End-to-end GPX reading tests through the binary (tasks 2.1-2.6).
# Usage: tests/test_gpx_cli.sh [path/to/gpx2fit]   (run from the repo root)
BIN=${1:-./gpx2fit}
D=tests/data
fail=0

ok()   { echo "ok   - $1"; }
bad()  { echo "FAIL - $1"; fail=1; }

# expect DESC RC STDERR_SUBSTRING ARGS...
expect() {
    local desc=$1 want_rc=$2 needle=$3; shift 3
    local err rc
    err=$("$BIN" "$@" 2>&1 >/dev/null); rc=$?
    if [ "$rc" -ne "$want_rc" ]; then
        bad "$desc (exit $rc, want $want_rc)"
    elif [ -n "$needle" ] && [[ $err != *"$needle"* ]]; then
        bad "$desc (stderr '$err' lacks '$needle')"
    else
        ok "$desc"
    fi
}

# expect_summary DESC "N trackpoints, M waypoints" ARGS...
expect_summary() {
    local desc=$1 want=$2; shift 2
    local out
    out=$("$BIN" "$@" 2>/dev/null)
    if [[ $out == *"$want"* ]]; then ok "$desc"; else bad "$desc (got '$out')"; fi
}

# Error cases: exit 1 with a one-line message.
expect "missing file"         1 "cannot open"        $D/nope.gpx /tmp/o.fit
expect "malformed XML"        1 "line 5:"            $D/malformed.gpx /tmp/o.fit
expect "no trackpoints"       1 "no trackpoints"     $D/empty_track.gpx /tmp/o.fit
expect "not a GPX file"       1 "not a GPX file"     $D/not_gpx.gpx /tmp/o.fit
expect "non-numeric lat"      1 "invalid latitude"   $D/bad_lat.gpx /tmp/o.fit
expect "lat out of range"     1 "invalid latitude"   $D/bad_lat_range.gpx /tmp/o.fit
expect "lon out of range"     1 "invalid longitude"  $D/bad_lon_range.gpx /tmp/o.fit
expect "missing lon"          1 "missing its lon"    $D/missing_lon.gpx /tmp/o.fit
expect "bad elevation"        1 "invalid elevation"  $D/bad_ele.gpx /tmp/o.fit
expect "--track out of range" 1 "does not exist"     --track 3 $D/two_tracks.gpx /tmp/o.fit

# Warnings (stderr) and their suppression.
expect "warns on several tracks"   0 "--track N"       $D/two_tracks.gpx /tmp/o.fit
expect "warns on several segments" 0 "2 segments"      $D/two_segments.gpx /tmp/o.fit
expect "--track avoids the warning" 0 ""               --track 2 $D/two_tracks.gpx /tmp/o.fit
if [ -z "$("$BIN" -q $D/two_tracks.gpx /tmp/o.fit 2>&1)" ]; then
    ok "--quiet silences warnings"
else
    bad "--quiet silences warnings"
fi

# Counts and names.
expect_summary "GPX 1.1 counts"  "'Basic Track': 3 trackpoints, 2 waypoints" $D/gpx11_basic.gpx /tmp/o.fit
expect_summary "GPX 1.0 counts"  "'Old Format': 3 trackpoints, 1 waypoints"  $D/gpx10_basic.gpx /tmp/o.fit
expect_summary "prefixed names"  "2 trackpoints, 1 waypoints"                $D/prefixed_ns.gpx /tmp/o.fit
expect_summary "name from file"  "'no_ele': 3 trackpoints"                   $D/no_ele.gpx /tmp/o.fit
expect_summary "--name override" "'Custom': 3 trackpoints"                   --name Custom $D/gpx11_basic.gpx /tmp/o.fit

# Cross-check against xmllint on every fixture that parses (and the CalTopo
# sample, if present): our trackpoint/waypoint counts must match XPath counts.
if command -v xmllint >/dev/null; then
    for f in $D/gpx11_basic.gpx $D/gpx10_basic.gpx $D/prefixed_ns.gpx \
             $D/no_ele.gpx $D/unicode_wpt.gpx $D/caltopo_sample.gpx; do
        [ -f "$f" ] || continue
        # Skip multi-track files: we only read one track by design.
        ntrk=$(xmllint --xpath 'count(//*[local-name()="trk"])' "$f")
        [ "$ntrk" = "1" ] || continue
        npt=$(xmllint --xpath 'count(/*/*[local-name()="trk"]/*[local-name()="trkseg"]/*[local-name()="trkpt"])' "$f")
        nwp=$(xmllint --xpath 'count(/*/*[local-name()="wpt"])' "$f")
        npt=${npt%.*}; nwp=${nwp%.*}
        out=$("$BIN" "$f" /tmp/o.fit 2>/dev/null)
        if [[ $out == *"$npt trackpoints, $nwp waypoints"* ]]; then
            ok "xmllint agrees: $(basename "$f") ($npt pts, $nwp wpts)"
        else
            bad "xmllint disagrees: $(basename "$f") (xmllint $npt/$nwp, got '$out')"
        fi
    done
else
    echo "skip - xmllint not installed; counts not cross-checked"
fi

exit $fail
