# gpx2fit: Task Breakdown

Scope: **GPX input, FIT course output, written in C.** GeoJSON and KML are out of scope.

Each task is sized to fit in one small working session. Every task lists what to load into context (**Needs**), what it produces (**Output**), and how to tell it is finished (**Done when**). Do the tasks in order unless a task says otherwise. Keep `course.h` and the task's own files in context, and little else.

## How to use this file in a session

1. Pick the next unchecked task.
2. Give the assistant: this task's text, the **Needs** files, and the line "Implement only this task. Do not change other files except as listed."
3. Build and run the task's check. Commit.
4. Tick the box.

---

## Phase 0: Recon (no code)

- [x] **0.1 Export a test map from CalTopo as GPX**
  - Make a small map: one track (about 5 to 10 km), 4 to 6 waypoints (some with symbols), one waypoint deliberately 400 m off the track.
  - Output: `tests/data/caltopo_sample.gpx`
  - Done when: file opens in a text editor and you have noted whether `<ele>`, `<sym>`, `<type>`, `<desc>` are present, and whether it is GPX 1.0 or 1.1.

- [x] **0.2 Record GPX findings**
  - Output: `NOTES.md` with a short list: namespace, track structure (`trk`/`trkseg` counts), waypoint fields present, elevation present or not, sample `<sym>` values.
  - Result to record: these CalTopo exports contain no `<sym>`, so waypoint types are derived from names (see Phase 5). Do not add `<sym>` parsing.
  - Done when: the notes answer every question in 0.1.

- [x] **0.3 Obtain a known-good FIT course file**
  - Create a short course in Garmin Connect and download it as FIT (or use one from the FIT SDK examples).
  - Output: `tests/data/reference_course.fit`
  - Done when: the file exists and you can open it with a hex viewer (`xxd`).

- [x] **0.4 Get the FIT SDK Profile reference**
  - Download the FIT SDK and locate the Profile spreadsheet and the C headers.
  - Output: in `NOTES.md`, a table for each message used (`file_id`, `course`, `lap`, `course_point`, `record`) giving field number, name, base type, size, scale, offset, and units.
  - Done when: every field you plan to write is in the table, with the `course_point.name` string length written down.

- [x] **0.5 Decode the reference file**
  - Use the SDK's CSV tool or a Python parser (`fitparse`) to dump the reference file.
  - Output: `tests/data/reference_course.csv` plus notes on message order and which fields Garmin itself writes.
  - Done when: `NOTES.md` has the message order of the reference file.

---

## Phase 1: Project skeleton

- [x] **1.1 Directory layout and Makefile**
  - Needs: the architecture section of `plan.md`.
  - Output: `Makefile` (targets `all`, `test`, `clean`, `asan`), empty `src/` and `tests/` dirs, flags `-std=c11 -Wall -Wextra -Wpedantic`. A rule that generates `default_map.h` from `data/default.map` is added in 5.3.
  - Done when: `make` builds a stub `main.c` that prints usage.

- [x] **1.2 Core types header**
  - Output: `src/course.h` with `struct trackpoint`, `struct waypoint`, `struct course_pt`, `struct course`, plus init/free function declarations. `struct waypoint` has `name`, lat and lon, and no `sym` field.
  - Done when: the header compiles on its own (`gcc -fsyntax-only`) and every field has a comment on units (degrees, meters, seconds).

- [x] **1.3 Course memory management**
  - Needs: `course.h`
  - Output: `src/course.c` with `course_init`, `course_free`, growable-array helpers for trackpoints and waypoints.
  - Done when: a tiny test adds 10,000 points, frees them, and runs clean under ASan.

- [x] **1.4 CLI parsing**
  - Needs: `main.c`, `course.h`
  - Output: `getopt_long` handling for `input.gpx output.fit`, `--snap METERS` (default 250), `--name`, `--pace`, `--start`, `--quiet`, `--strict`, `--max-points`, `--map FILE`, `--help`. Options are parsed into a `struct options`; unimplemented ones are accepted and ignored for now.
  - Done when: `--help` prints usage, bad arguments return exit code 2, and a test prints the parsed options.

---

## Phase 2: GPX reader (expat)

- [x] **2.1 Install and link expat**
  - Output: Makefile changes (`-lexpat`), a hello-world expat parse of the sample file that counts elements.
  - Done when: `make` links and the element count prints for `caltopo_sample.gpx`.

- [x] **2.2 Namespace-agnostic element matching**
  - Needs: `NOTES.md` findings, a stub `gpx.c`
  - Output: a helper that strips any namespace prefix or URI from element names, so GPX 1.0 and 1.1 both match.
  - Done when: unit test passes for `gpx:trkpt`, `trkpt`, and `{http://www.topografix.com/GPX/1/1}trkpt` style names (using whichever expat namespace mode you chose).

