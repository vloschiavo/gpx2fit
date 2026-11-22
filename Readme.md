# gpx2tcx

A lightweight command-line utility for Ubuntu/Linux that converts **CalTopo GPX** exports (containing both a track and standalone waypoints) into **Garmin TCX** courses with snapped `<CoursePoint>` cues.

## Why This Exists

Garmin Connect ignores standalone `<wpt>` tags when importing third-party GPX files into **Courses**, dropping your trail markers, water sources, and junctions. Standard CLI tools like `gpsbabel` convert GPX to TCX, but they do not snap detached waypoints onto the track geometry as Garmin `<CoursePoint>` entries.

`gpx2tcx.py` solves this by:
1. Calculating cumulative distances and generating monotonic timestamps along your GPX `<trk>`.
2. Snapping each `<wpt>` to the nearest `<trkpt>` using Haversine distance within a configurable snap threshold.
3. Writing a valid Garmin Training Center Database (`.tcx`) file ready for import into Garmin Connect and syncing to your watch's **Up Ahead** screen.

## Requirements

* **Python 3.9+** (uses only the Python standard library; `ET.indent` requires Python 3.9+)
* **Optional CLI inspection tools:** `libxml2-utils` (`xmllint`)

```bash
sudo apt update && sudo apt install -y python3 libxml2-utils
```

## Installation

1. Save `gpx2tcx.py` into a directory on your `$PATH` (such as `~/bin`):

```bash
mkdir -p ~/bin
cp gpx2tcx.py ~/bin/gpx2tcx.py
chmod +x ~/bin/gpx2tcx.py
```

2. Ensure `~/bin` is in your `$PATH` (add `export PATH="$HOME/bin:$PATH"` to your `~/.bashrc` if needed).

## Usage

```bash
gpx2tcx.py <input.gpx> <output.tcx> [max_snap_meters]
```

### Arguments

| Argument | Required | Description |
| :--- | :---: | :--- |
| `input.gpx` | Yes | GPX file exported from CalTopo (**GPX Type: Track**). |
| `output.tcx` | Yes | Destination path for the generated Garmin TCX file. |
| `max_snap_meters` | No | Maximum distance (in meters) a waypoint can be from the track to be snapped as a Course Point. Defaults to `250.0`. |

### Examples

Convert a CalTopo export using the default `250m` snap radius:

```bash
gpx2tcx.py trail_run.gpx trail_run.tcx
```

Increase the snap radius to `500m` to include off-trail peaks or campsites:

```bash
gpx2tcx.py alpine_loop.gpx alpine_loop.tcx 500
```

## CalTopo to Garmin Workflow

1. **Export from CalTopo:**
   * Select your route/track and waypoints.
   * Choose **Export > GPX** and set **GPX Type** to **Track** (not *Route*) to preserve full trail geometry.
2. **Inspect the GPX (Optional):**
   ```bash
   xmllint --xpath '//*[local-name()="wpt"]/*[local-name()="name"]/text()' trail_run.gpx; echo
   ```
3. **Convert to TCX:**
   ```bash
   gpx2tcx.py trail_run.gpx trail_run.tcx
   ```
4. **Verify Course Points (Optional):**
   ```bash
   xmllint --xpath '//*[local-name()="CoursePoint"]' trail_run.tcx
   ```
5. **Import to Garmin Connect:**
   * Go to **Training & Planning > Courses > Import** on Garmin Connect Web.
   * Upload `trail_run.tcx`, save the course, and send it to your device.

## Limitations & Notes

* **15-Character Name Limit:** Garmin watches truncate `<CoursePoint>` and `<Name>` fields at ~15 characters on the watch UI. Front-load critical keywords in CalTopo (e.g., `H2O Bear Crk` instead of `Spring at Bear Creek`).
* **PointType Icons:** All waypoints are currently assigned `<PointType>Generic</PointType>` (standard pin icon). Supported Garmin TCX types include `Water`, `Food`, `Summit`, `Danger`, `Left`, `Right`, and `Straight`.
* **Out-and-Back Routes:** Waypoints near shared out-and-back sections snap to whichever pass (outbound or inbound) is geometrically closer.

## License

MIT