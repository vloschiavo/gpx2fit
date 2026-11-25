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

### T0: Expand Default Waypoint Map (`data/default.map`) with Additional Garmin Course Points
- [x] **Add additional Garmin Course Point types and default keyword mappings to `data/default.map` without duplicates**
  - **Scope:**
    - Read: `data/default.map`, `course.h`, `symmap.c` (and any course-point enum/lookup table), `Makefile`, `tests/`.
    - Modify: `data/default.map`, course-point enum/lookup table (if any requested FIT `course_point` enum values are not yet registered), `tests/`.
  - **Implementation Details:**
    - Audit existing entries in `data/default.map` and the C `course_point` type lookup table so **zero duplicate keywords or duplicate enum entries** are introduced.
    - Ensure the C `course_point` enum/string table supports the FIT SDK `course_point` enum values corresponding to all requested categories (e.g., `info`, `service`, `aid_station`, `first_aid`, `food`, `energy_gel`, `sports_drink`, `sprint`, `hors_categorie` / `hc_climb`, `category_1`..`category_4`, `toilet`, `shower`, `gear`, `navaid`, `transport`, `transition`, `checkpoint`, `meeting_spot`, `campsite`, `shelter`, `rest_area`, `obstacle_start`, `obstacle_end`, `summit`, `tunnel`, `bridge`, `valley`, `overlook`, `store`, `alert`, `danger`, `obstacle`, `crossing`, `steep_incline`, `sharp_curve`, and POI-style course points such as `convenience`, `coffee_shops`, `bike_shop_and_repair`, `park`, `winery_or_brewery`, `bank_or_atm`, `restaurant`, `emergency`, `recreation`, `attraction`, `lodging`, `shopping`, `community`—verify exact FIT Profile enum IDs in `tests/tools/FITCSVTool.jar` or SDK notes).
    - Add clean, deduplicated `keyword = type` rules to `data/default.map` covering all unique items (note: `Transport` appeared twice in the raw request and must appear only once):
      - `Info`, `Service`, `Aid Station`, `First Aid`, `Food`, `Energy Gel`, `Sports Drink`, `Sprint`
      - `HC Climb`, `Category 1 Climb`, `Category 2 Climb`, `Category 3 Climb`, `Category 4 Climb`
      - `Toilet`, `Shower`, `Gear`, `Navaid`, `Transport`, `Transition`, `Checkpoint`, `Meeting Spot`
      - `Campsite`, `Shelter`, `Rest Area`, `Obstacle Start`, `Obstacle End`
      - `Summit`, `Tunnel`, `Bridge`, `Valley`, `Overlook`, `Store`
      - `Alert`, `Danger`, `Obstacle`, `Crossing`, `Steep Incline`, `Sharp Curve`
      - `Convenience`, `Coffee Shops`, `Bike Shop and Repair`, `Park`, `Winery or Brewery`, `Bank or ATM`, `Restaurant`, `Emergency`, `Recreation`, `Attraction`, `Lodging`, `Shopping`, `Community`
    - Ensure both lowercase/normalized keyword forms (e.g., `aid station`, `first aid`, `energy gel`, `sports drink`, `hc climb`, `category 1 climb`, etc.) and common aliases match without colliding with existing rules in `data/default.map`.
  - **Verification:** Rebuild `src/default_map.h` via `make`, add unit tests verifying each new course point maps cleanly with no duplicate keys or warnings, verify FIT output with `FITCSVTool.jar`, and run `make test`.
  - **Acceptance Criteria:**
    - Every unique course point in the list above is supported and mapped in `data/default.map`.
    - `data/default.map` contains no duplicate keys or duplicate mappings (including deduplicating `Transport`).
    - `make test` passes under ASan/UBSan and `FITCSVTool.jar` decodes the generated `course_point` types cleanly.

---

### T1: Multi-Track Stitching & Multi-Course Download in Web/WASM UI
- [ ] **Add "Stitch multiple tracks into one course" checkbox to Web/WASM UI and support multi-file `.fit` output**
  - **Scope:**
    - Read: WASM C bridge (`wasm.c` / `web/*.c`), `web/index.html`, `web/app.js`, `Makefile`.
    - Modify: WASM C bridge, `web/index.html`, `web/app.js`.
  - **Implementation Details:**
    - Add a checkbox in `web/index.html` (e.g., `Stitch multiple tracks into a single course`, checked by default or clearly labeled) that appears or is active when GPX files are converted.
    - Pass the `stitch` boolean option into the WASM conversion function.
    - When `stitch` is **checked**: generate and download a single `.fit` file using the stitching logic (including `"start"` waypoint detection).  If there is no obvious `"start"` waypoint, ask the user to pick the first track, this is important for loops.
    - When `stitch` is **unchecked** and the GPX contains multiple `<trk>` elements: generate one `.fit` per track and offer individual download buttons (or sequential downloads / zip if a helper already exists—follow YAGNI, individual download links/buttons per track in the results panel require zero new dependencies).
  - **Verification:** Build WASM target if Emscripten is available; verify JS/C bridge parameter signatures and UI state handling; run `make test`. Use `reference/Tahoe_200__Official_Race_Map_2022.gpx` to test multiple GPX tracks.
  - **Acceptance Criteria:**
    - Web UI has a checkbox to toggle stitching multiple GPX tracks into one FIT course vs. separate FIT courses.
    - Single-track GPX files continue to work without extra friction.
    - Multi-track GPX files produce either 1 stitched `.fit` or N separate `.fit` downloads depending on the checkbox. 

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
