# gpx2fit: Task Breakdown

## Tools & Agent Workflow

* **FIT Verification:** `tests/tools/FitCSVTool.jar` (run via `java -jar tests/tools/FitCSVTool.jar -b <file.fit> <out.csv>` to verify generated FIT messages).
* **Build & Test Commands:**
  * Native + ASan/UBSan unit & E2E tests: `make test`
  * WASM build (if Emscripten is installed): `make wasm` (or `emcc` target in `Makefile`)

* **Agentic Execution Rules (`Orchestrator` -> `@worker` -> `@reviewer`):**
  1. Execute tasks serially in order (`T1` through `T5`).
  2. **Token Efficiency Rule:** `@worker` must read **only** `course.h` (or core headers) and the exact files listed in the task's **Scope** block—do not scan unrelated files, grep across the whole repo unnecessarily, or re-read large fixtures.
  3. `@worker` must add unit/E2E tests for every behavioral change and run `make test` before reporting back.
  4. `@reviewer` inspects `git diff` against the task's **Acceptance Criteria** without modifying files and returns `PASS` or `FAIL`.

---

## Tasks

### T1: Multi-Track Natural Numerical Sorting & Independent Per-Track Maps, Snap Controls & Waypoint Toggles
* \[x\] **Sort tracks naturally (`"Day 1"` before `"Day 2"`) and render a separate map, snap distance control, and clickable waypoint list per track when not stitching**
  * **Scope:**
    * Read: WASM C bridge (`wasm.c` / `web/*.c`), `web/index.html`, `web/app.js`, `web/style.css`.
    * Modify: WASM C bridge (if track-level metadata/conversion is needed), `web/index.html`, `web/app.js`, `web/style.css`.
  * **Implementation Details:**
    * **Natural Numerical Track Ordering:** When a GPX contains multiple `<trk>` elements, sort tracks using natural numerical comparison (`localeCompare(b, undefined, { numeric: true, sensitivity: 'base' })` in JS, and matching natural order if exposed in C/WASM) so `"Day 1"`, `"Day 2"`, ..., `"Day 10"` appear in true numerical order rather than lexicographical (`"Day 1"`, `"Day 10"`, `"Day 2"`). Keep each track's original 1-based GPX `--track` index mapped so conversion targets the right track.
    * **Separate Maps, Per-Track Snap Distance & Clickable Waypoints When Unstitched:**
      * When **Stitch tracks** is **checked** (or for a single-track GPX): keep the unified single map, single snap distance control, and single waypoint table.
      * When **Stitch tracks** is **unchecked** and the GPX has multiple tracks:
        * Hide or replace the single combined view with a vertically stacked list of track/segment cards ordered numerically (`Day 1`, `Day 2`, ...).
        * Directly above each track's map container, render a dedicated header with the track name, track stats (distance, ascent/descent), a **Snap distance (m)** input/slider specific to that track (initialized from the default/global snap value), and a download `.fit` button for that track.
        * Render an independent Leaflet/map instance for each track showing that track's polyline and its snapped/skipped waypoints (respecting that track's snap distance and per-track waypoint status overrides).
        * Beneath (or attached to) each track's map, render that track/segment's own **waypoints table** displaying each waypoint's distance, offset, icon type, and clickable **Status** badge (`Snapped` / `Skipped`) using the same toggle behavior as **T2**—scoped per `(trackIndex, waypointIndex)` so overriding a waypoint on `"Day 1"` only affects `"Day 1"`'s map and `.fit` export without altering `"Day 2"`.
        * Properly destroy/clean up old map instances when switching between stitched/unstitched modes or loading a new GPX file to prevent memory leaks or blank map tiles (`invalidateSize()`).
  * **Verification:** Build WASM (`make wasm` if available) and run `make test`; test with a multi-track GPX containing tracks named `"Day 10"`, `"Day 2"`, `"Day 1"` in both stitched and unstitched modes, verifying per-segment waypoint lists and clickable status overrides.
  * **Acceptance Criteria:**
    * Tracks named `"Day 1"`, `"Day 2"`, `"Day 10"` display in numerical order (`1, 2, 10`).
    * When stitching is disabled on a multi-track GPX, each track renders its own independent map, its own snap distance control directly above its map, and its own waypoint table with clickable `Snapped`/`Skipped` status badges.
    * Adjusting snap distance or toggling a waypoint's `Snapped`/`Skipped` status on Track 1 updates only Track 1's map, waypoint statuses, and `.fit` output without affecting Track 2.

