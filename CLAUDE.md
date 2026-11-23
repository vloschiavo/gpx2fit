# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`gpx2fit` is a C11 command-line tool that converts a GPX track (plus its
waypoints) into a Garmin FIT **course** file. The intended workflow is planning
a route in a tool like CalTopo, exporting GPX, and producing a `.fit` a Garmin
device can navigate. Only one external dependency: expat (XML parsing); `-lm`
for math. See `README.md` for the full user-facing behavior spec.

## Build & test

```sh
make            # build ./gpx2fit
make test       # C unit tests (ASan+UBSan) + shell/e2e tests against the binary
make asan       # separate ASan+UBSan build -> ./gpx2fit-asan
make clean
```

Prereqs: C11 compiler, `make`, `libexpat1-dev`, and `xxd` (ships with vim —
used to embed `data/default.map`).

Run a single unit test by building and running its binary directly:

```sh
make build-test/test_snap && ./build-test/test_snap
```

Run a single shell test against the built binary:

```sh
make gpx2fit && bash tests/test_cli.sh ./gpx2fit
```

Unit tests are `tests/test_*.c` (linked against all of `src/` except `main.c`,
always with ASan+UBSan so any sanitizer report aborts the test). Shell tests
are `tests/test_*.sh` plus the end-to-end `tests/run.sh`, each invoked with the
binary path as `$1`. Test fixtures live in `tests/data/`.

## Code generation gotcha

`src/default_map.h` is **generated** by the Makefile from `data/default.map`
via `xxd` (embeds the default waypoint-name→type rules into the binary). Edit
`data/default.map`, not the header. The checked-in `src/default_map.h` is
regenerated on build when the `.map` changes.

## Architecture

The whole program is a single linear pipeline in `main.c:main()` (see the
comment there). Options are fully parsed and `--pace`/`--start`/`--map`
validated *before* any real work, so bad input fails fast (exit 2 for usage,
exit 1 for runtime). The output file is written only after every earlier stage
succeeds — a failed run never leaves a partial `.fit`, and the stdout summary
is printed *after* the write.

Pipeline stages, each owned by a module in `src/`:

1. `symmap.c` — load waypoint name→type rules (`--map` file first, then the
   embedded `default_map.h`). Loaded first so a bad map fails before any work.
2. `gpx.c` — parse GPX 1.0/1.1 with expat into `struct course` (track points +
   waypoints). Concatenates all `<trkseg>` of the chosen `<trk>`.
3. `geo.c` — haversine distance primitives (spherical Earth).
4. `course.c` — the central `struct course` data model and cumulative distance
   computation (`course_compute_distances`).
5. `snap.c` — project each waypoint onto the nearest segment of the track
   within `--snap` meters; `--pass` policy for tracks that revisit a point
   (out-and-back / loop). Produces course points.
6. `names.c` — UTF-8-safe truncation of names on character boundaries.
7. `timing.c` — synthetic timestamps from `--start` and `--pace` (GPX plans
   have no times, FIT requires them); also parses those options.
8. `decimate.c` — optional Douglas-Peucker thinning for `--max-points`, always
   preserving first/last points and the point nearest each course point.
9. `fit_write.c` — serialize the FIT course file (binary encoding, CRCs).

`main.c` only orchestrates and prints; the real logic lives in the modules.
Each module has a matching `.h` describing its contract.

## Reference material

`reference/` holds a real Garmin-exported course (`.fit` + source `.gpx` +
decoded `.csv`) used as a decoding/format oracle. `PLAN.md` tracks the phased
build (phase numbers appear in commit messages and source comments);
`NOTES.md` / `NOTES_phase7.md` are working notes.
