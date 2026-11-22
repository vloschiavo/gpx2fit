#!/usr/bin/env bash
# test_time_opts.sh: end-to-end checks for --pace and --start (tasks 6.1, 6.2).
# Usage: bash tests/test_time_opts.sh ./gpx2fit   (run from the repository root)
# Bad values are usage errors (exit 2) reported before any work is done.
# The times themselves are covered by tests/test_timing.c; they can only be
# seen from outside once the FIT writer exists (phase 7/8).
set -u

BIN=${1:-./gpx2fit}
GPX=tests/data/caltopo_sample.gpx
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
checks=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); fail=$((fail + 1)); echo "FAIL: $1" >&2; }

run() { "$BIN" --quiet "$@" "$GPX" "$TMP/o.fit" >"$TMP/out" 2>"$TMP/err"; echo $?; }

# accepts OPT VALUE: must exit 0.
accepts() {
    local rc; rc=$(run "$1" "$2")
    [ "$rc" = 0 ] && ok || bad "$1 '$2': exit $rc, expected 0 ($(head -c 200 "$TMP/err"))"
}
# rejects OPT VALUE: must exit 2 and name the option and the value on stderr.
rejects() {
    local rc; rc=$(run "$1" "$2")
    [ "$rc" = 2 ] && ok || bad "$1 '$2': exit $rc, expected 2"
    grep -qF -- "invalid $1 value '$2'" "$TMP/err" && ok \
        || bad "$1 '$2': message does not name the option and value: $(cat "$TMP/err")"
}

# Defaults (no option) work.
rc=$("$BIN" --quiet "$GPX" "$TMP/o.fit" >/dev/null 2>"$TMP/err"; echo $?)
[ "$rc" = 0 ] && ok || bad "defaults: exit $rc, expected 0"

# Valid pace: m/s and min/km.
accepts --pace 2.5
accepts --pace 1
accepts --pace 6:40
accepts --pace 12:05

# Invalid pace.
rejects --pace 0
rejects --pace 0:00
rejects --pace -2
rejects --pace fast
rejects --pace 6:75
rejects --pace 6:5
rejects --pace 1e3

# Valid start: UTC only.
accepts --start 2025-06-28T08:00:00Z
accepts --start 2025-06-28T08:00
accepts --start 2025-06-28
accepts --start 2024-02-29T12:00:00Z

# Invalid start.
rejects --start 2025-13-01
rejects --start 2025-02-29
rejects --start 2025-06-28T08:00:00+02:00
rejects --start 2025-06-28T25:00:00Z
rejects --start 1989-12-31
rejects --start 2200-01-01
rejects --start yesterday

# The offset message explains what to do instead.
run --start 2025-06-28T08:00:00+02:00 >/dev/null
grep -q "UTC" "$TMP/err" && ok || bad "offset rejection does not mention UTC"

# Both options together, one bad: still a usage error.
rc=$("$BIN" --quiet --pace 6:40 --start nope "$GPX" "$TMP/o.fit" >/dev/null 2>&1; echo $?)
[ "$rc" = 2 ] && ok || bad "good --pace with bad --start: exit $rc, expected 2"

# --help documents both defaults.
"$BIN" --help >"$TMP/out" 2>&1
grep -q "default 5 km/h" "$TMP/out" && ok || bad "--help does not mention the default pace"
grep -q "2025-01-01T00:00:00Z" "$TMP/out" && ok || bad "--help does not mention the default start"

if [ "$fail" -ne 0 ]; then
    echo "test_time_opts.sh: $fail of $checks checks FAILED" >&2
    exit 1
fi
echo "test_time_opts.sh: all $checks checks passed"