---

### T2: Diagnose and fix per-segment (unstitched) maps in the Web/WASM UI (follow-up to T3 regression)
* \[ \] **Identify why each unstitched per-track Leaflet map is over-zoomed, off-center, and missing tiles, then apply a minimal fix with regression tests**
  * **Scope:**
    * Read (only): map/render code in `web/app.js` (`getMapInstance`, `releaseMapInstance`, `renderRouteMap`, `renderWaypointTable`, `showResults`, `buildTrackCard`, `renderTrackCards`, `renderTrackCardResult`, `runConversion`, `requestConversion`, `loadSource`, `invalidateOutput`), map markup in `web/index.html`, map/card CSS in `web/style.css`, the fake-Leaflet harness in `tests/test_web.mjs`, and at most one multi-track fixture (`tests/data/multi_track.gpx` or `web/Rio_Del_Lago_100mi_2025.gpx`). Do not scan unrelated modules or re-read large reference fixtures.
    * Modify: `web/app.js`; `web/style.css` and `tests/test_web.mjs` (or a new `tests/test_*.mjs`) as needed; optionally `tests/test_multi_track_web.sh`.
  * **Implementation Details (diagnose first, report evidence before changing behavior):**
    1. Compare the working stitched path (`showResults` → `renderRouteMap(mapElement)` with visible `#route-map`) against the broken unstitched path (`renderTrackCards` → `buildTrackCard` → `renderTrackCardResult` → `renderRouteMap(mapForCard)`). Instrument `leaflet.map`, `invalidateSize`, `fitBounds`, and tile-layer creation to log, for each container: `hidden` state of the container **and all ancestors** (`#track-list`, `#results`), `offsetWidth`/`offsetHeight`/`getBoundingClientRect()`, track-point count, computed bounds, resulting zoom/center, and tile request status (including any HTTP 429/403).
    2. Test the leading hypothesis: `renderTrackCards` appends cards and calls `renderTrackCardResult` (which creates the Leaflet map and calls `invalidateSize()`/`fitBounds()`) **while `#track-list` is still `hidden`**, then sets `trackList.hidden = false` only after the loop. Leaflet would measure a 0×0 container, fit to meaningless dimensions, and never refit because `mappedSourceByElement` records the source as already fitted and later per-track updates deliberately skip `fitBounds`. Confirm or refute with logged sizes/zoom/center; check whether `invalidateOutput()` leaving `#results` hidden adds a second hidden ancestor on full reconverts.
    3. Rule out data/viewport alternatives: verify `metadata.track_points_list` for `stitch=false, track=N` contains the expected lat/lon array for that track (non-degenerate bounds) and that `unwrapTrackCoordinates` does not distort per-track bounds; verify marker coordinates land inside the fitted bounds. Check whether missing tiles are merely a consequence of the wrong zoom/center/viewport or a separate issue (OSM tile throttling from many simultaneous map instances, tile URL/connection limits), using observed tile request counts and statuses.
    4. After identifying the root cause(s), implement the smallest fix in `web/app.js`: e.g. reveal `#results`/`#track-list` and let layout settle before instantiating/fitting per-track maps (or defer `invalidateSize()` + `fitBounds()` with `requestAnimationFrame` after visibility), and make the fit-state guard account for a container that was unmeasurable at first render. Ensure mode switches, new GPX loads, snap changes, and waypoint toggles still clean up old maps (`releaseMapInstance`) and leave every visible map correctly sized and fitted. Only change tile configuration if step 3 proves it is an independent cause.
    5. Add regression coverage in the JS harness: simulate hidden ancestors/zero-size containers, assert that each per-track map gets a non-zero-size `invalidateSize()` and a `fitBounds()` over its own track's bounds after becoming visible; assert distinct per-track bounds for `Day 1`/`Day 2`, correct behavior when switching stitched ↔ unstitched and after a per-track snap/toggle update, and no `fitBounds` on a still-hidden container.
  * **Verification:** Reproduce the bad zoom/center with the harness instrumentation (and a real browser/devtools if available) before the fix and capture the same metrics after. Run `make test` (includes `tests/test_web.mjs` and `test_multi_track_web.sh`) and `make wasm` if Emscripten is installed; review `git diff` for scope.
  * **Acceptance Criteria:**
    * Root cause documented with concrete evidence (container sizes, fitted bounds, zoom/center, tile statuses) and the stitched-vs-unstitched difference explained.
    * In unstitched multi-track mode every segment map is centered on its own track at a sensible zoom comparable to the stitched map, with all expected tiles loading; maps remain correct after stitched/unstitched switches, new GPX loads, per-track snap changes, and waypoint status toggles.
    * Regression tests fail against the current code and pass after the fix; `make test` passes under ASan/UBSan and no stale/blank map instances remain.


