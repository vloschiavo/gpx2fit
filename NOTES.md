# gpx2fit: Recon Notes (Phase 0)

Sources: `tests/data/caltopo_sample.gpx`, `tests/data/reference_course.fit`
(Garmin Connect course, serial 373491188, `course-service-26.22`), FIT profile
from the `garmin-fit-sdk` Python package, **Profile 21.222** (the reference file
header says protocol 1.0 / profile 21220).

## 0.1 / 0.2 GPX findings (CalTopo export)

| Question | Answer |
|---|---|
| GPX version | **1.1** (`version="1.1"`, `creator="CALTOPO"`) |
| Namespace | default `http://www.topografix.com/GPX/1/1`; also declares `xmlns:gpxx` (Garmin extensions v3) but **no `gpxx` elements are used** |
| XML declaration | `<?xml version="1.0"?>` (no encoding attribute, so UTF-8 default) |
| Whitespace | whole file is a single line, no indentation |
| Metadata | `<metadata><name><![CDATA[caltopo_sample]]></name></metadata>`, **name is in a CDATA section** (expat handles it, but name text can arrive in several callback chunks) |
| Tracks | 1 `<trk>`, 1 `<trkseg>`, 114 `<trkpt>` |
| Track name | `<trk><name>caltopo_sample</name>` (plain text, not CDATA) |
| Trackpoint fields | `lat`, `lon` attributes + `<ele>` only. **No `<time>`.** |
| Elevation | present on all 114 points; mostly whole meters, 10 points have fractional values (e.g. `143.89425420262572`). Range 140 to 156 m |
| Waypoints | 5 `<wpt>`, **all before `<trk>`** in the file, fields: `lat`, `lon`, `<name>` only |
| `<sym>`, `<type>`, `<desc>`, `<time>`, `<extensions>` | **none present** (0 occurrences) |
| Waypoint names | `Start`, `Ranger Station`, `Water`, `Caution`, `Camp` |
| Total length | ~3.39 km (haversine), so shorter than the 5 to 10 km the plan asked for |
| Duplicate points | 9 consecutive trackpoints with identical lat/lon (zero-length segments). Good natural test for task 3.1 |
| Point spacing | very uneven: from 0 m (duplicates) to a few hundred meters |

Approximate waypoint offsets from the track (local projection, my own check,
to be confirmed by task 3.3):

| Waypoint | Offset | Distance along |
|---|---|---|
| Start | 0.2 m | 0.000 km |
| Camp | 14.0 m | 0.990 km |
| Water | 5.4 m | 1.557 km |
| Caution | **477 m** (the deliberately far waypoint; the plan said about 400 m) | ~2.215 km (nearest point) |
| Ranger Station | 3.1 m | 3.394 km (the track end) |

Consequences for the plan:

- **Phase 5 problem:** CalTopo wrote no `<sym>`. Type mapping therefore has to
  work from **waypoint names** ("Water" to `water`, "Camp" to `campsite`,
  "Caution" to `danger`, and so on). Task 5.1 cannot list sym values from this
  file. Export a map where you actually pick marker symbols, and check whether
  the symbol shows up at all. If CalTopo never exports it, drop `<sym>`
  parsing and keep `--map`.
- Names may arrive split across expat character-data callbacks (CDATA and
  entities), so the reader must accumulate text and use it on the end tag.
- `has_ele` handling is still needed even though this file has elevation.
- No timestamps, so Phase 6 synthetic times are required as planned.
- Two waypoint decisions to confirm: `Start` sits at the first point and
  `Ranger Station` at the last point, so both exercise the clamp-to-end cases.

## 0.3 Reference FIT file

- File: `tests/data/reference_course.fit`, 4960 bytes. Header: 14 bytes,
  protocol version 0x10, profile version 21220, data size 4944, `.FIT`,
  header CRC `e498`; 2-byte file CRC trailer.
