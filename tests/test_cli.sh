#!/usr/bin/env bash
# CLI parsing tests (task 1.4). Usage: tests/test_cli.sh [path/to/gpx2fit]
BIN=${1:-./gpx2fit}
fail=0

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
expect_rc "minimal valid args"               0 in.gpx out.fit
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
expect_out "--quiet"             "quiet=1"         --quiet in.gpx out.fit
expect_out "-q"                  "quiet=1"         -q in.gpx out.fit
expect_out "--strict"            "strict=1"        --strict in.gpx out.fit
expect_out "options after positionals" "snap=40"   in.gpx out.fit --snap 40
expect_out "--opt=value form"    "snap=7"          --snap=7 in.gpx out.fit

exit $fail
