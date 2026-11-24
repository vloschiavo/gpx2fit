# gpx2fit: v2 Task Breakdown (Activity Types, Multi-Arch Releases & Web/WASM)

Scope: **Add Garmin activity/sport selection, automate cross-platform binary releases via GitHub Actions, and compile the core C library to WebAssembly for a client-side web app.**

Each task fits in one small working session. Do the tasks in order unless noted. Keep `course.h` and the task's own files in context, and little else.

---

## Phase 11: Garmin Activity / Sport Selection (`--sport`)

`struct fit_options` already has a `sport` field (defaulting to hiking = 17)[cite: 2, 3], and the `course` message already writes `sport` and `sub_sport`[cite: 2, 3]. Verify exact FIT SDK `sport` and `sub_sport` enum numbers against `garmin-fit-sdk` (Profile 21.222) before locking constants[cite: 2].

- [x] **11.1 Sport & sub-sport enum mapping (`src/sport.h`, `src/sport.c`)**
  - Needs: `NOTES.md` (Section 0.4 `course` message)[cite: 2], `src/fit_write.h`[cite: 3]
  - Output: lookup table and parser `sport_parse(const char *str, uint8_t *sport, uint8_t *sub_sport)` supporting case-insensitive names and aliases:
    - `hiking` (default: `sport = 17`, `sub_sport = 0` generic)[cite: 2]
    - `running` (`sport = 1`, `sub_sport = 0` generic)[cite: 2]
    - `trail-running` / `trail_running` (`sport = 1`, `sub_sport = 3` trail — verify exact `sub_sport` ID in `garmin-fit-sdk`)
    - `cycling` / `road-cycling` (`sport = 2`, `sub_sport = 0` generic or `7` road — verify in `garmin-fit-sdk`)[cite: 2]
    - `gravel` / `gravel-cycling` (`sport = 2`, `sub_sport = 46` gravel_cycling — verify in `garmin-fit-sdk`)
    - `mtb` / `mountain-biking` (`sport = 2`, `sub_sport = 8` mountain — verify in `garmin-fit-sdk`)
    - `other` / `generic` (`sport = 0`, `sub_sport = 0` generic)
  - Done when: unit test covers all 7 activity types, aliases, case-insensitivity, and rejects invalid names.

- [x] **11.2 Wire `sub_sport` through `fit_options` and `fit_write.c`**
  - Needs: `src/fit_write.h`, `src/fit_write.c`, `src/sport.h`[cite: 3]
  - Output: add `uint8_t sub_sport` to `struct fit_options` (0 = generic by default)[cite: 2, 3] and write it into the `course` message field 7[cite: 2, 3].
  - Done when: `FitCSVTool` / `garmin-fit-sdk` decodes both `sport` and `sub_sport` accurately for all 7 types, and existing golden tests still pass with defaults[cite: 3].

- [x] **11.3 Add `--sport TYPE` CLI option and update docs/tests**
  - Needs: `src/main.c`, `src/sport.h`, `README.md`, `tests/test_cli.sh`[cite: 2, 4, 5]
  - Output: add `--sport TYPE` to `getopt_long`, `--help`, `--dump-options`[cite: 2, 4], summary output, and `README.md` options table[cite: 5]. Bad `--sport` values return exit code 2[cite: 5].
  - Done when: `make test` and `make test-asan` pass with new CLI and decode tests for all 7 sport types[cite: 1].

---

## Phase 12: Release Packaging & Multi-Arch GitHub Actions CI

Keep local development focused on native x86_64 builds (`make`, `make test`, `make test-asan`)[cite: 1] and let GitHub Actions build cross-platform binaries on native runners.

- [x] **12.1 Add `install` target and `LICENSE` (completes 10.5)**
  - Needs: `Makefile`[cite: 1]
  - Output: `LICENSE` (MIT)[cite: 4, 5] and `install` target in `Makefile` honoring `PREFIX ?= /usr/local` and `DESTDIR`[cite: 1, 4].
  - Done when: `make install PREFIX=/tmp/gpx2fit-test` installs the binary cleanly[cite: 4].

- [x] **12.2 CI test workflow (`.github/workflows/ci.yml`)**
  - Needs: `Makefile`[cite: 1]
  - Output: GitHub Actions workflow triggered on push and PR running `make test`, `make test-asan`, and `cppcheck`[cite: 1, 4].
  - Done when: workflow passes green on GitHub-hosted `ubuntu-latest`.

