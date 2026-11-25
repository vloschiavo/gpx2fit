# gpx2fit: Task Breakdown

## Tools & Agent Workflow

* **FIT Verification:** `tests/tools/FitCSVTool.jar` (run via `java -jar tests/tools/FitCSVTool.jar -b <file.fit> <out.csv>` to verify generated FIT messages).
* **Build & Test Commands:**
  * Native + ASan/UBSan unit & E2E tests: `make test`
  * WASM build (if Emscripten is installed): `make wasm` (or `emcc` target in `Makefile`)

* **Agentic Execution Rules (`Orchestrator` -> `@worker` -> `@reviewer`):**
  1. Execute tasks serially in order (`T1` through `T4`).
  2. **Token Efficiency Rule:** `@worker` must read **only** `course.h` (or core headers) and the exact files listed in the task's **Scope** block—do not scan unrelated files, grep across the whole repo unnecessarily, or re-read large fixtures.
  3. `@worker` must add unit/E2E tests for every behavioral change and run `make test` before reporting back.
  4. `@reviewer` inspects `git diff` against the task's **Acceptance Criteria** without modifying files and returns `PASS` or `FAIL`.

---

## Tasks

### T1: Fix Scroll Jump, Layout Shift, and Focus Loss in `"Custom waypoint type rules"` Dialog
* [x] **Debounce custom waypoint rules edits (800 ms) and preserve scroll position, layout height, and input focus during Web/WASM updates**
  * **Scope:**
    * Read: `web/index.html`, `web/app.js`, `web/style.css`, and existing web UI test scripts (`tests/test_web*.mjs`, `tests/test_web*.sh`).
    * Modify: `web/index.html`, `web/app.js`, `web/style.css`, and web UI tests as needed.
  * **Implementation Details:**
    * Debounce WASM re-conversion/re-render triggered by typing in the `"Custom waypoint type rules"` input/textarea to **800 ms** (`input` event resets the timer; `change` / explicit apply or submit can trigger immediately if appropriate), rather than re-running conversion on every keystroke.
    * Ensure the custom rules dialog/container is not torn down or replaced when preview/results sections update, keeping focus anchored in the active field even under focus-follows-mouse window managers.
    * Preserve scroll position around re-renders by saving `window.scrollY` (and any relevant scroll container `scrollTop`) before DOM updates and restoring it immediately afterward; avoid side-effect `.focus()` or `.scrollIntoView()` calls on non-target elements during background updates.
    * Reserve space / stabilize layout height on preview/summary/warning containers (e.g., non-collapsing container or `min-height` / retaining current content until new render output is ready) so content updates do not cause vertical jumps beneath the pointer.
  * **Verification:** Run `make test` and existing web/WASM test scripts (`tests/test_web*.sh` / `tests/test_web*.mjs`); add a regression check or test coverage for debounced rule updates and scroll/focus stability if applicable.
  * **Acceptance Criteria:**
    * Typing in `"Custom waypoint type rules"` uses an 800 ms debounce instead of re-converting on every single keystroke.
    * Page scroll position and layout height remain stable across rule updates, and focus stays in the rules input without forcing one-character-at-a-time scrolling under focus-follows-mouse.
    * `make test` and web tests pass with no regressions.

---

### T2: Apply Debounce, Scroll Preservation, and Layout Stabilization to Remaining Web/WASM Inputs
* [ ] **Extend the 800 ms debounce, scroll preservation, and layout height stabilization pattern to all other text, numeric, select, and option inputs in the Web/WASM UI**
  * **Scope:**
    * Read: `web/index.html`, `web/app.js`, `web/style.css`, and web UI test scripts (`tests/test_web*.mjs`, `tests/test_web*.sh`).
    * Modify: `web/index.html`, `web/app.js`, `web/style.css`, and web UI tests as needed.
  * **Implementation Details:**
    * Audit remaining form controls in `web/index.html` and `web/app.js` (such as course name, pace, start time, snap distance, max points, sport, pass mode, track options, and checkboxes/toggles).
    * Apply the same **800 ms** debounce on text/numeric `input` handlers that trigger live re-conversion/re-renders (while keeping `change` / blur / explicit actions responsive), and route all re-renders through the shared scroll-preservation and height-stabilization helpers established in **T1**.
    * Ensure no other input triggers unexpected page jumps or steals focus during re-renders.
  * **Verification:** Run `make test` and existing web/WASM test scripts (`tests/test_web*.sh` / `tests/test_web*.mjs`); add targeted test assertions for debounced input handling and scroll/render stability.
  * **Acceptance Criteria:**
    * Other text/numeric/option inputs in the Web/WASM UI follow the same 800 ms debounce and scroll/layout-height stabilization behavior as `"Custom waypoint type rules"`.
    * No input field causes jarring page jumps or focus loss during re-renders.
    * `make test` and web tests pass cleanly.[cite: 2, 4]

---

# Optional 

### T3: Turn-by-Turn Cue Generation from Track Geometry (Core C + CLI)
* [ ] **Generate Garmin turn course points (`left`, `right`, `slight_left`, `slight_right`, `sharp_left`, `sharp_right`, `u_turn`) from track bearing changes**
  * **Scope:**
    * Read: `course.h`, geometry/distance math (`track.c` / `course.c`), `main.c`, `README.md`, `tests/`.
    * Modify: `course.h`, new or existing geometry module, `main.c`, `README.md`, `tests/`.
  * **Implementation Details:**
    * Add CLI flag `--turns` (and optional `--turn-angle DEG` threshold, default e.g. `35` degrees, off by default unless `--turns` is passed so existing waypoint-only workflows remain unchanged).
    * Compute smoothed incoming and outgoing bearings over a lookback/lookahead chord window (e.g., 15–30 m along the track) to filter out GPS jitter on switchbacks and noisy trail points.
    * Classify heading deltas ($\Delta\theta$) into FIT `course_point` types already supported by the codebase:
      * `slight_left` / `slight_right` (e.g., $35^\circ \le |\Delta\theta| < 65^\circ$)
      * `left` / `right` (e.g., $65^\circ \le |\Delta\theta| < 120^\circ$)
      * `sharp_left` / `sharp_right` (e.g., $120^\circ \le |\Delta\theta| < 160^\circ$)
      * `u_turn` (e.g., $|\Delta\theta| \ge 160^\circ$)
    * Enforce a minimum spacing between generated turn cues (e.g., 30–50 m) and suppress auto-generated turns within `--snap` (or 30 m) of an existing user waypoint so "Up Ahead" isn't cluttered.
    * Give generated turn points concise names (`"Left"`, `"Sharp right"`, `"U-turn"`, etc., well under the 15-byte limit) and ensure `--max-points` preserves trackpoints near generated turn cues.
  * **Verification:** Add unit tests with synthetic L-turn, hairpin/U-turn, slight-bend, and noisy straight-line tracks verifying exact turn types, distances, noise immunity, and waypoint deduplication. Verify generated FIT output with `FitCSVTool.jar` and run `make test`.
  * **Acceptance Criteria:**
    * `--turns` generates accurate FIT turn course points from track geometry without firing on minor GPS jitter.
    * Auto-turns do not duplicate existing waypoints at the same junction.
    * `make test` passes under ASan/UBSan.[cite: 2]

---

### T4: Expose Turn-by-Turn Cue Generation in Web/WASM UI & Update Docs
* [ ] **Add Turn-by-Turn cues toggle in Web/WASM UI and wire it to the C turn generator**
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
    * `README.md` accurately documents all new CLI flags and Web UI options.[cite: 2]
