#!/usr/bin/env bash
# T4: built-in default.map rules in the Web/WASM Advanced Options popover.
# Usage: tests/test_web_default_map.sh [path/to/gpx2fit]
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail=0
expect() {
    local desc=$1; shift
    if "$@"; then
        echo "ok   - $desc"
    else
        echo "FAIL - $desc"; fail=1
    fi
}

INDEX="$ROOT/web/index.html"
APP="$ROOT/web/app.js"
CSS="$ROOT/web/style.css"
MAKEFILE="$ROOT/Makefile"
BRIDGE="$ROOT/src/wasm_api.c"

expect "info button has the required accessible label" \
    grep -Fq 'aria-label="View built-in waypoint mapping rules"' "$INDEX"
expect "Advanced Options popover is wired to the info button" \
    grep -Fq 'aria-controls="default-map-popover"' "$INDEX"
expect "popover explains custom-rule precedence" \
    grep -Fq 'Custom rules take precedence over these defaults.' "$INDEX"
expect "popover content is read from the WASM getter" \
    grep -Fq '_gpx2fit_get_default_map' "$APP"
expect "no hardcoded default.map rules are duplicated in the web app" \
    bash -c "! grep -Fq 'water   = water' '$APP'"
expect "WASM build exports the default-map getter" \
    grep -Fq '_gpx2fit_get_default_map' "$MAKEFILE"
expect "C bridge returns the embedded default map" \
    grep -Fq 'EMSCRIPTEN_KEEPALIVE const char *gpx2fit_get_default_map' "$BRIDGE"
expect "popover is styled for desktop and mobile" \
    grep -Fq '.default-map-popover' "$CSS"

# T1: custom rule edits are debounced and rendered without moving the viewport.
expect "custom rules use an 800 ms debounce" \
    grep -Fq 'MAP_RULES_DEBOUNCE_MS = 800' "$APP"
expect "rules textarea has a stable container in the markup" \
    grep -Fq 'id="map-rules-field"' "$INDEX"
expect "viewport and focus are preserved around re-renders" \
    grep -Fq 'withStableViewport' "$APP"
expect "no scrollIntoView side effects during background updates" \
    bash -c "! grep -Fq 'scrollIntoView' '$APP'"
expect "preview and warning containers reserve layout height" \
    bash -c "grep -Fq '.results {' '$CSS' && grep -Fq '.warnings {' '$CSS' && grep -Fq 'min-height' '$CSS'"

# T2: all remaining live controls share the same debounce/stable-render path.
expect "live inputs share the custom-rules 800 ms debounce" \
    grep -Fq 'export const INPUT_DEBOUNCE_MS = MAP_RULES_DEBOUNCE_MS;' "$APP"
expect "the shared timer uses the 800 ms delay" \
    grep -Fq 'convertTimer = win.setTimeout(run, INPUT_DEBOUNCE_MS);' "$APP"
all_debounced=1
for id in courseNameInput paceInput maxPointsInput startTimeInput; do
    grep -Fq "$id.addEventListener('input', () => scheduleInputConversion())" "$APP" \
        || all_debounced=0
done
expect "text/numeric inputs debounce input events" test "$all_debounced" -eq 1
all_immediate=1
for id in courseNameInput paceInput maxPointsInput startTimeInput; do
    grep -Fq "$id.addEventListener('change', () => applyInputConversion())" "$APP" \
        || all_immediate=0
done
expect "snap controls use the debounced/immediate helpers" \
    bash -c "grep -A5 -F \"snapSlider.addEventListener('input'\" '$APP' | grep -Fq 'scheduleInputConversion();' && grep -A5 -F \"snapInput.addEventListener('input'\" '$APP' | grep -Fq 'scheduleInputConversion();' && grep -A5 -F \"snapSlider.addEventListener('change'\" '$APP' | grep -Fq 'applyInputConversion();' && grep -A5 -F \"snapInput.addEventListener('change'\" '$APP' | grep -Fq 'applyInputConversion();'"
expect "text/numeric change events convert immediately" test "$all_immediate" -eq 1
all_options=1
for id in paceUnitSelect activitySelect passModeSelect strictModeCheckbox stitchCheckbox; do
    grep -Fq "$id.addEventListener('change', () => applyInputConversion())" "$APP" \
        || all_options=0
done
expect "selects and checkboxes use the stable immediate path" test "$all_options" -eq 1
expect "sliders and per-track numbers use the shared helpers" \
    bash -c "grep -Fq \"snapSlider.addEventListener('input', () => {\" '$APP' && grep -Fq 'scheduleInputConversion({ track: track.index, snap: value })' '$APP' && grep -Fq 'applyInputConversion({ track: track.index, snap: value })' '$APP'"
expect "no live input bypasses the debounce helper" \
    bash -c "! grep -Fq \"addEventListener('input', () => requestConversion()\" '$APP'"

# The behavior test imports the generated WASM bundle via app.js. Follow the
# existing web tests: only run it when Node and a built bundle are available.
if ! command -v node >/dev/null 2>&1; then
    echo "SKIP - node is not installed; browser popover behavior test skipped"
elif [ ! -f "$ROOT/web/dist/gpx2fit.js" ]; then
    echo "SKIP - web/dist/gpx2fit.js is not built; run make wasm to enable the browser popover test"
else
    if node "$ROOT/tests/test_web_default_map.mjs"; then
        echo "ok   - browser popover behavior test"
    else
        echo "FAIL - browser popover behavior test"; fail=1
    fi
fi

exit "$fail"