- [x] **2.3 Parse trackpoints**
  - Needs: `gpx.c`, `course.h`
  - Output: `read_gpx()` fills `course.tp[]` with lat, lon, and elevation (`has_ele` flag set only when `<ele>` is present).
  - Done when: the sample file's point count matches `xmllint`, and the first and last points match by eye.

- [x] **2.4 Multiple segments and tracks**
  - Output: concatenation of all `<trkseg>` in a track. With several `<trk>`, select the longest by default, and honor `--track N`. Warn on stderr.
  - Done when: fixtures with two segments and with two tracks behave as described.

- [x] **2.5 Parse waypoints**
  - Output: fills `course.wp[]` with name, lat, lon (name string copied, UTF-8 preserved). `<sym>` is not parsed.
  - Done when: the sample file's waypoint count and names match `xmllint`.

- [x] **2.6 Parse track name and handle bad input**
  - Output: course name from `<trk><name>`, falling back to the file name. Clear error messages for a missing file, malformed XML (with line number), no trackpoints, and bad lat/lon values.
  - Done when: each error case has a fixture and returns exit code 1 with a message.

- [x] **2.7 GPX reader tests**
  - Output: `tests/test_gpx.c` and fixtures: GPX 1.0, GPX 1.1, no elevation, empty track, Unicode waypoint name.
  - Done when: `make test` passes, ASan clean.

---

## Phase 3: Geometry

- [x] **3.1 Haversine and cumulative distance**
  - Needs: `course.h`
  - Output: `src/geo.c`: `haversine_m()`, `course_compute_distances()` which sets `tp[i].dist` and skips zero-length segments safely.
  - Done when: a test with known distances (for example one degree of latitude is about 111.19 km) passes within a tolerance.

- [x] **3.2 Local projection helper**
  - Output: equirectangular conversion to local x/y meters around a reference latitude, with a documented accuracy limit.
  - Done when: round-trip and distance tests pass against haversine within 0.1% for tracks under 100 km.

- [x] **3.3 Point-to-segment projection**
  - Output: `project_to_segment()` returning the clamped parameter `t`, the projected lat/lon, and the perpendicular distance. Handles zero-length segments.
  - Done when: tests cover perpendicular, before-start clamp, after-end clamp, and degenerate segment cases.

- [x] **3.4 Elevation statistics**
  - Output: total ascent and descent, with a simple noise threshold (for example ignore changes under 1 m), and min/max elevation. Safe when `has_ele` is false.
  - Done when: a synthetic profile with known ascent gives the expected value.

- [x] **3.5 Geometry test suite**
  - Output: `tests/test_geo.c` collecting tests from 3.1 to 3.4.
  - Done when: `make test` passes, ASan and UBSan clean.

---

## Phase 4: Snapping

- [x] **4.1 Bounding-box prefilter**
  - Needs: `geo.c`, `course.h`
  - Output: a function that returns the candidate segments for a waypoint within the snap threshold, using bounding boxes before exact projection.
  - Done when: results match a brute-force reference on random data.

- [x] **4.2 Candidate snaps for one waypoint**
  - Output: `snap_candidates()` returns every local minimum of distance (one per pass of the track) within the threshold, each with distance-along-course, offset, and projected position.
  - Done when: an out-and-back fixture returns two candidates for a waypoint on the shared section, and one for a waypoint off the shared section.

- [x] **4.3 Simple nearest-candidate assignment**
  - Output: default mode `--pass=nearest`, choosing the candidate with the smallest offset; waypoints beyond the threshold are skipped with a stderr warning.
  - Done when: straight-line and loop fixtures give expected distances.

- [x] **4.4 Ordered assignment**
  - Output: `--pass=ordered` assigns candidates so that distance-along-course never decreases in GPX waypoint order, using a small DP or greedy approach. Document the tie-breaking rule at the top of the file.
  - Done when: an out-and-back fixture with waypoints listed outbound-then-return lands on the correct passes.

- [x] **4.5 First and last pass modes**
  - Output: `--pass=first` and `--pass=last`.
  - Done when: out-and-back fixture shows the expected pass for each mode.

- [x] **4.6 Sorting, `--strict`, and the summary**
  - Output: course points sorted by distance, nonzero exit under `--strict` if any waypoint was skipped, and a printed summary line for each course point (name, km, offset).
  - Done when: the sample GPX produces the expected report, including the 400 m skipped waypoint.

- [x] **4.7 Snapping tests**
  - Output: `tests/test_snap.c` with all fixtures: straight, loop, out-and-back, far waypoint, duplicate waypoints at the same spot.
  - Done when: `make test` passes.

