#!/usr/bin/env bash
# test_map.sh: end-to-end checks for --map and point types (task 5.4).
# Usage: bash tests/test_map.sh ./gpx2fit   (run from the repository root)
set -u

BIN=${1:-./gpx2fit}
GPX=tests/data/caltopo_sample.gpx
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
checks=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); fail=$((fail + 1)); echo "FAIL: $1" >&2; }

# expect_rc DESC EXPECTED_RC ARGS...   (stderr -> $TMP/err, stdout -> $TMP/out)
run() { "$BIN" "$@" >"$TMP/out" 2>"$TMP/err"; echo $?; }
has()  { grep -qF -- "$1" "$TMP/err"; }

# Missing --map file: exit 1, message names the file, nothing else needed.
rc=$(run --map "$TMP/does-not-exist.map" "$GPX" "$TMP/o.fit")
[ "$rc" = 1 ] && ok || bad "missing --map file: exit $rc, expected 1"
has "does-not-exist.map" && ok || bad "missing --map file: message does not name the file"

# A directory is not a map file either.
rc=$(run --map "$TMP" "$GPX" "$TMP/o.fit")
[ "$rc" = 1 ] && ok || bad "--map directory: exit $rc, expected 1"

# Defaults only. 'Start' and 'Ranger Station' have no rule (NOTES.md 5.1), so
# they warn; Water and Camp are covered by default.map and must not.
rc=$(run "$GPX" "$TMP/o.fit")
[ "$rc" = 0 ] && ok || bad "defaults: exit $rc, expected 0"
has "no type for 'Start'" && ok || bad "defaults: no warning for Start"
has "no type for 'Ranger Station'" && ok || bad "defaults: no warning for Ranger Station"
has "no type for 'Water'" && bad "defaults: Water should match default.map" || ok
has "no type for 'Camp'" && bad "defaults: Camp should match default.map" || ok
has "add a line to --map" && ok || bad "defaults: warning text lacks the --map hint"

# --quiet: no warnings.
rc=$(run --quiet "$GPX" "$TMP/o.fit")
[ "$rc" = 0 ] && ok || bad "--quiet: exit $rc, expected 0"
[ ! -s "$TMP/err" ] && ok || bad "--quiet: stderr not empty: $(cat "$TMP/err")"

# A user rule for Start removes that warning; Ranger Station still warns,
# and Water (default rule) still does not.
printf 'start = checkpoint\n' > "$TMP/user.map"
rc=$(run --map "$TMP/user.map" "$GPX" "$TMP/o.fit")
[ "$rc" = 0 ] && ok || bad "user map: exit $rc, expected 0"
has "no type for 'Start'" && bad "user map: Start still warns" || ok
has "no type for 'Ranger Station'" && ok || bad "user map: Ranger Station should still warn"

# Bad lines in the user map warn with file and line number, and do not abort.
printf 'start = checkpoint\nnot a rule\nfoo = banana\n' > "$TMP/bad.map"
rc=$(run --map "$TMP/bad.map" "$GPX" "$TMP/o.fit")
[ "$rc" = 0 ] && ok || bad "bad map lines: exit $rc, expected 0"
has "bad.map:2" && ok || bad "bad map lines: no warning for line 2"
has "bad.map:3" && ok || bad "bad map lines: no warning for line 3"
rc=$(run --quiet --map "$TMP/bad.map" "$GPX" "$TMP/o.fit")
[ ! -s "$TMP/err" ] && ok || bad "--quiet with bad map lines: stderr not empty"

if [ "$fail" -ne 0 ]; then
    echo "test_map.sh: $fail of $checks checks FAILED" >&2
    exit 1
fi
echo "test_map.sh: all $checks checks passed"
