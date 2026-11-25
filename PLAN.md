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

### T1: Add Default Map Rules (`creek`, `lake`, `river`, `spring`, `market`)
- [x] **Add new default keyword mappings to `data/default.map` and verify `shopping` FIT type support**
  * **Scope:**
    * Read: `data/default.map`, `symmap.c` (or course-point type lookup table), `README.md`, `tests/`.
    * Modify: `data/default.map`, `symmap.c` (only if `shopping` is not yet mapped to its Garmin FIT `course_point` enum), `README.md`, `tests/`.
  * **Implementation Details:**
    * Append/insert the following rules in `data/default.map` (ensuring no duplicate keywords exist):
      * `creek = water`
      * `lake = water`
      * `river = water`
      * `spring = water`
      * `market = shopping`
    * Confirm `shopping` is recognized as a valid FIT `course_point` type in the C type parser (verify exact FIT SDK enum value if not already present; check `README.md` rule format list as well).
    * Update `README.md` (`Built-in defaults` and accepted `course_point` type list) to reflect the new rules.
  * **Verification:** Run `make test` and add unit tests verifying `"Bear Creek"`, `"Mirror Lake"`, `"American River"`, `"Hot Spring"`, and `"Trailhead Market"` resolve to `water` and `shopping` without warnings.
  * **Acceptance Criteria:**
    * `creek`, `lake`, `river`, and `spring` map to `water`, and `market` maps to `shopping` in `data/default.map`.
    * `shopping` encodes cleanly into the `.fit` file (verified with `FitCSVTool.jar` if newly added to the C enum table).
    * `make test` passes under ASan/UBSan.

---

### T2: Clickable Waypoint Status Toggle (Force Snapped vs. Force Skipped)
- [x] **Allow clicking each waypoint's Status field in the Web UI to toggle `Snapped` vs. `Skipped`, overriding `--snap` distance**
  * **Scope:**
    * Read: `course.h`, `snap.c` / `course.c`, WASM C bridge (`wasm.c` / `web/*.c`), `web/index.html`, `web/app.js`, `web/style.css`.
    * Modify: `course.h` / `snap.c` (if per-waypoint override is passed to C/WASM), WASM C bridge, `web/app.js`, `web/style.css`.
  * **Implementation Details:**
    * Track per-waypoint manual overrides in Web UI state (e.g., `auto` | `force_snap` | `force_skip`, keyed by waypoint index/ID, reset when a new GPX file is loaded).
    * Render the **Status** cell/badge (`Snapped` / `Skipped`) in the waypoint table as an interactive button/clickable pill with clear visual affordance (hover state, cursor pointer, `aria-pressed` or accessible label, and a subtle indicator if manually overridden).
    * Clicking a `Skipped` waypoint forces it to **Snapped** (projecting it onto the nearest track point regardless of how far beyond `--snap` it lies).
    * Clicking a `Snapped` waypoint forces it to **Skipped** (excluding it from the preview map course points and the generated `.fit` file).
    * Pass waypoint override decisions into the WASM conversion call (or filter/force-snap in the C snapping layer via an optional per-waypoint override array/JSON field) so both the live preview map/stats and the downloaded `.fit` file reflect the manual overrides immediately.
  * **Verification:** Build WASM (`make wasm` if available) and run `make test`; add a unit/WASM test verifying a waypoint > `--snap` meters is included when force-snapped and a waypoint < `--snap` meters is excluded when force-skipped.
  * **Acceptance Criteria:**
    * Clicking any waypoint's Status badge toggles it between `Snapped` and `Skipped`, overriding the global/track `--snap` threshold.
    * Toggling immediately updates the map markers, course point count, and exported `.fit` binary.
    * Changing `--snap` respects manually overridden waypoints while still updating non-overridden waypoints.

---

### T3: Multi-Track Natural Numerical Sorting & Independent Per-Track Maps, Snap Controls & Waypoint Toggles
* \[ \] **Sort tracks naturally (`"Day 1"` before `"Day 2"`) and render a separate map, snap distance control, and clickable waypoint list per track when not stitching**
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

# Optional 

### T4: Turn-by-Turn Cue Generation from Track Geometry (Core C + CLI)
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

### T5: Expose Turn-by-Turn Cue Generation in Web/WASM UI & Update Docs
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
