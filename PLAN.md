# gpx2fit: v3 Task Breakdown (Out-and-Back Waypoints, Multi-Track Stitching, Turn-by-Turn & Web UX)

## Tools & Agent Workflow
- **FIT Verification:** `tests/tools/FITCSVTool.jar` (run via `java -jar tests/tools/FITCSVTool.jar -b <file.fit> <out.csv>` to verify generated FIT messages).
- **Build & Test Commands:**
  - Native + ASan/UBSan unit & E2E tests: `make test`
  - WASM build (if Emscripten is installed): `make wasm` (or `emcc` target in `Makefile`)
- **Agentic Execution Rules (`Orchestrator` -> `@worker` -> `@reviewer`):**
  1. Execute tasks serially in order (`T1` through `T9`).
  2. `@worker` must read **only** `course.h` (or core headers) and the specific files listed in the task's **Scope** block—do not scan unrelated files.
  3. `@worker` must add unit/E2E tests for every behavioral change and run `make test` before reporting back.
  4. `@reviewer` inspects `git diff` against the task's **Acceptance Criteria** without modifying files and returns `PASS` or `FAIL`.

---

## Tasks

### T1: Change Default Activity Type to `Trail Running`
- [x] **Change default `--sport` from `hiking` to `trail-running` across CLI, C library defaults, WASM UI, and docs**
  - **Scope:**
    - Read: `course.h`, `main.c`, `web/index.html`, `web/app.js`, `README.md`, and `tests/` files referencing default sport (`hiking` / `17`).
    - Modify: Core options initialization (`main.c` / `course.h`), `web/index.html` (default `<select>` option), `README.md`, and affected test assertions.
  - **Implementation Details:**
    - Set default sport to `trail-running` (`sport = 1`, `sub_sport = 3`).
    - Update CLI `--help` output and `README.md` (`--sport TYPE` table and Quick Start example output).
    - Update the Web/WASM sport selector so `Trail Running` (`trail-running`) is selected by default on load.
  - **Verification:** `make test` (update existing tests that assumed `hiking` when `--sport` is omitted, and add/confirm a test verifying FIT `sport=1, sub_sport=3` by default).
  - **Acceptance Criteria:**
    - Running `./gpx2fit input.gpx output.fit` without `--sport` outputs `Activity: trail-running` and writes `sport=1, sub_sport=3` in the FIT `sport` message.
    - Explicit `--sport hiking` still produces `17 / 0`.
    - Web UI defaults to Trail Running.
    - `make test` passes with zero warnings or ASan/UBSan errors.

---

### T2: Add Web/WASM Favicon (`web/favicon.svg`)
- [ ] **Wire `web/favicon.svg` into the web app and service worker cache**
  - **Scope:**
    - Read: `web/index.html`, `web/sw.js` (if present), `docker/httpd-gpx2fit.conf`, `Makefile`.
    - Modify: `web/index.html`, `web/sw.js` (if caching static assets), and build/deploy file lists if explicit.
  - **Implementation Details:**
    - Verify `web/favicon.svg` exists in the repo; add `<link rel="icon" type="image/svg+xml" href="favicon.svg">` in `web/index.html` `<head>`.
    - If `web/sw.js` maintains a precache asset array, include `./favicon.svg` and bump the cache version if required by existing conventions.
  - **Verification:** Check `web/index.html` markup, validate SVG syntax (`xmllint --noout web/favicon.svg` if available), and run `make test`.
  - **Acceptance Criteria:**
    - `web/index.html` includes `<link rel="icon" type="image/svg+xml" href="favicon.svg">`.
    - `web/favicon.svg` is included in static web bundle / service worker asset lists if applicable.
    - No broken relative paths.

---

