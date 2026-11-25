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

### T1: Add `-v` and `--version` CLI Flags & Single-Source Version Definition
* [x] **Expose version information via `-v` and `--version` in the CLI binary from a single authoritative version source**
  * **Scope:**
    * Read: `course.h`, `Makefile`, `main.c`, `README.md`, and CLI test scripts (`tests/`).
    * Modify: `course.h` (or version header / `Makefile` define), `main.c`, `README.md`, and CLI tests.
  * **Implementation Details:**
    * Define the project version in one canonical place (e.g., a macro/header or `VERSION` variable passed/synced cleanly) so CLI output, WASM/web build metadata, and release tags stay consistent without drift.
    * Handle both `-v` and `--version` in CLI argument parsing in `main.c`, printing `gpx2fit <version>` (matching existing CLI output conventions) to `stdout` and exiting with status `0` before requiring an input GPX file.
    * Ensure `-v` / `--version` works regardless of flag ordering or extra trailing arguments if appropriate, and document both flags in `--help` / usage text and `README.md`.
    * Set the version number as `v1.0.0`
  * **Verification:** Add unit/E2E CLI tests checking `-v` and `--version` exit code `0` and expected version output; run `make test`.
  * **Acceptance Criteria:**
    * Running the CLI binary with `-v` or `--version` prints the version string and exits `0` without requiring a GPX input file.
    * Usage/help output and `README.md` document `-v` and `--version`.
    * `make test` passes under ASan/UBSan.

---

### T2: Display Small Version Number at the Bottom of the WASM Page
* [x] **Render a subtle, non-intrusive version label in the footer of the Web/WASM UI**
  * **Scope:**
    * Read: `Makefile`, `web/index.html`, `web/app.js`, `web/style.css`, and existing web UI test scripts (`tests/test_web*.mjs`, `tests/test_web*.sh`).
    * Modify: `Makefile` (if build-time version injection/generation is used), `web/index.html`, `web/app.js`, `web/style.css`, and web UI tests.
  * **Implementation Details:**
    * Add a compact footer element at the bottom of `web/index.html` displaying the current version (e.g., `v...` or `Version ...`) in small, muted text that does not shift layout height or interfere with existing controls.
    * Populate or inject the version string consistently with the project/WASM build (via build-time generation, `version.json`/config, or WASM/JS constant) so it updates automatically across builds.
    * Style the footer cleanly in `web/style.css` for both desktop and mobile viewports.
  * **Verification:** Run `make test`, WASM build (`make wasm` if available), and web/WASM test scripts (`tests/test_web*.sh` / `tests/test_web*.mjs`); add a test assertion verifying the footer version element is rendered.
  * **Acceptance Criteria:**
    * A small version label appears at the bottom of the WASM page without causing layout shift or visual clutter.
    * The displayed version matches the project version.
    * `make test` and web tests pass with no regressions.

---

### T3: Parameterize Build & Cross-Compilation Targets in `Makefile`
* [x] **Support configurable target OS/architecture output names, compilers, and library paths in `Makefile`**
  * **Scope:**
    * Read: `course.h`, `Makefile`.
    * Modify: `Makefile`.
  * **Implementation Details:**
    * Ensure `CC`, `AR`, `LD`, `CFLAGS`, `LDFLAGS`, `BUILD`, and `TARGET` (including `.exe` suffix handling when targeting Windows) can be cleanly overridden per build without artifact collisions.
    * Support custom include/library flags (e.g., for `expat` and `m` across native, cross-compiled, and MinGW/MSYS2/Homebrew environments) and keep `install` (`PREFIX`, `DESTDIR`) predictable.
    * Avoid running host-only tools or ASan-instrumented binaries when cross-compiling for non-host architectures.
  * **Verification:** Test native build, custom `BUILD`/`TARGET` directory builds, and cross-compiler variable overrides locally where toolchains are installed; run `make test`.
  * **Acceptance Criteria:**
    * `Makefile` allows isolated out-of-tree/target-specific builds (`BUILD` and `TARGET`) and compiler/flag overrides cleanly.
    * Native builds and `make test` succeed without regressions.

---

### T4: Expand GitHub Actions Matrix to Build Binaries for macOS, Windows, and Linux Across CPU Architectures
* [ ] **Build and package CLI release artifacts across Linux, macOS, and Windows for as many supported CPU architectures as possible**
  * **Scope:**
    * Read: `Makefile`, `.github/workflows/` (existing CI/release workflows).
    * Modify: `.github/workflows/` release/build workflow file(s) (and `Makefile` only if needed for target build flags).
  * **Implementation Details:**
    * Configure a matrix covering:
      * **Linux:** `x86_64` (`amd64`), `aarch64` (`arm64`), and additional architectures supported by runners/cross-toolchains (e.g., `armv7`/`armhf`, `riscv64`, `s390x`, `ppc64le` via native/ARM runners or cross-compilers + static/sysroot `expat` where feasible).
      * **macOS:** `arm64` (Apple Silicon) and `x86_64` (Intel, via universal/target flags or x86_64/cross-arch flags where supported on macOS runners) with `expat` linked cleanly.
      * **Windows:** `x86_64` (`amd64`) and `arm64` (where toolchain/MinGW/MSVC/LLVM supports it) producing `.exe` binaries with `expat` linked cleanly.
    * Package each binary with `README.md`, `LICENSE`, and `SHA256SUMS.txt` into `.tar.gz` (Unix) and `.zip` (Windows), and attach artifacts to GitHub Actions / GitHub Releases on version tags (`v*`) and `workflow_dispatch`.
    * Clearly separate/skip architectures that cannot link `expat` or complete cleanly in CI rather than silently shipping broken binaries.
  * **Verification:** Validate workflow YAML syntax, verify all matrix entries have distinct artifact names and archive formats, and run `make test`.
  * **Acceptance Criteria:**
    * GitHub Actions builds and packages binaries for macOS, Windows, and Linux across multiple CPU architectures with unique artifact names and checksums.
    * Every included target compiles and links `expat` cleanly with no broken/empty artifacts.
    * `make test` passes.

---

# Optional

### T5: Turn-by-Turn Cue Generation from Track Geometry (Core C + CLI)
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
    * `make test` passes under ASan/UBSan.
```[cite: 1]

*(Note: Previously optional WASM UI task T4 was removed as completed/superseded by T2 above; if you also want the turn-by-turn WASM toggle kept in `PLAN.md`, let me know and I can slot it in right after `T5`.)*[cite: 1]
