# gpx2fit

Convert a GPX track and its waypoints into a Garmin FIT **course** file.

You plan a route in a mapping tool such as CalTopo, export it as GPX, and run
`gpx2fit`. The result is a `.fit` course that a Garmin watch or bike computer
can follow, with your waypoints turned into named course points ("Up Ahead"
entries and map icons for water, camps, hazards, and so on).

- Written in C (C11), one dependency: [expat](https://libexpat.github.io/).
- Input: GPX 1.0 or 1.1. Output: FIT course. GeoJSON and KML are not supported.
- No `<sym>` parsing. Waypoint **types come from waypoint names** (see
  [Waypoint types](#waypoint-types)), because CalTopo exports contain no
  `<sym>`.

## Build

Requires a C11 compiler, `make`, and the expat development headers.

```sh
# Debian / Ubuntu
sudo apt install build-essential libexpat1-dev

make            # build ./gpx2fit
make test       # unit tests
make asan       # build with AddressSanitizer
make clean
```

## Quick start

```sh
./gpx2fit route.gpx route.fit
```

Typical output:

```
Read 'Lost Coast Day 2': 1842 trackpoints, 5 waypoints
warning: waypoint 'Old Cabin' is 400 m from the track (limit 250 m); skipped
  Water 1                    3.10 km  offset   4.2 m
  Camp                       7.85 km  offset  11.0 m
  Caution                    9.40 km  offset   2.7 m
Snapped 4 of 5 waypoints (1 skipped)
Wrote 'route.fit' (4 course points)
```

Warnings go to stderr; everything else goes to stdout.

## Usage

```
gpx2fit [options] input.gpx output.fit
```

| Option | Meaning |
| --- | --- |
| `--snap METERS` | Maximum distance from a waypoint to the track. Farther waypoints are skipped with a warning. Default `250`. |
| `--pass MODE` | Which pass of the track a waypoint snaps to when the track visits it more than once: `nearest` (default), `ordered`, `first`, `last`. See [Out-and-back routes](#out-and-back-and-loop-routes). |
| `--name TEXT` | Course name. Default: the GPX track name, or the file name if the track has none. |
| `--pace PACE` | Synthetic pace, as m/s or min/km (for example `2.5` or `6:40`). Default 5 km/h. |
| `--start TIME` | Synthetic start time, UTC: `YYYY-MM-DD` or `YYYY-MM-DDTHH:MM:SSZ`. Default `2025-01-01T00:00:00Z`. |
| `--track N` | Use the Nth `<trk>` of the GPX file. Default: the longest. |
| `--max-points N` | Reduce the track to at most N points (N >= 2). |
| `--map FILE` | Waypoint name-to-type rules, tried before the built-in defaults. |
| `--strict` | Treat warnings as errors (currently: any skipped waypoint fails the run). |
| `-q`, `--quiet` | Print only errors. |
| `-h`, `--help` | Show help and exit. |

### Exit status

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Runtime error: unreadable or malformed input, no trackpoints, out of memory, `--strict` failure, output not writable, bad `--map` file |
| 2 | Bad command line (unknown option, invalid value, wrong number of arguments) |

The output file is written only after every earlier stage has succeeded, so a
failed run does not leave a partial `.fit` behind.

## CalTopo workflow

1. Draw your route as a single line (track) in CalTopo.
2. Add markers for the things you want on the watch, and **name them with a
   keyword the type map recognizes**: "Water", "Camp", "Caution". Numbers and
   extra words are fine ("Water 2", "Upper camp").
3. Export as GPX and run `gpx2fit`.
4. Send the `.fit` to your device through Garmin Connect, or copy it to the
   device's courses folder over USB.

## Waypoint types

A course point's type decides which icon Garmin draws. Because the GPX files
carry no symbol, `gpx2fit` picks the type from the waypoint **name** using a
list of rules.

### Rule format

One rule per line:

```
keyword = type
```

- `#` starts a comment; blank lines are ignored.
- `keyword` is matched as a **case-insensitive substring** anywhere in the
  waypoint name.
- The **first** matching rule in file order wins. Put specific keywords above
  general ones.
- `type` is a FIT course point type name such as `water`, `danger`,
  `campsite`, or `generic`.
- A name that matches nothing becomes `generic`, and a warning is printed
  (unless `--quiet`):
  `no type for 'Foo', using generic; add a line to --map`.
- An unknown type name or a malformed line produces a warning with the line
  number, and that line is skipped.

### Built-in defaults

The defaults live in [`data/default.map`](data/default.map) and are embedded
in the binary at build time, so there is no runtime file lookup. They include
at least:

```
water   = water
camp    = campsite
caution = danger
```

### Your own rules (`--map`)

`--map FILE` loads your rules **before** the defaults. Your rules win, and
names you do not cover still fall through to the defaults.

```
# my.map
waterfall = generic     # beats the default "water" rule because it is first
summit    = summit
```

```sh
./gpx2fit --map my.map route.gpx route.fit
```

A missing or unreadable map file is an error (exit 1) and is detected before
any other work is done.

## How it works

1. **Read** the GPX file. All `<trkseg>` of the chosen track are concatenated.
   With several `<trk>` elements the longest is used, or the one given by
   `--track N`, and a warning is printed. Elevation is kept only for points
   that have an `<ele>` element.
2. **Distances.** Cumulative distance along the track is computed with the
   haversine formula (spherical Earth, mean radius).
3. **Snap.** Each waypoint is projected onto the nearest part of the track
   within `--snap` meters. The snapped position and distance along the course
   become a course point. Course points are sorted by distance along the
   course; waypoints at the same distance keep their GPX order.
4. **Types and names.** Types come from the rules above. Names are truncated
   safely on UTF-8 boundaries.
5. **Synthetic times.** GPX tracks from planning tools usually have no
   timestamps, but FIT courses need them, so times are generated from the
   start time and pace. Course point times are interpolated from their
   distance along the course.
6. **Write** the FIT course file.

### Out-and-back and loop routes

If the track passes the same waypoint more than once (an out-and-back, a loop
that revisits a junction), `--pass` decides which pass to use:

| Mode | Behavior |
| --- | --- |
| `nearest` | The pass where the track gets closest to the waypoint (default). |
| `ordered` | Choose passes so that distance along the course never decreases in GPX waypoint order. Useful when you list outbound waypoints first, then return waypoints. A waypoint that cannot keep order falls back to its nearest pass, with a warning if it had several passes. |
| `first` | The first pass. |
| `last` | The last pass. |

### Limits and truncation

- Course names longer than 90 bytes are truncated (with a warning). The true
  device maximum is unknown; 90 bytes is a length known to work.
- Course point names are truncated to the FIT `course_point.name` field
  length, never splitting a UTF-8 character.

## Output format

Printed to stdout (suppressed by `--quiet`):

```
Read '<course name>': <N> trackpoints, <M> waypoints
  <name>   <km> km  offset <m> m          one line per course point
Snapped <S> of <T> waypoints (<K> skipped)
Wrote '<output.fit>' (<P> course points)
```

`offset` is how far the original waypoint was from the track, in meters.

## Limitations

- GPX in, FIT course out only.
- Times are synthetic. They exist to satisfy the FIT format, not to describe a
  real activity.
- The Earth model is spherical (up to about 0.5% difference from WGS84),
  which is far below GPS noise for course purposes.
- Not intended for tracks near the poles.
- Garmin-internal undocumented FIT fields are not written.
- `--max-points` and the extended summary (total distance, ascent and descent)
  are still being implemented; see the project plan.

## Tested devices

Not yet recorded. Real-device validation (Garmin Connect import, watch
navigation, USB copy) is tracked in `PLAN.md` phase 9. Please report the
device model and firmware if you try it.

## License

MIT (see `LICENSE`).