---

## Phase 5: Point types and names

CalTopo exports no `<sym>` for these waypoints, so the type comes from the
waypoint **name** alone. The FIT `course_point` type is what makes Garmin draw an
icon on the map, so the goal of this phase is name -> FIT type. Matching rules are
fixed here so the code and tests agree.

- [ ] **5.1 Inventory names and FIT types**
  - Needs: the `course_point.type` enum table from `NOTES.md` (task 0.4)
  - Output: in `NOTES.md`, (a) every distinct waypoint name used in your CalTopo exports, and (b) the FIT type names and values you intend to target (at least `water`, `danger`, `campsite`), confirmed against the SDK Profile.
  - Done when: every name you use has an intended FIT type.

- [ ] **5.2 Map rule parser and matcher**
  - Output: `src/symmap.c` with `symmap_parse(text, &map, warn_cb)` and `symmap_lookup(&map, name)`. Line format is `keyword = type`; `#` starts a comment; blank lines are ignored. `type` is a FIT enum name (`water`, `danger`, `campsite`, `generic`, ...) resolved through a small name->value table. Matching is a case-insensitive **substring** match: the keyword may appear anywhere in the name. First matching rule in file order wins, so put more specific keywords above general ones. No match returns generic.
  - Done when: unit tests cover case-insensitivity, a keyword in the middle and at the end of a name, first-match-wins ordering, comments and blank lines, an unknown type name (warning with line number, line skipped), and a malformed line (same).

- [ ] **5.3 `data/default.map`**
  - Output: `data/default.map` in the same format the parser reads, containing at least `water = water`, `camp = campsite`, `caution = danger`, plus anything 5.1 turned up. The Makefile embeds this file into the binary at build time (for example `xxd -i` into a generated `default_map.h`), so the program has no runtime file lookup and the file stays the single source of truth.
  - Done when: `make` regenerates the header when `default.map` changes, and a test feeds "Water", "Camp", "Caution", "water 2", "CAMP" and an unknown name through the embedded defaults and gets the expected types.

- [ ] **5.4 `--map FILE` layering**
  - Output: `--map FILE` loads user rules ahead of the embedded defaults, so user rules win and unmatched names still fall through to `default.map`. A missing or unreadable file is an error (exit 1). Names that match nothing produce a stderr warning ("no type for 'Foo', using generic; add a line to --map") unless `--quiet`.
  - Done when: a test map overrides `water` to another type, an unrelated name still hits a default, a user line such as `waterfall = generic` placed first beats the default `water` rule, and the missing-file case exits 1.

- [ ] **5.5 Safe name truncation**
  - Output: one truncation function taking a max byte count, which never splits a UTF-8 sequence and always null-terminates. Two limits: the course point name limit from the 0.4 table, and `COURSE_NAME_MAX`, set to 90 bytes (a length known to work on the device; the true maximum is unknown, so revisit it in 9.1).
  - Done when: tests cover ASCII, multibyte at the boundary, and exact-length strings, for both limits.

---

## Phase 6: Synthetic timestamps

- [ ] **6.1 Time model**
  - Output: `course_assign_times()` using start time (`--start`, default a fixed date) and pace (`--pace`, accepts m/s or min/km syntax), computing `tp[i].t` as seconds since the FIT epoch (1989-12-31 00:00:00 UTC).
  - Done when: a test confirms the epoch constant (Unix epoch plus 631065600) and monotonic times.

- [ ] **6.2 Time for course points**
  - Output: each course point's time is interpolated from its distance along the course.
  - Done when: a course point halfway along a segment gets the midpoint time.

---

## Phase 7: FIT encoder

Work through these in order. Verify against `reference_course.csv` and the decoder after each step.  Garmin writes undocumented fields (record 121 and 122, course 254, file_creator field 2). We can omit them.

- [ ] **7.1 CRC-16**
  - Output: `src/fit_write.c` with the table-driven CRC from the SDK.
  - Done when: a test matches known CRC values (compute them with the SDK or a Python reference).

- [ ] **7.2 Buffered writer**
  - Output: a small in-memory writer with `put_u8/u16/u32`, little-endian, growable buffer, and a function that writes the file once at the end.
  - Done when: unit test checks byte output for known values.

- [ ] **7.3 Header and footer**
  - Output: the 14-byte header (with header CRC), back-patching the data size once all records are written, and the trailing file CRC.
  - Done when: a file with no messages has a valid header and CRC according to the SDK decoder.

- [ ] **7.4 Definition message helper**
  - Output: a function that writes a definition message from a table of (field number, size, base type), assigning local message types.
  - Done when: output bytes match the definition record of the reference file for `file_id`.