---

# Optional 

### T3: Turn-by-Turn Cue Generation from Track Geometry (Core C + CLI)
* \[ \] **Generate Garmin turn course points (`left`, `right`, `slight_left`, `slight_right`, `sharp_left`, `sharp_right`, `u_turn`) from track bearing changes**
  * **Scope:**
    * Read: `course.h`, geometry/distance math (`track.c` / `course.c`), `main.c`, `README.md`, `tests/`.
    * Modify: `course.h`, new or existing geometry module, `main.c`, `README.md`, `tests/`.
  * **Implementation Details:**
    * Add CLI flag `--turns` (and optional `--turn-angle DEG` threshold, default e.g. `35` degrees, off by default unless `--turns` is passed so existing waypoint-only workflows remain unchanged).
    * Compute smoothed incoming and outgoing bearings over a lookback/lookahead chord window (e.g., 15–30 m along the track) to filter out GPS jitter on switchbacks and noisy trail points.
    * Classify heading deltas ($\Delta\theta$) into FIT `course_point` types already supported by the codebase:
      * `slight_left` / `slight_right` (e.g., $35^\circ \le \vert{}\Delta\theta\vert{} < 65^\circ$)
      * `left` / `right` (e.g., $65^\circ \le \vert{}\Delta\theta\vert{} < 120^\circ$)
      * `sharp_left` / `sharp_right` (e.g., $120^\circ \le \vert{}\Delta\theta\vert{} < 160^\circ$)
      * `u_turn` (e.g., $\vert{}\Delta\theta\vert{} \ge 160^\circ$)
    * Enforce a minimum spacing between generated turn cues (e.g., 30–50 m) and suppress auto-generated turns within `--snap` (or 30 m) of an existing user waypoint so "Up Ahead" isn't cluttered.
    * Give generated turn points concise names (`"Left"`, `"Sharp right"`, `"U-turn"`, etc., well under the 15-byte limit) and ensure `--max-points` preserves trackpoints near generated turn cues.
  * **Verification:** Add unit tests with synthetic L-turn, hairpin/U-turn, slight-bend, and noisy straight-line tracks verifying exact turn types, distances, noise immunity, and waypoint deduplication. Verify generated FIT output with `FitCSVTool.jar` and run `make test`.
  * **Acceptance Criteria:**
    * `--turns` generates accurate FIT turn course points from track geometry without firing on minor GPS jitter.
    * Auto-turns do not duplicate existing waypoints at the same junction.
    * `make test` passes under ASan/UBSan.

---

### T4: Expose Turn-by-Turn Cue Generation in Web/WASM UI & Update Docs
* \[ \] **Add Turn-by-Turn cues toggle in Web/WASM UI and wire it to the C turn generator**
  * **Scope:**
    * Read: WASM C bridge (`wasm.c` / `web/*.c`), `web/index.html`, `web/app.js`, `README.md`.
    * Modify: WASM C bridge, `web/index.html`, `web/app.js`, `README.md`.
  * **Implementation Details:**
    * Add a checkbox/toggle in `web/index.html` (e.g., `Generate turn-by-turn cues` in Options / Advanced Options).
    * Pass the `turns` flag into the WASM conversion call and display generated turn course points in the web preview/summary table alongside user waypoints.
    * Update `README.md` with documentation for `--turns` and the updated Web/WASM features.
  * **Verification:** Build WASM target if Emscripten is installed; run `make test`; review `git diff` across all modified files for consistency.
  * **Acceptance Criteria:**
    * Users can enable/disable turn-by-turn cue generation in the Web/WASM UI.
    * Generated turn cues appear in the web course point summary and in the downloaded `.fit` file.
    * `README.md` accurately documents all new CLI flags and Web UI options.