- Hex viewer check done (`xxd`): the header is `0e10 e452 5013 0000 2e46 4954 e498`.
- It is a Garmin Connect export (manufacturer 1 garmin, product 65534 =
  `connect`), not a file I created. The course is **4.13 km hiking**,
  "Foresthill Rd to Ruck-A-Chucky", with one course point, "Water Stop".

## 0.4 FIT profile: fields to write

Base type codes: enum 0x00, uint8 0x02, string 0x07, sint32 0x85, uint16 0x84,
uint32 0x86, uint32z 0x8C. Sizes: enum/uint8 1, uint16 2, sint32/uint32 4,
string variable. Scale/offset convention: `value = raw / scale - offset`
(so `raw = (value + offset) * scale`).

Global message numbers: file_id 0, course 31, lap 19, event 21, record 20,
course_point 32, file_creator 49.

### file_id (0)

| # | Name | Base type | Size | Scale | Offset | Units | In reference |
|---|---|---|---|---|---|---|---|
| 0 | type | enum | 1 | 1 | 0 | | 6 (course) |
| 1 | manufacturer | uint16 | 2 | 1 | 0 | | 1 (garmin) |
| 2 | product | uint16 | 2 | 1 | 0 | | 65534 |
| 3 | serial_number | uint32z | 4 | 1 | 0 | | 373491188 |
| 4 | time_created | uint32 | 4 | 1 | 0 | s since FIT epoch | yes |
| 5 | number | uint16 | 2 | 1 | 0 | | 1 |

### course (31)

| # | Name | Base type | Size | Scale | Offset | Units | In reference |
|---|---|---|---|---|---|---|---|
| 4 | sport | enum | 1 | 1 | 0 | | 17 (hiking) |
| 5 | name | string | **31 bytes** in ref (30 chars + NUL) | | | | yes |
| 7 | sub_sport | enum | 1 | 1 | 0 | | 0 |
| 6 | capabilities | uint32z | 4 | 1 | 0 | bit flags | **not written** |
| 254 | (undocumented) | uint16 | 2 | | | | 0, written by Garmin |

### lap (19)

| # | Name | Base type | Size | Scale | Offset | Units |
|---|---|---|---|---|---|---|
| 254 | message_index | uint16 | 2 | 1 | 0 | |
| 253 | timestamp | uint32 | 4 | 1 | 0 | s |
| 2 | start_time | uint32 | 4 | 1 | 0 | s |
| 3 / 4 | start_position_lat / long | sint32 | 4 | 1 | 0 | semicircles |
| 5 / 6 | end_position_lat / long | sint32 | 4 | 1 | 0 | semicircles |
| 7 | total_elapsed_time | uint32 | 4 | 1000 | 0 | s |
| 8 | total_timer_time | uint32 | 4 | 1000 | 0 | s |
| 9 | total_distance | uint32 | 4 | 100 | 0 | m |
| 21 | total_ascent | uint16 | 2 | 1 | 0 | m |
| 22 | total_descent | uint16 | 2 | 1 | 0 | m |
| 13 | avg_speed | uint16 | 2 | 1000 | 0 | m/s |
| 27 / 28 | nec_lat / nec_long | sint32 | 4 | 1 | 0 | semicircles (north-east corner) |
| 29 / 30 | swc_lat / swc_long | sint32 | 4 | 1 | 0 | semicircles (south-west corner) |

The reference lap definition has 17 fields, in this on-disk order (taken
from the definition message): 2, 253, 3, 4, 5, 6, 21, 22, 29, 30, 27, 28, 13,
7, 8, 9, 254. Use that as the target order for task 7.8.
The names for 27 to 30 (`nec_*`, `swc_*`) come from the SDK profile;
`fitparse` shows them as `unknown_27..30` because its bundled profile is older.
The reference has `total_ascent` = 0 and `total_descent` = 339 and
`avg_speed` = 0.

### record (20)