- [x] **12.3 Multi-arch release workflow (`.github/workflows/release.yml`)**
  - Needs: `Makefile`[cite: 1]
  - Output: workflow triggered on `v*` tags (and `workflow_dispatch`) building native binaries across:
    - `linux-x86_64` (`ubuntu-latest`, `libexpat1-dev`)[cite: 5]
    - `linux-arm64` (`ubuntu-24.04-arm`, `libexpat1-dev`)[cite: 5]
    - `macos-arm64` (`macos-latest`, `brew install expat`)
    - `windows-x86_64` (`windows-latest`, MSYS2 UCRT64 `mingw-w64-ucrt-x86_64-expat` + `make`)
  - Packages each binary + `README.md` + `LICENSE` into `.tar.gz` / `.zip` with `SHA256SUMS.txt` and uploads to the GitHub Release.
  - Done when: a manual `workflow_dispatch` test run produces working archives for all four targets.

---

## Phase 13: Decouple Core Library from File I/O for WASM

Prepare `LIB_SRC` so every stage (GPX read, custom map parse, FIT write, warnings) runs in memory with zero filesystem assumptions[cite: 1, 5].

- [x] **13.1 In-memory GPX reader (`read_gpx_mem`)**
  - Needs: `src/gpx.h`, `src/gpx.c`[cite: 4]
  - Output: `read_gpx_mem(const char *buf, size_t len, const char *fallback_name, struct course *c, ...)` feeding chunks into `XML_Parse` without `fopen`/`fread`. Refactor `read_gpx(path, ...)` to read the file into a buffer and delegate to `read_gpx_mem()`.
  - Done when: all existing GPX unit tests and `fuzz_gpx.c` pass with zero behavior change[cite: 2, 4].

- [x] **13.2 Structured warning & error collector**
  - Needs: `src/gpx.c`, `src/snap.c`, `src/symmap.c`, `src/main.c`[cite: 2, 4]
  - Output: optional callback or `struct diag_list` passed into library functions so warnings/errors are captured into strings for WASM callers while `main.c` still prints them to `stderr`[cite: 5].
  - Done when: `make test` passes and CLI stderr output matches `tests/run.sh` byte-for-byte[cite: 1, 2, 4].

---

## Phase 14: WebAssembly Build & Node Smoke Tests

- [x] **14.1 WASM C bridge (`src/wasm_api.c`)**
  - Needs: `src/course.h`, `src/gpx.h`, `src/geo.h`, `src/snap.h`, `src/symmap.h`, `src/timing.h`, `src/fit_write.h`, `src/sport.h`[cite: 2, 3, 4]
  - Output: `EMSCRIPTEN_KEEPALIVE` functions:
    - `gpx2fit_convert(...)` accepting GPX XML string, optional custom `.map` string, and options (course name, sport/sub_sport, snap meters, pass mode, pace m/s, start timestamp, max points, strict flag), returning a pointer/length to the encoded `.fit` bytes plus a JSON string containing stats (distance, ascent/descent, point counts, snapped waypoints with lat/lon/km/offset/type, skipped waypoints) and warnings.
    - `gpx2fit_free(void *ptr)` to release allocated output buffers.
  - Done when: compiles cleanly under `emcc -Wall -Wextra -Wpedantic`.

- [x] **14.2 `make wasm` target & Node.js verification test**
  - Needs: `Makefile`, `src/wasm_api.c`, `tests/data/caltopo_sample.gpx`[cite: 1, 2]
  - Output: `make wasm` target building `web/dist/gpx2fit.js` + `web/dist/gpx2fit.wasm` via `emcc` (`-O2 -sWASM=1 -sUSE_EXPAT=1 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_ES6=1`), plus `tests/test_wasm.mjs` executed via `node` converting `caltopo_sample.gpx` and verifying the `.fit` header, CRC, and JSON metadata[cite: 2].
  - Done when: `make wasm && node tests/test_wasm.mjs` passes and is added to `.github/workflows/ci.yml`.

---

## Phase 15: Client-Side Web Application (`web/`)

Zero-backend static app (hostable on GitHub Pages / Cloudflare Pages) where GPX files never leave the user's browser.

- [ ] **15.1 Minimalist UI & file dropzone (`web/index.html`, `web/app.js`, `web/style.css`)**
  - Needs: `web/dist/gpx2fit.js`
  - Output: drag-and-drop `.gpx` picker + "Load sample CalTopo route" button (`caltopo_sample.gpx`)[cite: 2, 5], primary controls (Course Name, Activity Type dropdown for all 7 sports, Pace input supporting `min/km` and `km/h`, Snap distance slider/input), and one-click `.fit` download button.
  - Done when: dropping a GPX file converts and

## Later (optional)

- [ ] Support the same waypoint in both directions on out and back courses
- [ ] Turn-by-turn cue generation from track geometry.

