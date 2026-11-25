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

Prerequisites:

- a C11 compiler (gcc or clang) and `make`
- the expat development headers and library (`libexpat1-dev`)
- `xxd`, used at build time to embed `data/default.map` into the binary

```sh
# Debian / Ubuntu (on older releases xxd is in the vim-common package)
sudo apt install build-essential libexpat1-dev xxd
```

Then, from the source directory:

```sh
make            # build ./gpx2fit
make test       # unit tests (built with ASan+UBSan) plus shell/end-to-end tests
make asan       # separate ASan+UBSan build -> ./gpx2fit-asan
make test-asan  # shell/end-to-end tests against ./gpx2fit-asan
make clean
```

`make test` needs a compiler with AddressSanitizer and UBSan support (any
recent gcc or clang).

### Install

There is no install step yet. `gpx2fit` is a single self-contained binary
(the default type rules are built in), so copy it to a directory on your
`PATH`:

```sh
mkdir -p ~/.local/bin
cp gpx2fit ~/.local/bin/
gpx2fit --help
```

## Quick start

```sh
./gpx2fit route.gpx route.fit
```

For example, with the reference route that is also the web app's sample (a
161.67 km course with 11 waypoints):

```sh
./gpx2fit reference/Rio_Del_Lago_100mi_2025.gpx route.fit
```

```
Read 'RDL 100 2025': 8409 trackpoints, 11 waypoints
Activity: trail-running
Track: 161.67 km, ascent 2523 m, descent 2521 m, 8409 points
  Willow Creek AS            17.33 km  offset   0.0 m
  Black Miners AS            27.61 km  offset   0.0 m
  Beals AS                   34.90 km  offset   0.0 m
  ...
  Granite Beach             153.86 km  offset   0.0 m
Snapped 11 of 11 waypoints (0 skipped)
Wrote 'route.fit' (11 course points)
```

The course-point list above is abbreviated; ten names have no built-in type
rule, so a warning for each goes to stderr. Warnings go to stderr; everything
else goes to stdout. The `Read` line is
printed as soon as the GPX file has been read. The rest of the summary is
printed only after the file has been written, so a run that fails at a later
stage shows the `Read` line and its error (plus, for a `--strict` waypoint
failure, the list of snapped waypoints).

## Usage

```
gpx2fit [options] input.gpx output.fit
```