| # | Name | Base type | Size | Scale | Offset | Units |
|---|---|---|---|---|---|---|
| 253 | timestamp | uint32 | 4 | 1 | 0 | s |
| 0 | position_lat | sint32 | 4 | 1 | 0 | semicircles |
| 1 | position_long | sint32 | 4 | 1 | 0 | semicircles |
| 2 | altitude | uint16 | 2 | **5** | **500** | m |
| 5 | distance | uint32 | 4 | 100 | 0 | m |
| 6 | speed | uint16 | 2 | 1000 | 0 | m/s |
| 121, 122 | (undocumented) | uint16 | 2 | | | written by Garmin; 121 is always 0, 122 looks like a running counter |

Altitude check: reference first raw value 5302 gives 5302/5 - 500 = **560.4 m**; last raw value 3607 gives 221.4 m (a descent of about 339 m, matching the lap's total_descent of 339).
Invalid value for uint16 is 0xFFFF.

### course_point (32)

| # | Name | Base type | Size | Scale | Offset | Units |
|---|---|---|---|---|---|---|
| 254 | message_index | uint16 | 2 | 1 | 0 | |
| 1 | timestamp | uint32 | 4 | 1 | 0 | s |
| 2 / 3 | position_lat / long | sint32 | 4 | 1 | 0 | semicircles |
| 4 | distance | uint32 | 4 | 100 | 0 | m |
| 5 | type | enum | 1 | 1 | 0 | course_point type |
| 6 | name | string | **11 bytes in the reference** ("Water Stop" + NUL; sized to the string) | | | |
| 8 | favorite | enum | 1 | | | bool, optional, not written |

**course_point.name length:** the profile does not give a maximum. The
reference sizes the field to the actual text. Treat the safe maximum as
**unknown until verified**; start by truncating to 15 characters + NUL (a limit
commonly quoted for watch displays, not confirmed here) and test on a device
(tasks 5.4 and 9.2). The course `name` is 31 bytes in the reference, which
shows at least 30 characters is accepted.

### event (21) and file_creator (49), also in the reference

event: timestamp (253, uint32), event (0, enum), event_type (1, enum),
event_group (4, uint8). Reference writes `timer/start` and a final
`timer/stop_disable_all`(9). file_creator: software_version (0, uint16,
**absent** in reference), hardware_version (1, uint8, value 0), plus an
undocumented field 2 string "course-service-26.22".

### course_point type enum (what I plan to use)

0 generic, 1 summit, 2 valley, 3 water, 4 food, 5 danger, 6 left, 7 right,
8 straight, 9 first_aid, 27 campsite, 28 aid_station, 29 rest_area,
35 checkpoint, 36 shelter, 37 meeting_spot, 38 overlook, 39 toilet, 40 shower.
(Full list in the profile; the plan's Phase 5 map should start from these.)

### Other constants

- FIT epoch: 1989-12-31 00:00:00 UTC = Unix 631065600.
- Semicircles = degrees * 2^31 / 180. Checked on the reference: lat
  464950366 gives 38.9717 deg and lon -1443050446 gives -120.9551 deg, which
  is the Auburn/Foresthill area, as expected for this course.
- sport 17 = hiking, 1 running, 2 cycling, 11 walking; sub_sport 0 generic.
- `file` type 6 = course.

## 0.5 Reference file decode

`tests/data/reference_course.csv` (1875 rows; columns: msg_index, message,
field, field_num, raw_value, scaled_value, units). Produced with `fitparse`;
values were cross-checked against the Garmin SDK decoder (integrity check
passed, no decode errors).

**Message order in the reference (file order):**

1. `file_id` (1)
2. `course` (1)
3. `lap` (1)
4. `event` start (1)
5. `record` x 50
6. `course_point` (1)
7. `record` x 133
8. `event` stop (1)
9. `file_creator` (1)

Definition messages sit directly before the first data message of each type
(local types 0..5 for file_id, course, lap, event, record, course_point; the
event and file_creator definitions reuse local types 3 and 0 later).

Key observations:

- **Course points are interleaved with the records by timestamp**, not placed
  in a block before or after them. The course point at 20:41:43 comes right
  before the first record with timestamp >= that time (record index 50 of 183).
  This answers the question in task 7.10 for this file, and it is different
  from the "before or after" choice the plan assumed.
- `file_creator` comes **last**, and `lap` comes **before** the records.
- Record timestamps are about 7 to 14 s apart (irregular, not fixed pace).
- Garmin writes the lap and records with speed and the unknown fields 121/122;
  we can omit these unless a device rejects the file.
- The lap bounding box fields (27 to 30) are present; lap total_distance is
  4126.89 m and equals the last record's distance.
- `time_created` in this file decodes to 2026-11-22, which is when it was
  exported; the track timestamps are from 2025-06-28.
- Definition message for `file_id` is 6 fields: 0, 1, 2, 4, 3, 5 (that order).

## Phase 5 findings: names and point types

### 5.1 (a) Waypoint names in the exports

Only one export exists so far (`tests/data/caltopo_sample.gpx`), so this
inventory is those five names. Add rows here when other exports show new names.

| Waypoint name | Intended FIT type | Value | Rule in `data/default.map` |
|---|---|---|---|
| `Water` | water | 3 | `water = water` |
| `Camp` | campsite | 27 | `camp = campsite` |
| `Caution` | danger | 5 | `caution = danger` |
| `Start` | generic | 0 | none (warns "no type for 'Start'") |
| `Ranger Station` | generic | 0 | none (warns "no type for 'Ranger Station'") |

Decision still open: `Start` and `Ranger Station` are left as generic with a
warning. If you want an icon, pick a type and add a line to `default.map`
(for example `ranger = aid_station` or `start = checkpoint`).

`Caution` is 477 m from the track, so the default `--snap 250` skips it and its
type never reaches the file. Use `--snap 500` (or more) to include it.

`default.map` also has a few extra rules that no export has needed yet
(`hazard`, `danger`, `first aid`, `summit`, `overlook`, `shelter`, `toilet`,
`food`). They are marked as extras in the file. Delete any you do not want.

Matching is a case-insensitive **substring** match, so `camp` also matches
`Campbell Creek` and `water` also matches `Waterfall`. Override with a line in
your `--map` file, which is tried first (`waterfall = generic`).

`<sym>` is still absent from this export, as noted in 0.2. The plan says not to
add `<sym>` parsing, so none exists. Names are the only input.

### 5.1 (b) FIT course_point types

Checked in this session against `garmin_fit_sdk` 21.222.0
(`Profile['types']['course_point']`), the same profile as 0.4. Every value in
the 0.4 table is correct. The full enum is:

0 generic, 1 summit, 2 valley, 3 water, 4 food, 5 danger, 6 left, 7 right,
8 straight, 9 first_aid, 10 fourth_category, 11 third_category,
12 second_category, 13 first_category, 14 hors_category, 15 sprint,
16 left_fork, 17 right_fork, 18 middle_fork, 19 slight_left, 20 sharp_left,
21 slight_right, 22 sharp_right, 23 u_turn, 24 segment_start, 25 segment_end,
**(26 does not exist)**, 27 campsite, 28 aid_station, 29 rest_area,
30 general_distance, 31 service, 32 energy_gel, 33 sports_drink, 34 mile_marker,
35 checkpoint, 36 shelter, 37 meeting_spot, 38 overlook, 39 toilet, 40 shower,
41 gear, 42 sharp_curve, 43 steep_incline, 44 tunnel, 45 bridge, 46 obstacle,
47 crossing, 48 store, 49 transition, 50 navaid, 51 transport, 52 alert, 53 info.

`src/symmap.c` accepts any of these names (any letter case) as the right-hand
side of a rule. Whether a given watch draws an icon for every type is not
verified (task 9.2).

### 5.4 / 5.5 decisions

- Rule order: user `--map` rules, then the embedded `default.map`. First match wins.
- Names are matched in full, then truncated for the file, so a long name still
  gets its type.
- Limits live in `src/names.h`: `COURSE_POINT_NAME_MAX` = **15 bytes**
  (provisional, from 0.4, verify in 9.2) and `COURSE_NAME_MAX` = **90 bytes**
  (revisit in 9.1). Both count bytes without the NUL; the FIT string field is
  limit + 1. Truncation never splits a UTF-8 character, so a name with accented
  letters can end up a byte or two shorter than the limit.
- `xxd` is now a build requirement (it generates `src/default_map.h`).
  `src/default_map.h` is generated, so add it to `.gitignore`.

## Phase 6 findings: synthetic timestamps

The CalTopo export has no `<time>`, so every time is invented from a start time
and one constant pace. Code is in `src/timing.c` (named `timing`, not `time`,
because `src/time.h` would shadow the system `<time.h>` under `-Isrc`).

- **Epoch:** FIT epoch = Unix 631065600 (1989-12-31 00:00:00 UTC), checked with
  independent date arithmetic and in `tests/test_timing.c`.
- **`--start`:** UTC only. Accepted: `YYYY-MM-DD` (midnight), and
  `YYYY-MM-DDTHH:MM[:SS][Z]`. Offsets like `+02:00` are rejected, not guessed.
  Default is `2025-01-01T00:00:00Z` (FIT 1104624000), fixed so output is
  reproducible.
- **FIT date range:** the FIT protocol reads a `date_time` below `0x10000000` as
  a device uptime, not a date. That limit is **1998-07-03T21:24:16Z**, so an
  earlier `--start` is rejected. The last valid time is `0xFFFFFFFE`
  (2126-02-06T06:28:14Z) because `0xFFFFFFFF` is the "invalid" value; a course
  that would run past it is an error (exit 1), not a silent wrap.
- **`--pace`:** a value with a `:` is **min/km** (`6:40` = 2.5 m/s, seconds are
  two digits, 00 to 59). A plain number is **m/s** (`2.5`). So a plain number is
  never read as minutes. Zero, negative, signs, exponents, `inf`/`nan` and hex
  are rejected (exit 2). Default is 5 km/h (1.389 m/s).
- **Times:** `t = start + round(dist / speed)` in whole seconds, from
  `tp[i].dist`. Repeated points get the same time, and times never decrease. On
  the sample (3.39 km) the default pace gives about 40 min 40 s.
- **Course points:** `t` is linearly interpolated between the two trackpoints on
  either side of the point's distance and rounded to a second (halves round up),
  so a course point always falls between its neighbours' times. A point beyond
  either end takes that end's time. This runs after snapping and typing and must
  stay before `--max-points` decimation (8.2), so decimation cannot move them.
- `--dump-options` still prints the raw `--pace` and `--start` text. They are
  validated after the dump check, so the 1.4 option test is unaffected.

Open items from this phase:

- **Duplicate timestamps.** The sample has 9 repeated points. They now get equal
  times, so the FIT file would contain records with identical timestamps (the
  Garmin reference has none). Not known whether a watch minds. Decide in 7.x /
  8.2 whether to drop zero-length duplicates before writing, and check on a
  device in 9.2.
- **Uneven spacing.** The reference file has records every 7 to 14 s. Ours are
  proportional to distance, and this sample has gaps of a few hundred metres, so
  gaps of a few minutes are possible. Check how the watch draws the course.
- **Pace is constant.** No slope adjustment. Only `--pace` changes it.

## Not done / open items

- I could not download the official FIT SDK zip (`Profile.xlsx`, C headers):
  that domain is not reachable from this sandbox. Field data above is from the
  SDK's own profile data shipped in `garmin-fit-sdk` 21.222, which is the same
  source. Fetch the zip locally when you want the C headers and `FitCSVTool`.
- The `course_point.name` maximum length is unknown (see 0.4).