- [ ] **7.5 `file_id` message**
  - Output: type = course, manufacturer, product, serial (if required), time created.
  - Done when: the SDK decoder reads it correctly. Compare with the reference file.

- [ ] **7.6 `course` message**
  - Output: sport and name. The name field in the definition message must be sized for `COURSE_NAME_MAX` plus the terminator (91 bytes).
  - Done when: the decoder shows the right name, including a 90-byte name.

- [ ] **7.7 `record` messages**
  - Output: timestamp, position_lat, position_long (semicircles), distance, altitude (scaled and offset per 0.4); altitude written as the invalid value when `has_ele` is false.
  - Done when: decoded values match the input within quantization error, and a conversion test checks semicircle math at known points (0, 90, -180 degrees).

- [ ] **7.8 `lap` message**
  - Output: start and end positions, total distance, elapsed and timer time, per whatever the reference file includes.
  - Done when: the decoder reports totals matching the summary.

- [ ] **7.9 `course_point` messages**
  - Output: timestamp, position, distance, type, name.
  - Done when: decoded course points match the snapped list from phase 4.

- [ ] **7.10 Message ordering**
  - Output: reorder messages to match the reference file (verify whether course points go before or after records).
  - Done when: the decoder accepts the file with no warnings, and the message order matches `NOTES.md`.

- [ ] **7.11 Encoder tests**
  - Output: `tests/test_fit.c` plus a Bash script that encodes the sample, decodes it with the SDK tool or `fitparse`, and diffs against expected values.
  - Done when: `make test` passes and the decode diff is empty or explained.

---

## Phase 8: Integration

- [ ] **8.1 Wire the pipeline in `main.c`**
  - Output: read GPX, distances, snap, map types (using the `--map` rules layered over the embedded defaults), times, write FIT, print summary. Each stage returns an error code.
  - Done when: `gpx2fit caltopo_sample.gpx out.fit` produces a file that decodes cleanly.

- [ ] **8.2 `--max-points` decimation**
  - Output: Douglas-Peucker (or uniform) reduction that always keeps the first point, the last point, and the points nearest each course point.
  - Done when: a dense fixture reduces to N points and course point distances stay within 1 m of the full-resolution values.

- [ ] **8.3 Summary and `--quiet`**
  - Output: total distance, ascent and descent, point count, course point list. Silence everything except errors under `--quiet`.
  - Done when: output matches the format in the README draft.

- [ ] **8.4 End-to-end Bash test**
  - Output: `tests/run.sh` that runs every fixture through the binary and checks exit codes and the summary text.
  - Done when: `make test` runs unit and end-to-end tests together.

---

## Phase 9: Real-world validation

- [ ] **9.1 Garmin Connect import**
  - Upload the generated FIT as a course. Confirm distance, elevation profile, and course point list look right.
  - Done when: the course saves with no errors. Note any warnings in `NOTES.md`.

- [ ] **9.2 Watch test**
  - Send to the watch, start the course, and check Up Ahead for names, types, and distances.
  - Done when: points appear in the right order at the right distances.

- [ ] **9.3 Direct USB copy test**
  - Copy the FIT file to the watch's courses folder and confirm it loads.
  - Done when: result recorded in `NOTES.md` (works or not, on which model and firmware).

- [ ] **9.4 Fix problems found**
  - Open a new task for each issue here, with the symptom and the suspected cause.
  - Done when: 9.1 to 9.3 all pass.

---

## Phase 10: Hardening and release

- [ ] **10.1 Sanitizer run**
  - Done when: the full test suite is clean under ASan and UBSan.

- [ ] **10.2 Fuzz the GPX reader**
  - Output: a libFuzzer or AFL++ harness around `read_gpx()`.
  - Done when: a run of at least a few minutes finds no crashes or leaks, or each finding is fixed.

- [ ] **10.3 Static analysis**
  - Done when: `cppcheck` or `clang-tidy` output is reviewed and issues are fixed or explained.

- [ ] **10.4 README**
  - Output: usage, options table, build and install steps (`libexpat1-dev`), the CalTopo workflow (name waypoints "Water", "Camp", "Caution"), the `--map` file format and `default.map`, limitations, and notes on tested devices.
  - Done when: someone else can follow it from scratch.

- [ ] **10.5 Install target and license**
  - Output: `make install PREFIX=~/.local`, `LICENSE` (MIT, as in the original, unless you choose otherwise).
  - Done when: a clean checkout builds and installs.

---

## Later (optional)

- [ ] Rust port using the C test fixtures and decoded reference output as the spec.
- [ ] Turn-by-turn cue generation from track geometry.
- [ ] Support for other Garmin course point types beyond the mapped set.
