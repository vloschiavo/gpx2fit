#!/usr/bin/env bash
# make_fixtures.sh: write the small GPX fixtures used by tests/test_gpx.c into
# tests/data/. Safe to re-run. The values come from the checks in test_gpx.c;
# the line numbers of the error fixtures matter ("line 5:" in the messages).
set -euo pipefail
cd "$(dirname "$0")/.."
d=${1:-tests/data}
mkdir -p "$d"

NS11='xmlns="http://www.topografix.com/GPX/1/1"'
HDR='<?xml version="1.0" encoding="UTF-8"?>'

# GPX 1.1: track name differs from <metadata><name>; point 2 has only a Garmin
# extension (no <ele>); second waypoint has no name.
cat >"$d/gpx11_basic.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11 xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1">
  <metadata><name>Metadata Name</name></metadata>
  <wpt lat="38.5820" lon="-121.4938"><name>Water</name></wpt>
  <wpt lat="38.5821" lon="-121.4936"></wpt>
  <trk>
    <name>Basic Track</name>
    <trkseg>
      <trkpt lat="38.5816" lon="-121.4944"><ele>10.5</ele></trkpt>
      <trkpt lat="38.5818" lon="-121.4940"><extensions><gpxtpx:TrackPointExtension><gpxtpx:hr>120</gpxtpx:hr></gpxtpx:TrackPointExtension></extensions></trkpt>
      <trkpt lat="38.5822" lon="-121.4935"><ele>12.25</ele></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

# GPX 1.0: file-level <name> must not become the course name.
cat >"$d/gpx10_basic.gpx" <<EOF
$HDR
<gpx version="1.0" creator="test" xmlns="http://www.topografix.com/GPX/1/0">
  <name>File Level Name</name>
  <wpt lat="38.50" lon="-121.50"><name>Camp</name></wpt>
  <trk>
    <name>Old Format</name>
    <trkseg>
      <trkpt lat="38.50" lon="-121.50"><ele>1.0</ele></trkpt>
      <trkpt lat="38.51" lon="-121.50"><ele>2.0</ele></trkpt>
      <trkpt lat="38.52" lon="-121.50"><ele>3.0</ele></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

# Every GPX element carries a prefix.
cat >"$d/prefixed_ns.gpx" <<EOF
$HDR
<gpx:gpx version="1.1" creator="test" xmlns:gpx="http://www.topografix.com/GPX/1/1">
  <gpx:wpt lat="10.0" lon="20.0"><gpx:name>Prefixed WP</gpx:name></gpx:wpt>
  <gpx:trk>
    <gpx:name>Prefixed Track</gpx:name>
    <gpx:trkseg>
      <gpx:trkpt lat="10.0" lon="20.0"><gpx:ele>7.0</gpx:ele></gpx:trkpt>
      <gpx:trkpt lat="10.1" lon="20.0"><gpx:ele>8.0</gpx:ele></gpx:trkpt>
    </gpx:trkseg>
  </gpx:trk>
</gpx:gpx>
EOF

# No <ele> on any point (one is an empty element); no track name (the course
# name falls back to the file name "no_ele"); no waypoints.
cat >"$d/no_ele.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <trkseg>
      <trkpt lat="1.0" lon="1.0"></trkpt>
      <trkpt lat="1.1" lon="1.0"><ele/></trkpt>
      <trkpt lat="1.2" lon="1.0"></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

# Non-ASCII waypoint and track names (UTF-8).
cat >"$d/unicode_wpt.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <wpt lat="1.0" lon="1.0"><name>Café ☕ 水</name></wpt>
  <trk>
    <name>Ünïcödé Trail</name>
    <trkseg>
      <trkpt lat="1.0" lon="1.0"></trkpt>
      <trkpt lat="1.1" lon="1.0"></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

# One track, two segments: points are concatenated in order.
cat >"$d/two_segments.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <name>Segments</name>
    <trkseg>
      <trkpt lat="1.0" lon="1.0"></trkpt>
      <trkpt lat="1.1" lon="1.0"></trkpt>
    </trkseg>
    <trkseg>
      <trkpt lat="2.0" lon="1.0"></trkpt>
      <trkpt lat="2.1" lon="1.0"></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

# Track 1 is "Short" (2 points), track 2 is "Long" (3 points, longer):
# --track 0 (default) picks the longest, which is track 2.
cat >"$d/two_tracks.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <name>Short</name>
    <trkseg>
      <trkpt lat="0.0" lon="0.0"></trkpt>
      <trkpt lat="0.0" lon="0.001"></trkpt>
    </trkseg>
  </trk>
  <trk>
    <name>Long</name>
    <trkseg>
      <trkpt lat="1.0" lon="1.0"></trkpt>
      <trkpt lat="1.0" lon="1.01"></trkpt>
      <trkpt lat="1.0" lon="1.02"></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

# Error fixtures. In the ones that report a line, the bad element is on
# line 5.
cat >"$d/malformed.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <trkseg>
      <trkpt lat="1.0" lon="1.0"></wrong>
    </trkseg>
  </trk>
</gpx>
EOF

cat >"$d/empty_track.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <name>Empty</name>
    <trkseg></trkseg>
  </trk>
</gpx>
EOF

cat >"$d/not_gpx.gpx" <<EOF
$HDR
<html><body><p>This is not a GPX file.</p></body></html>
EOF

for spec in "bad_lat|abc|1.0" "bad_lat_range|95.0|1.0" "bad_lon_range|1.0|200.0"; do
    IFS='|' read -r name lat lon <<<"$spec"
    cat >"$d/$name.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <trkseg>
      <trkpt lat="$lat" lon="$lon"></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF
done

cat >"$d/missing_lon.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <trkseg>
      <trkpt lat="1.0"></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

cat >"$d/bad_ele.gpx" <<EOF
$HDR
<gpx version="1.1" creator="test" $NS11>
  <trk>
    <trkseg>
      <trkpt lat="1.0" lon="1.0"><ele>high</ele></trkpt>
    </trkseg>
  </trk>
</gpx>
EOF

echo "wrote GPX fixtures to $d"
