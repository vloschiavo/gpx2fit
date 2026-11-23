#!/usr/bin/env bash
# test_fit_decode.sh: decode the fixture FIT files written by test_fit with the
# Garmin SDK tool (FitCSVTool.jar) and diff the result against expected values.
#
# Needs: java and FitCSVTool.jar (tools/FitCSVTool.jar, or set FITCSVTOOL=path;
# https://github.com/garmin/fit-sdk-tools/releases). Without them the script
# prints SKIP and exits 0. If python3 has the garmin-fit-sdk package
# (pip install garmin-fit-sdk) its integrity check runs too.
#
#   bash tests/test_fit_decode.sh [ignored: path of gpx2fit, passed by make]
#   UPDATE_EXPECTED=1 bash tests/test_fit_decode.sh   # rewrite the golden file
#
# Fixtures come from tests/test_fit.c ($FIT_TEST_OUT, default build-test/).
set -euo pipefail

cd "$(dirname "$0")/.."
out=${FIT_TEST_OUT:-build-test}
expected=tests/data/fit_fixture_main.expected
jar=${FITCSVTOOL:-}

if [ -z "$jar" ]; then
    for c in tools/FitCSVTool.jar tests/tools/FitCSVTool.jar; do
        if [ -f "$c" ]; then jar=$c; break; fi
    done
fi
if ! command -v java >/dev/null 2>&1 || [ -z "$jar" ] || [ ! -f "$jar" ]; then
    echo "SKIP: java or FitCSVTool.jar not found (set FITCSVTOOL=path)"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0

# decode NAME: FIT -> $tmp/NAME.csv; fail on any tool message about problems.
decode() {
    local name=$1 fit="$out/fit_fixture_$1.fit"
    if [ ! -f "$fit" ]; then
        echo "FAIL: $fit missing (run build-test/test_fit first)"
        return 1
    fi
    java -jar "$jar" -b "$fit" "$tmp/$name.csv" -i >"$tmp/$name.log" 2>&1 || true
    if grep -Ei 'error|warning|problem' "$tmp/$name.log"; then
        echo "FAIL: decoder reported problems for $fit"
        return 1
    fi
    [ -s "$tmp/$name.csv" ] || { echo "FAIL: no CSV for $fit"; return 1; }
}

# normalize CSV: one line per data message, "message field=value ...", in file
# order. Derived 'enhanced_*' fields and units are left out.
normalize() {
    awk -F, '$1 == "Data" {
        line = $3
        for (i = 4; i + 1 <= NF; i += 3) {
            k = $i; v = $(i + 1)
            if (k == "" || k ~ /^enhanced_/) continue
            gsub(/"/, "", v)
            line = line " " k "=" v
        }
        print line
    }' "$1"
}

for f in main noele; do
    decode "$f" || fail=1
done

if [ "$fail" -eq 0 ]; then
    normalize "$tmp/main.csv" >"$tmp/main.txt"
    if [ "${UPDATE_EXPECTED:-0}" = 1 ]; then
        cp "$tmp/main.txt" "$expected"
        echo "wrote $expected"
    elif ! diff -u "$expected" "$tmp/main.txt"; then
        echo "FAIL: decoded fixture differs from $expected"
        fail=1
    fi

    # No elevation: no altitude, ascent or descent values at all (invalid
    # values are not shown by the tool), and the other fields still decode.
    normalize "$tmp/noele.csv" >"$tmp/noele.txt"
    if grep -E 'altitude=|total_ascent=|total_descent=' "$tmp/noele.txt"; then
        echo "FAIL: noele fixture contains elevation values"
        fail=1
    fi
    [ "$(grep -c '^record ' "$tmp/noele.txt")" -eq 11 ] \
        || { echo "FAIL: noele fixture should have 11 records"; fail=1; }
fi

# Optional second opinion: header, sizes and both CRCs via the Python SDK.
if python3 -c 'import garmin_fit_sdk' >/dev/null 2>&1; then
    for f in main noele; do
        python3 - "$out/fit_fixture_$f.fit" <<'EOF' || fail=1
import sys
from garmin_fit_sdk import Decoder, Stream
d = Decoder(Stream.from_file(sys.argv[1]))
if not (d.is_fit() and d.check_integrity()):
    print("FAIL: Python SDK integrity check failed for", sys.argv[1])
    sys.exit(1)
_, errors = Decoder(Stream.from_file(sys.argv[1])).read()
if errors:
    print("FAIL:", errors)
    sys.exit(1)
EOF
    done
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "test_fit_decode: fixtures decode cleanly and match $expected"