| Option | Meaning |
| --- | --- |
| `--snap METERS` | Maximum distance from a waypoint to the track. Farther waypoints are skipped with a warning. Default `250`. |
| `--pass MODE` | Which pass of the track a waypoint snaps to when the track visits it more than once: `nearest` (default), `ordered`, `first`, `last`, `both` (alias `all`, one course point per distinct pass). See [Out-and-back routes](#out-and-back-and-loop-routes). |
| `--name TEXT` | Course name. Default: the GPX track name, or the file name if the track has none. |
| `--sport TYPE` | Activity type written to the FIT course (`sport` and `sub_sport`). Default `trail-running`. See [Activity type](#activity-type---sport). |
| `--pace PACE` | Synthetic pace, as m/s or min/km (for example `2.5` or `6:40`). Default 5 km/h. |
| `--start TIME` | Synthetic start time, UTC: `YYYY-MM-DD` or `YYYY-MM-DDTHH:MM[:SS][Z]` (for example `2025-06-01T07:30Z`). Time zone offsets such as `+02:00` are rejected. Default `2025-01-01T00:00:00Z`. |
| `--track N` | Use the Nth `<trk>` of the GPX file, counting from 1. Default: the longest. |
| `--stitch` | Order and join every `<trk>` into one course, reversing legs when needed. A waypoint named `start` anchors the beginning. See [Multiple tracks](#multiple-tracks). |
| `--split-tracks` | Write one FIT course per `<trk>`: `<output_stem>_1.fit`, `<output_stem>_2.fit`, ... See [Multiple tracks](#multiple-tracks). |
| `--max-points N` | Reduce the track to at most N points (N >= 2). Always keeps the first point, the last point, and the point nearest each course point. See [Reducing the track](#reducing-the-track---max-points). |
| `--map FILE` | Waypoint name-to-type rules, tried before the built-in defaults. |
| `--strict` | Make two warnings fatal: a waypoint skipped for being too far from the track, and a `--max-points` too small to hold the required points. Other warnings (no type for a name, truncated names, several tracks or segments) stay warnings. |
| `-q`, `--quiet` | Print only errors. |
| `-h`, `--help` | Show help and exit. |

### Activity type (`--sport`)

`--sport TYPE` chooses the activity stored in the FIT course's `sport` and
`sub_sport` fields, which controls how the device categorizes the course.
Names are case-insensitive, and `-` and `_` are interchangeable, so
`--sport trail-running` and `--sport TRAIL_Running` are the same type. Leading
and trailing whitespace is ignored.

| Value | Aliases | FIT `sport` / `sub_sport` |
| --- | --- | --- |
| `trail-running` (default) | `trail_running` | 1 / 3 |
| `hiking` | | 17 / 0 |
| `running` | | 1 / 0 |
| `cycling` | | 2 / 0 |
| `road-cycling` | `road_cycling` | 2 / 7 |
| `gravel` | `gravel-cycling`, `gravel_cycling` | 2 / 46 |
| `mtb` | `mountain-biking`, `mountain_biking` | 2 / 8 |
| `other` | `generic` | 0 / 0 |

An unknown (or empty) value is a usage error: exit 2, a clear error on stderr,
and no `.fit` file is written. The chosen name is printed in the summary.

### Exit status

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Runtime error: unreadable or malformed input, no trackpoints, out of memory, `--strict` failure, output not writable, bad `--map` file |
| 2 | Bad command line (unknown option, invalid value such as an unknown `--sport`, wrong number of arguments) |

The output file is written only after every earlier stage has succeeded, so a
failed run does not leave a partial `.fit` behind.

## CalTopo workflow

1. Draw your route as a single line (track) in CalTopo. If the export
   contains several tracks, the longest is used (see `--track`), or use
   `--stitch` / `--split-tracks` to combine or export them all.
2. Add markers for the things you want on the watch, and **name them with a
   keyword the type map recognizes**: "Water", "Camp", "Caution". Numbers and
   extra words are fine ("Water 2", "Upper camp"). Markers must be within
   `--snap` meters (default 250) of the line, or they are skipped.
3. Export as GPX and run `gpx2fit route.gpx route.fit`. Names that match no
   rule (such as "Start") still become course points, with the generic icon
   and a warning.
4. Send the `.fit` to your device through Garmin Connect, or copy it to the
   device's courses folder over USB.  The easist is probably to use Garmin
   connect Training & Planning -> Courses -> Import to upload the .fit.

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
- `type` is a FIT course point type name (case-insensitive) such as `water`,
  `danger`, `campsite`, or `generic`. The accepted names are the FIT
  `course_point` values: `generic`, `summit`, `valley`, `water`, `food`,
  `danger`, `left`, `right`, `straight`, `first_aid`, `fourth_category`,
  `third_category`, `second_category`, `first_category`, `hors_category`,
  `sprint`, `left_fork`, `right_fork`, `middle_fork`, `slight_left`,
  `sharp_left`, `slight_right`, `sharp_right`, `u_turn`, `segment_start`,
  `segment_end`, `campsite`, `aid_station`, `rest_area`, `general_distance`,
  `service`, `energy_gel`, `sports_drink`, `mile_marker`, `checkpoint`,
  `shelter`, `meeting_spot`, `overlook`, `toilet`, `shower`, `gear`,
  `sharp_curve`, `steep_incline`, `tunnel`, `bridge`, `obstacle`, `crossing`,
  `store`, `transition`, `navaid`, `transport`, `alert`, `info`. Which of
  these get a distinct icon depends on the device.
- Matching uses the full waypoint name, before any truncation.
- A name that matches nothing becomes `generic`, and a warning is printed
  (unless `--quiet`):
  `no type for 'Foo', using generic; add a line to --map`.
- An unknown type name or a malformed line produces a warning with the line
  number, and that line is skipped.

### Built-in defaults

The defaults live in [`data/default.map`](data/default.map) and are embedded
in the binary at build time, so there is no runtime file lookup. To change
them, edit that file and rebuild. The rules are, in order:

```
water     = water
h2o       = water
camp      = campsite
caution   = danger
hazard    = danger
danger    = danger
first aid = first_aid
summit    = summit
overlook  = overlook
shelter   = shelter
toilet    = toilet
food      = food
```

"Start" and "Ranger Station" deliberately have no rule, so they become
`generic` with a warning. Because matching is by substring, "camp" also
matches "Campbell Creek" and "water" also matches "Waterfall"; override such
cases in your `--map` file.

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

A missing or unreadable map file, one larger than 1 MiB, or one containing a
NUL byte is an error (exit 1), detected before any other work is done. Bad
lines inside a readable file are only warnings (see above).

## How it works

1. **Read** the GPX file. All `<trkseg>` of the chosen track are concatenated.
   With several `<trk>` elements the one given by `--track N` is used;
   without `--track` the longest is used and a warning is printed.
   `--stitch` joins all tracks instead (see [Multiple tracks](#multiple-tracks)).
   Elevation is kept only for points that have an `<ele>` element.
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
6. **Reduce** the track if `--max-points` was given.
7. **Write** the FIT course file, then print the summary.

### Out-and-back and loop routes

If the track passes the same waypoint more than once (an out-and-back, a loop
that revisits a junction), `--pass` decides which pass to use:

| Mode | Behavior |
| --- | --- |
| `nearest` | The pass where the track gets closest to the waypoint (default). |
| `ordered` | Choose passes so that distance along the course never decreases in GPX waypoint order. Useful when you list outbound waypoints first, then return waypoints. A waypoint that cannot keep order falls back to its nearest pass, with a warning if it had several passes. |
| `first` | The first pass. |
| `last` | The last pass. |
| `both` | Every distinct pass: a waypoint visited twice produces two course points, ordered by distance along the track. `all` is an alias. A waypoint visited once still produces one point. Adjacent trackpoints within one pass never duplicate a point. |

With `both`, `--max-points` keeps the trackpoint nearest each emitted course
point, including every pass of a repeatedly visited waypoint.

### Multiple tracks

By default a GPX file with several `<trk>` elements behaves as before: the
longest track is used with a warning, and `--track N` selects one by its 1-based
position in the file. Two options handle all of them:

- **`--stitch`** builds a single course. If a waypoint is named exactly `start`
  (case-insensitive, surrounding whitespace ignored), the track with the
  nearest endpoint to it becomes the first leg, reversed if its end is closer
  than its beginning. Without such a waypoint, the first track in file order
  starts the course. Each remaining track is then chosen by whichever of its
  endpoints is closest to the current end of the course, and reversed when
  that endpoint is its last point. The trackpoints are concatenated in that
  order *before* distances, waypoint snapping, synthetic times and
  `--max-points` are applied, so course points and their distances span the
  whole joined route. The stitched course is named after the first leg unless
  `--name` overrides it.

  ```sh
  ./gpx2fit --stitch legs.gpx course.fit
  ```

- **`--split-tracks`** writes one independent FIT course per track, named
  `<stem>_N.fit`, where `N` is the track's original 1-based position and
  `<stem>` is the output path without its `.fit` suffix. For example,
  `course.gpx course.fit` produces `course_1.fit`, `course_2.fit`, and so on.
  Every waypoint is snapped to every track within `--snap`; waypoints far from
  a given track are skipped for that course with a warning (a fatal error
  under `--strict`). `--name` applies to each output.

The two options cannot be combined with each other or with `--track`: pick one
way to handle the tracks. A file with a single `<trk>` works unchanged with
either option.

### Reducing the track (`--max-points`)

Dense tracks make large files. `--max-points N` thins the track to N points
using Douglas-Peucker: it repeatedly keeps the dropped point that lies
farthest from the line between its kept neighbors, so corners and bends
survive while straight stretches lose points.

- The first point, the last point, and, for each course point, the trackpoint
  nearest to it along the course are always kept.
- Surviving points are not changed. Their distances and times keep their
  full-resolution values, so total distance and course point distances and
  times are the same with or without `--max-points`.
- The total, ascent and descent in the summary are computed from the
  full-resolution track.
- The shape is judged on position only; elevation detail between kept points
  is lost, so a reduced track can show a smoother elevation profile.
- If the required points alone number more than N, all of them are kept and a
  warning is printed (an error under `--strict`). A track that already has N or
  fewer points is left as is.

### Limits and truncation

- Course names longer than 90 bytes are truncated (with a warning). The true
  device maximum is unknown; 90 bytes is a length known to work.
- Course point names longer than 15 bytes are truncated (with a warning),
  never splitting a UTF-8 character. 15 bytes is known to work on a Fenix 7X;
  longer names are untested.

## Output format

Printed to stdout (suppressed by `--quiet`):

```
Read '<course name>': <N> trackpoints, <M> waypoints
Activity: <sport type>
Track: <km> km, ascent <m> m, descent <m> m, <P> points
  <name>   <km> km  offset <m> m          one line per course point
Snapped <S> of <T> waypoints (<K> skipped)
Wrote '<output.fit>' (<C> course points)
```

- `Activity` is the type given by `--sport` (`trail-running` by default).
- `Track` totals come from the full-resolution track. With `--max-points` the
  point count shows the reduced number and the original, for example
  `1000 points (reduced from 1842)`.
- Ascent and descent ignore elevation changes under 1 m (noise). A track with
  no elevation data prints `no elevation data` instead.
- `offset` is how far the original waypoint was from the track, in meters.
- `--quiet` removes all of this; only errors are printed.

## Limitations

- GPX in, FIT course out only.
- Times are synthetic. They exist to satisfy the FIT format, not to describe a
  real activity.
- The Earth model is spherical (up to about 0.5% difference from WGS84),
  which is far below GPS noise for course purposes.
- Not intended for tracks near the poles.
- Garmin-internal undocumented FIT fields are not written.
- `--max-points` judges shape by position only, not elevation.

## Tested devices

| Device | Firmware | Result |
| --- | --- | --- |
| Garmin Fenix 7X | 27.18 | Works |

Tested with a 115.72 km course (5159 points, 30 course points) and a 3.39 km
CalTopo export (114 points, 4 course points), converted with default options:

- Garmin Connect import: both courses imported with no errors or warnings;
  distance, elevation profile and course point list looked right.
- On the watch: Up Ahead showed the course points in the right order, at the
  right distances, with the expected names and types.
- Direct USB copy of the `.fit` into the watch's courses folder also works.

The sample bundled with the web app is
`reference/Rio_Del_Lago_100mi_2025.gpx` (161.67 km, 8409 points, 11 course
points). It converts with default options, but has not itself been checked on
a device.

Other devices have not been tested. Please report the device model and
firmware if you try one.

## Web app (GitHub Pages)

A browser version is hosted at https://vloschiavo.github.io/gpx2fit/. It is the
same C converter compiled to WebAssembly with Emscripten: you load a GPX file,
the browser writes the FIT, and no track data is uploaded. All GPX processing
happens client-side, so hosting the app only requires serving static files.
Use **Load sample Rio Del Lago route** to try the bundled
`reference/Rio_Del_Lago_100mi_2025.gpx` course. The sections below cover
running that same bundle yourself.

## Self-hosting

### Docker

The published image is `vloschiavo/gpx2fit`. The runtime is `httpd:alpine` and
serves only the static `web/` bundle on container port 80 — there is no
server-side GPX processing in the container.

| Tag | Points to | Architectures |
| --- | --- | --- |
| `latest` | Newest stable build (`main` or a `v*` release) | `linux/amd64`, `linux/arm64` |
| `main` | Latest build from the `main` branch | `linux/amd64`, `linux/arm64` |
| `sha-<short>` | The commit that built it, e.g. `sha-abc1234` | `linux/amd64`, `linux/arm64` |
| `v1.2.3`, `1.2.3`, `1.2` | A `v*` release tag: the ref tag plus semver variants | `linux/amd64`, `linux/arm64` |
| `amd64` | Most recent build, single-platform | `linux/amd64` only |
| `arm64` | Most recent build, single-platform | `linux/arm64` only |

The first four are multi-arch indexes, so Docker picks the right variant
automatically. `amd64` and `arm64` are single-arch images, useful when you want
to pin a platform. Check what a tag contains with `imagetools`:

```sh
$ docker buildx imagetools inspect docker.io/vloschiavo/gpx2fit:latest
Name:      docker.io/vloschiavo/gpx2fit:latest
MediaType: application/vnd.oci.image.index.v1+json
Digest:    sha256:<digest>

Manifests:
  Name:      docker.io/vloschiavo/gpx2fit@sha256:<amd64-digest>
  MediaType: application/vnd.oci.image.manifest.v1+json
  Platform:  linux/amd64

  Name:      docker.io/vloschiavo/gpx2fit@sha256:<arm64-digest>
  MediaType: application/vnd.oci.image.manifest.v1+json
  Platform:  linux/arm64
```

Inspect `:amd64` or `:arm64` the same way to confirm each tag lists only its own
platform. Build provenance may add an attestation manifest, but there should be
no second runnable platform.

```sh
docker run -d --name gpx2fit -p 8080:80 vloschiavo/gpx2fit:main
```

The app is then at http://localhost:8080/. The same thing as
`docker-compose.yml`:

```yaml
services:
  gpx2fit:
    image: vloschiavo/gpx2fit:main
    container_name: gpx2fit
    restart: unless-stopped
    ports:
      - "8080:80"
```

To build the image from a checkout instead of pulling the hosted one:

```sh
docker build -f docker/Dockerfile -t gpx2fit-web:local .
```

The image's `docker/httpd-gpx2fit.conf` sets the headers the web app needs:
`AddType application/wasm .wasm` so the browser can load the module,
`X-Content-Type-Options: nosniff`, `no-cache, must-revalidate` for
`index.html`, `sw.js` and `version.json` (so the update check notices new
deployments immediately), and `public, max-age=3600` for `.wasm`, `.js` and
`.css`. If you serve `web/` with a different web server, reproduce those
rules.

### HAProxy SSL offload

To serve the app over HTTPS, terminate TLS at HAProxy and forward plain HTTP
to the container. HAProxy passes on the real client address and the original
scheme so the container's logging shows the browser's IP, not the proxy's.
A complete `/etc/haproxy/haproxy.cfg` (the certificate file is a PEM with the
certificate chain and private key concatenated):

```haproxy
global
    log /dev/log local0
    maxconn 4096
    user haproxy
    group haproxy
    daemon

defaults
    log     global
    mode    http
    option  httplog
    option  dontlognull
    retries 3
    timeout connect 5s
    timeout client  30s
    timeout server  30s

frontend gpx2fit_front
    bind *:80
    bind *:443 ssl crt /etc/haproxy/certs/gpx2fit.pem
    http-request redirect scheme https unless { ssl_fc }

    # TLS ends here, so tell the app the browser's request was HTTPS.
    http-request set-header X-Forwarded-Proto https if { ssl_fc }

    default_backend gpx2fit_back

backend gpx2fit_back
    balance roundrobin
    option forwardfor
    option httpchk GET /
    http-check expect status 200
    server gpx2fit1 gpx2fit:80 check inter 5s fall 3 rise 2
```

`option forwardfor` adds `X-Forwarded-For` with the client IP seen by HAProxy,
and `http-request set-header ... if { ssl_fc }` adds
`X-Forwarded-Proto: https` on the TLS listener. In this example `gpx2fit:80`
is the container name on a Docker network shared with HAProxy (as in the
compose example above); if HAProxy runs on the host and the container was
started with `-p 8080:80`, use `127.0.0.1:8080` instead. The health check
expects `GET /` to return HTTP 200.

The image enables `mod_remoteip` and trusts `X-Forwarded-For` **only from
private proxy addresses** — see the `RemoteIPInternalProxy` lines in
`docker/httpd-gpx2fit.conf` (loopback, `10.0.0.0/8`, `172.16.0.0/12`,
`192.168.0.0/16`, and the IPv6 ULA/link-local ranges). Keep HAProxy on one of
those ranges, typically on the same Docker network as the app; a direct
connection from a public IP is ignored (with a published port, Apache sees the
private bridge gateway instead, so the header is still trusted). Its access
log, written to stdout/stderr, records the resolved client IP (`%a`) and the
scheme as `proto=%{X-Forwarded-Proto}i`, so `docker logs gpx2fit` shows both:

```
203.0.113.42 - - [24/Nov/2026:12:00:00 +0000] "GET / HTTP/1.1" 200 1234 "-" "Mozilla/5.0 ..." proto=https
```

## License

MIT (see `LICENSE`).
