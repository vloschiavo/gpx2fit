#!/usr/bin/env bash
# Service worker / build stamping tests (task 15.4).
# Usage: tests/test_web_update.sh [path/to/gpx2fit] (the binary is not needed).
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
fail=0

expect() {
    local desc=$1; shift
    if "$@"; then
        echo "ok   - $desc"
    else
        echo "FAIL - $desc"; fail=1
    fi
}

json_field() {
    local file=$1 field=$2
    if command -v node >/dev/null 2>&1; then
        node -e 'const v=require(process.argv[1]); process.stdout.write(String(v[process.argv[2]] ?? ""))' \
            "$file" "$field"
    else
        sed -n "s/.*\"$field\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$file" | head -1
    fi
}

INDEX="$ROOT/web/index.html"
SW="$ROOT/web/sw.js"
VERSION="$ROOT/web/version.json"
APP="$ROOT/web/app.js"
CSS="$ROOT/web/style.css"
# The canonical version comes from the same source as `gpx2fit --version`.
PROJECT_VERSION=$(make -s -C "$ROOT" version)

expect "version.json is valid JSON with a build id" \
    test -n "$(json_field "$VERSION" build_id)"
expect "checked-in page and worker agree on the build id" \
    grep -Fq "content=\"$(json_field "$VERSION" build_id)\"" "$INDEX"
expect "checked-in worker carries the matching BUILD_ID" \
    grep -Fq "const BUILD_ID = '$(json_field "$VERSION" build_id)';" "$SW"
expect "checked-in worker carries the matching cache version" \
    grep -Fq "const CACHE_NAME = 'gpx2fit-$(json_field "$VERSION" build_id)';" "$SW"

expect "worker accepts SKIP_WAITING only via message" \
    grep -Fq "data.type === 'SKIP_WAITING'" "$SW"
expect "worker calls skipWaiting on request" \
    grep -Fq 'self.skipWaiting()' "$SW"
expect "worker claims clients on activation" \
    grep -Fq 'self.clients.claim()' "$SW"
expect "worker fetches version.json with cache: no-store" \
    grep -Fq "cache: 'no-store'" "$SW"
expect "worker precaches the app shell and wasm" \
    grep -Fq "'./dist/gpx2fit.wasm'" "$SW"
expect "worker precaches index and styles" \
    grep -Fq "'./index.html'" "$SW"
expect "worker keeps the CDN out of the precache" \
    bash -c "! grep -Fq 'cdn.jsdelivr.net' '$SW'"

expect "detector uses cache: no-store" \
    grep -Fq "cache: 'no-store'" "$APP"
expect "detector checks on load, focus, visibility and interval" \
    bash -c "grep -Fq 'visibilitychange' '$APP' && grep -Fq 'setInterval' '$APP' && grep -Fq 'checkVersion(); // Page load' '$APP'"
expect "detector posts the update banner message" \
    grep -Fq 'A new version of gpx2fit is available.' "$APP"
expect "detector requests worker activation" \
    grep -Fq "type: 'SKIP_WAITING'" "$APP"
expect "page exposes the update banner and Reload button" \
    bash -c "grep -Fq 'id=\"update-banner\"' '$INDEX' && grep -Fq 'id=\"update-reload\"' '$INDEX'"
expect "pass mode selector exposes --pass both" \
    grep -Fq '<option value="both">Both passes' "$INDEX"
expect "web app accepts the both pass mode" \
    grep -Fq "new Set(['nearest', 'ordered', 'first', 'last', 'both'])" "$APP"

expect "footer renders the canonical version label" \
    grep -Fq "<span id=\"gpx2fit-version\" class=\"version-label\">$PROJECT_VERSION</span>" "$INDEX"
expect "version label sits at the bottom of the page" \
    bash -c "awk '/<footer>/,/<\/footer>/' '$INDEX' | grep -Fq 'id=\"gpx2fit-version\"'"
expect "version label is styled small and muted for desktop and mobile" \
    bash -c "grep -Fq '.version-label {' '$CSS' && awk '/@media \(max-width: 36rem\)/,0' '$CSS' | grep -Fq '.version-label {'"

# Stamping must work from a copy, without dirtying the checked-in web files.
before=$(cksum "$VERSION" "$SW" "$INDEX")
mkdir -p "$TMP/web" "$TMP/src"
cp "$ROOT/Makefile" "$TMP/"
cp "$ROOT/src/version.h" "$TMP/src/"
cp "$VERSION" "$SW" "$INDEX" "$TMP/web/"

stamp() {
    make -C "$TMP" -f Makefile web-version \
        WEB_BUILD_ID="$1" WEB_SHA="$2" WEB_BUILD_TIME="$3" >"$TMP/make.log" 2>&1
}

if stamp stamp-one abc123 20260101T000000Z; then
    echo "ok   - make web-version stamps a build"
else
    echo "FAIL - make web-version stamps a build"; cat "$TMP/make.log"; fail=1
fi
expect "stamp updates version.json" \
    bash -c "grep -Fq '\"build_id\": \"stamp-one\"' '$TMP/web/version.json' && grep -Fq '\"sha\": \"abc123\"' '$TMP/web/version.json' && grep -Fq '\"built_at\": \"20260101T000000Z\"' '$TMP/web/version.json'"
expect "stamp updates worker build id and cache name" \
    bash -c "grep -Fq \"const BUILD_ID = 'stamp-one';\" '$TMP/web/sw.js' && grep -Fq \"const CACHE_NAME = 'gpx2fit-stamp-one';\" '$TMP/web/sw.js'"
expect "stamp updates the page build id" \
    grep -Fq 'content="stamp-one"' "$TMP/web/index.html"
