# gpx2fit: v3 Task Breakdown (Out-and-Back Waypoints, Multi-Track Stitching, Turn-by-Turn & Web UX)

## Tools & Agent Workflow
- **FIT Verification:** `tests/tools/FitCSVTool.jar` (run via `java -jar tests/tools/FITCSVTool.jar -b <file.fit> <out.csv>` to verify generated FIT messages).
- **Build & Test Commands:**
  - Native + ASan/UBSan unit & E2E tests: `make test`
  - WASM build (if Emscripten is installed): `make wasm` (or `emcc` target in `Makefile`)
- **Agentic Execution Rules (`Orchestrator` -> `@worker` -> `@reviewer`):**
  1. Execute tasks serially in order (`T0` through `T3`).
  2. `@worker` must read **only** `course.h` (or core headers) and the specific files listed in the task's **Scope** block—do not scan unrelated files.
  3. `@worker` must add unit/E2E tests for every behavioral change and run `make test` before reporting back.
  4. `@reviewer` inspects `git diff` against the task's **Acceptance Criteria** without modifying files and returns `PASS` or `FAIL`.

---

## Tasks

---

### T2: Turn-by-Turn Cue Generation from Track Geometry (Core C + CLI)
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

### T3: Expose Turn-by-Turn Cue Generation in Web/WASM UI
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