### T3: Replace Sample Route with `reference/Rio_Del_Lago_100mi_2025.gpx`
- [ ] **Set `reference/Rio_Del_Lago_100mi_2025.gpx` as the default sample GPX in Web/WASM and CLI docs/tests**
  - **Scope:**
    - Read: `web/index.html`, `web/app.js`, `web/sw.js`, `Makefile`, `README.md`, and `tests/` referencing `caltopo_sample.gpx`.
    - Modify: Web sample loader (`web/app.js` / `web/index.html` / `Makefile`), `README.md`, and sample tests if applicable.
  - **Implementation Details:**
    - Copy or bundle `reference/Rio_Del_Lago_100mi_2025.gpx` into `web/` (or load it via the Web app's sample/default GPX mechanism—check how `caltopo_sample.gpx` is currently loaded in `web/`).
    - Replace references to the CalTopo sample route in the Web UI ("Load sample" / default preloaded route) with `Rio_Del_Lago_100mi_2025.gpx`.
    - Update `README.md` Quick Start / Tested Devices sections if they reference the default sample route (keep `tests/data/caltopo_sample.gpx` intact if existing unit tests depend on its exact coordinates/offsets).
  - **Verification:** Run `make test` and test converting `reference/Rio_Del_Lago_100mi_2025.gpx` via `./gpx2fit reference/Rio_Del_Lago_100mi_2025.gpx /tmp/rdl.fit`.
  - **Acceptance Criteria:**
    - Web/WASM app uses `Rio_Del_Lago_100mi_2025.gpx` as its default/sample GPX instead of `caltopo_sample.gpx`.
    - Converting `reference/Rio_Del_Lago_100mi_2025.gpx` succeeds cleanly with `make test` (including ASan/UBSan) without memory errors or truncation bugs.
    - Existing unit tests relying on `tests/data/caltopo_sample.gpx` continue to pass.

---

### T4: Expose Built-in `default.map` Rules in Web/WASM Advanced Options
- [ ] **Embed `data/default.map` into the WASM/Web UI and display it via an info popover/modal in Advanced Options**
  - **Scope:**
    - Read: `data/default.map`, `Makefile` (how `default.map` is embedded via `xxd`), WASM C bridge (`web/*.c` or `wasm.c`), `web/index.html`, `web/app.js`, `web/style.css`.
    - Modify: WASM export (or build step), `web/index.html`, `web/app.js`, `web/style.css`.
  - **Implementation Details:**
    - Expose the built-in `default.map` text to the web frontend (either via an exported C getter like `gpx2fit_get_default_map()` compiled into WASM so `data/default.map` remains the single source of truth, or injected at build time).
    - In the Web UI **Advanced Options** section (near the custom `--map` rules input), add an accessible info button (`aria-label="View built-in waypoint mapping rules"`).
    - Clicking or hovering/focusing the info button opens a clean tooltip/popover or modal displaying the exact `keyword = type` rules from `data/default.map` plus a brief note that custom rules take precedence over defaults. Ensure click-outside / `Escape` closes it cleanly on both desktop and mobile.
  - **Verification:** Rebuild WASM if toolchain is present; inspect `web/index.html`, `web/app.js`, and `web/style.css`; run `make test`.
  - **Acceptance Criteria:**
    - `data/default.map` content is dynamically sourced (no hardcoded duplicate drift).
    - Advanced Options contains an info button supporting both hover and click/keyboard focus.
    - Activating the info button clearly displays all built-in waypoint-to-Garmin course point mappings.

---

### T5: Support Snapping the Same Waypoint in Both Directions on Out-and-Back Courses
- [ ] **Add `--pass both` (or multi-pass snapping mode) so a single waypoint visited multiple times generates course points on each distinct pass**
  - **Scope:**
    - Read: `course.h`, waypoint snapping implementation (`snap.c` / `course.c`), `main.c`, `web/index.html`, `web/app.js`, `README.md`, and existing `--pass` tests in `tests/`.
    - Modify: `course.h`, snapping module, `main.c`, Web `--pass` dropdown (`web/index.html`), `README.md`, and `tests/`.
  - **Implementation Details:**
    - Inspect how candidate track projections/passes within `--snap` meters are currently detected for `--pass nearest|ordered|first|last`.
    - Add a new pass mode `both` (also consider whether `all` should be an alias for `both` so loops/out-and-backs with 2+ passes work seamlessly).
    - In `both` mode: when a track enters within `--snap` meters of a waypoint, finds a local minimum distance, exits `--snap` (or separates along track distance beyond the snap corridor so adjacent trackpoints on the same pass do not duplicate), and later re-enters `--snap` on the return leg, emit a course point for **each** distinct pass (ordered by cumulative track distance).
    - Ensure single-pass waypoints still emit only 1 course point, and ensure `--max-points` preserves trackpoints for **all** emitted course points.
    - Add `both` to CLI `--pass` parsing, `--help`, `README.md`, and the Web UI Pass Mode selector.
  - **Verification:** Add unit tests covering: (1) an out-and-back track passing an aid station twice (emits 2 course points at outbound and inbound distances), (2) a waypoint only visited once in `both` mode (emits 1 course point), and (3) `--max-points` preservation for both passes. Run `make test`.
  - **Acceptance Criteria:**
    - `--pass both` emits distinct outbound and return course points sorted by course distance when a track passes within `--snap` of a waypoint in both directions.
    - Adjacent trackpoints within a single pass do not create duplicate course points.
    - Web UI exposes the `both` pass option.
    - `make test` passes under ASan/UBSan.

---

### T6: Multi-Track GPX Stitching & Start Waypoint Identification (Core C + CLI)
- [ ] **Support stitching multiple `<trk>` elements into one course (`--stitch`) or emitting multiple `.fit` courses, using a `"start"` waypoint or track proximity to order segments**
  - **Scope:**
    - Read: `course.h`, GPX parser (`gpx.c` / `parser.c`), `main.c`, `README.md`, and multi-track tests in `tests/`.
    - Modify: `course.h`, GPX parser, `main.c`, `README.md`, and `tests/`.
  - **Implementation Details:**
    - **CLI Options:**
      - Add `--stitch` flag: concatenates all `<trk>` tracks into a single FIT course.
      - Add `--all-tracks` (or default behavior when `--stitch` is off and `--track` is omitted—check backward compatibility with existing `--track N` tests!):
        - Preserve `--track N` for selecting a single specific track.
        - When `--stitch` is passed:
          1. Look for a waypoint named `"start"` (case-insensitive exact match or `"start"` keyword—check existing waypoint naming conventions) to identify the starting track/endpoint.
          2. Pick the track whose start (or end, reversing if needed, or nearest start point to the `"start"` waypoint) matches the `"start"` waypoint; if no `"start"` waypoint exists, start with track 1 (or the track nearest the first waypoint) and greedily chain remaining tracks by nearest endpoint distance (reversing a track if its end is closer than its start to the current chain tail).
          3. Concatenate the ordered trackpoints into one continuous track before distance calculation, waypoint snapping, synthetic timestamps, and `--max-points` reduction.
        - When multi-course export is requested (e.g., `--split-tracks` or when `--stitch` is false in multi-track mode): write `<output_stem>_1.fit`, `<output_stem>_2.fit`, etc. (or `<track_name>.fit` sanitized), each snapping waypoints within `--snap` of that track. Keep existing single-track default behavior intact if existing tests expect it unless explicitly superseded.
  - **Verification:** Add unit and E2E tests with a multi-`<trk>` GPX fixture: (1) `--stitch` with out-of-order tracks and a `"Start"` waypoint orders and joins tracks accurately, (2) `--stitch` without a `"Start"` waypoint falls back cleanly, and (3) multi-course output writes valid separate `.fit` files verified with `FITCSVTool.jar`. Run `make test`.
  - **Acceptance Criteria:**
    - `--stitch` merges multiple `<trk>` elements into a single continuous course, using a `"start"` waypoint to anchor the start when present.
    - Multi-course mode generates separate valid `.fit` files per track.
    - `make test` passes with zero leaks or ASan/UBSan findings.

---

### T7: Multi-Track Stitching & Multi-Course Download in Web/WASM UI
- [ ] **Add "Stitch multiple tracks into one course" checkbox to Web/WASM UI and support multi-file `.fit` output**
  - **Scope:**
    - Read: WASM C bridge (`wasm.c` / `web/*.c`), `web/index.html`, `web/app.js`, `Makefile`.
    - Modify: WASM C bridge, `web/index.html`, `web/app.js`.
  - **Implementation Details:**
    - Add a checkbox in `web/index.html` (e.g., `Stitch multiple tracks into a single course`, checked by default or clearly labeled) that appears or is active when GPX files are converted.
    - Pass the `stitch` boolean option into the WASM conversion function.
    - When `stitch` is **checked**: generate and download a single `.fit` file using the T6 stitching logic (including `"start"` waypoint detection).
    - When `stitch` is **unchecked** and the GPX contains multiple `<trk>` elements: generate one `.fit` per track and offer individual download buttons (or sequential downloads / zip if a helper already exists—follow YAGNI, individual download links/buttons per track in the results panel require zero new dependencies).
  - **Verification:** Build WASM target if Emscripten is available; verify JS/C bridge parameter signatures and UI state handling; run `make test`.
  - **Acceptance Criteria:**
    - Web UI has a checkbox to toggle stitching multiple GPX tracks into one FIT course vs. separate FIT courses.
    - Single-track GPX files continue to work without extra friction.
    - Multi-track GPX files produce either 1 stitched `.fit` or N separate `.fit` downloads depending on the checkbox.

---

### T8: Turn-by-Turn Cue Generation from Track Geometry (Core C + CLI)
- [ ] **Generate Garmin turn course points (`left`, `right`, `slight_left`, `slight_right`, `sharp_left`, `sharp_right`, `u_turn`) from track bearing changes**
  - **Scope:**
    - Read: `course.h`, geometry/distance math (`track.c` / `course.c`), `main.c`, `README.md`, `tests/`.
    - Modify: `course.h`, new or existing geometry module, `main.c`, `README.md`, `tests/`.
  - **Implementation Details:**
    - Add CLI flag `--turns` (and optional `--turn-angle DEG` threshold, default e.g. `35` degrees, off by default unless `--turns` is passed so existing waypoint-only workflows remain unchanged).
    - Compute smoothed incoming and outgoing bearings over a lookback/lookahead chord window (e.g., 15–30 m along the track) to filter out GPS jitter on switchbacks and noisy trail points.
    - Classify heading deltas ($\Delta\theta$) into FIT `course_point` types already supported by the codebase:
      - `slight_left` / `slight_right` (e.g., $35^\circ \le |\Delta\theta| < 65^\circ$)
      - `left` / `right` (e.g., $65^\circ \le |\Delta\theta| < 120^\circ$)
      - `sharp_left` / `sharp_right` (e.g., $120^\circ \le |\Delta\theta| < 160^\circ$)
      - `u_turn` (e.g., $|\Delta\theta| \ge 160^\circ$)
    - Enforce a minimum spacing between generated turn cues (e.g., 30–50 m) and suppress auto-generated turns within `--snap` (or 30 m) of an existing user waypoint so "Up Ahead" isn't cluttered.
    - Give generated turn points concise names (`"Left"`, `"Sharp right"`, `"U-turn"`, etc., well under the 15-byte limit) and ensure `--max-points` preserves trackpoints near generated turn cues.
  - **Verification:** Add unit tests with synthetic L-turn, hairpin/U-turn, slight-bend, and noisy straight-line tracks verifying exact turn types, distances, noise immunity, and waypoint deduplication. Verify generated FIT output with `FITCSVTool.jar` and run `make test`.
  - **Acceptance Criteria:**
    - `--turns` generates accurate FIT turn course points from track geometry without firing on minor GPS jitter.
    - Auto-turns do not duplicate existing waypoints at the same junction.
    - `make test` passes under ASan/UBSan.

---

### T9: Expose Turn-by-Turn Cue Generation in Web/WASM UI
- [ ] **Add Turn-by-Turn cues toggle in Web/WASM UI and wire it to the C turn generator**
  - **Scope:**
    - Read: WASM C bridge (`wasm.c` / `web/*.c`), `web/index.html`, `web/app.js`, `README.md`.
    - Modify: WASM C bridge, `web/index.html`, `web/app.js`, `README.md`.
  - **Implementation Details:**
    - Add a checkbox/toggle in `web/index.html` (e.g., `Generate turn-by-turn cues` in Options / Advanced Options).
    - Pass the `turns` flag into the WASM conversion call and display generated turn course points in the web preview/summary table alongside user waypoints.
    - Update `README.md` with documentation for `--turns`, `--stitch`, `--pass both`, and the updated Web/WASM features.
  - **Verification:** Build WASM target if Emscripten is installed; run `make test`; review `git diff` across all modified files for consistency.
  - **Acceptance Criteria:**
    - Users can enable/disable turn-by-turn cue generation in the Web/WASM UI.
    - Generated turn cues appear in the web course point summary and in the downloaded `.fit` file.
    - `README.md` accurately documents all new CLI flags and Web UI options.