expect "stamp keeps the footer version from src/version.h" \
    grep -Fq "<span id=\"gpx2fit-version\" class=\"version-label\">$PROJECT_VERSION</span>" "$TMP/web/index.html"

if stamp stamp-two abc124 20260101T000001Z; then
    echo "ok   - a second stamp with new sha/timestamp produces a new id"
else
    echo "FAIL - a second stamp with new sha/timestamp produces a new id"; cat "$TMP/make.log"; fail=1
fi
expect "second stamp replaces the previous build id everywhere" \
    bash -c "grep -Fq '\"build_id\": \"stamp-two\"' '$TMP/web/version.json' && grep -Fq \"const CACHE_NAME = 'gpx2fit-stamp-two';\" '$TMP/web/sw.js' && grep -Fq 'content=\"stamp-two\"' '$TMP/web/index.html' && ! grep -Fq 'stamp-one' '$TMP/web/version.json' '$TMP/web/sw.js' '$TMP/web/index.html'"

if stamp 'bad id' abc125 20260101T000002Z 2>/dev/null; then
    echo "FAIL - unsafe build ids are rejected"; fail=1
else
    echo "ok   - unsafe build ids are rejected"
fi
expect "rejected stamp leaves files untouched" \
    bash -c "! grep -Fq 'bad id' '$TMP/web/version.json' '$TMP/web/sw.js' '$TMP/web/index.html'"

# Regression: $(shell date ...) must be evaluated once per make invocation.
# A clock that changes on every lookup would otherwise stamp three different
# ids into the three artifacts (recursive ?= expansion).
mkdir -p "$TMP/vary/bin" "$TMP/vary/web" "$TMP/vary/src"
cp "$ROOT/Makefile" "$TMP/vary/"
cp "$ROOT/src/version.h" "$TMP/vary/src/"
cp "$VERSION" "$SW" "$INDEX" "$TMP/vary/web/"
cat >"$TMP/vary/bin/date" <<'EOF'
#!/bin/sh
n=$(cat "$VARY_COUNTER" 2>/dev/null || echo 0)
n=$((n + 1))
printf '%s\n' "$n" > "$VARY_COUNTER"
printf 'varying%s\n' "$n"
EOF
chmod +x "$TMP/vary/bin/date"
if VARY_COUNTER="$TMP/vary/count" PATH="$TMP/vary/bin:$PATH" \
    make -C "$TMP/vary" -f Makefile web-version WEB_SHA=abc123 >"$TMP/vary.log" 2>&1; then
    echo "ok   - stamp succeeds with a varying clock"
else
    echo "FAIL - stamp succeeds with a varying clock"; cat "$TMP/vary.log"; fail=1
fi
vary_id=$(json_field "$TMP/vary/web/version.json" build_id)
expect "varying clock is sampled once" test "$vary_id" = "abc123-varying1"
expect "varying clock keeps worker BUILD_ID in sync" \
    grep -Fq "const BUILD_ID = '$vary_id';" "$TMP/vary/web/sw.js"
expect "varying clock keeps worker cache name in sync" \
    grep -Fq "const CACHE_NAME = 'gpx2fit-$vary_id';" "$TMP/vary/web/sw.js"
expect "varying clock keeps page meta in sync" \
    grep -Fq "content=\"$vary_id\"" "$TMP/vary/web/index.html"

# The footer version must follow the canonical header, not a hardcoded copy.
mkdir -p "$TMP/ver/web" "$TMP/ver/src"
cp "$ROOT/Makefile" "$TMP/ver/"
printf '#define GPX2FIT_VERSION "v9.8.7"\n' > "$TMP/ver/src/version.h"
cp "$VERSION" "$SW" "$INDEX" "$TMP/ver/web/"
if make -C "$TMP/ver" -f Makefile web-version WEB_BUILD_ID=ver-test >"$TMP/ver.log" 2>&1; then
    echo "ok   - stamping reads the version from src/version.h"
else
    echo "FAIL - stamping reads the version from src/version.h"; cat "$TMP/ver.log"; fail=1
fi
expect "footer version follows src/version.h" \
    grep -Fq '<span id="gpx2fit-version" class="version-label">v9.8.7</span>' "$TMP/ver/web/index.html"

after=$(cksum "$VERSION" "$SW" "$INDEX")
expect "test did not modify the checked-in web files" test "$before" = "$after"

if command -v node >/dev/null 2>&1; then
    if [ -f "$ROOT/web/dist/gpx2fit.js" ]; then
        if node "$ROOT/tests/test_web_update.mjs"; then
            echo "ok   - browser-side update detector checks"
        else
            echo "FAIL - browser-side update detector checks"; fail=1
        fi

        # The .mjs assertions derive the build id from version.json. Re-run
        # them against a stamped temp copy to prove they survive stamping;
        # the checked-in files are never modified.
        mkdir -p "$TMP/mjs/web" "$TMP/mjs/tests"
        cp "$TMP/web/index.html" "$TMP/web/sw.js" "$TMP/web/version.json" \
            "$ROOT/web/app.js" "$ROOT/web/style.css" "$TMP/mjs/web/"
        cp -R "$ROOT/web/dist" "$TMP/mjs/web/dist"
        cp "$ROOT/tests/test_web_update.mjs" "$TMP/mjs/tests/"
        if node "$TMP/mjs/tests/test_web_update.mjs"; then
            echo "ok   - detector checks pass after stamping a temp copy"
        else
            echo "FAIL - detector checks pass after stamping a temp copy"; fail=1
        fi
    else
        echo "skip - browser-side detector checks need web/dist/gpx2fit.js (make wasm)"
    fi
else
    echo "skip - browser-side detector checks need node"
fi

exit "$fail"
