## Phase 7 findings: FIT encoder

Code: `src/fit_write.{h,c}`. Tests: `tests/test_fit.c` (unit, ASan/UBSan),
`tests/test_fit_decode.sh` (SDK decode + diff), golden file
`tests/data/fit_fixture_main.expected`. Entry points: `fit_encode_course()` and
`fit_write_course()`; `struct fit_options` has `time_created` (0 = time of the
first trackpoint, so output is reproducible) and `sport` (default hiking).

### How it was verified

- CRC: matches the CRC-16/ARC check value (`"123456789"` = 0xBB3D) and
  reproduces the header CRC (0x98E4) and file CRC (0xD1FC) stored in the Garmin
  reference file.
- Both SDK decoders (FitCSVTool 21.222 and the Python `garmin-fit-sdk`) read the
  fixtures with no errors or warnings.
- Round trip of the Garmin reference course: its 183 records and the "Water Stop"
  course point were fed through the encoder and decoded again. Latitude,
  longitude, altitude, distance and timestamp are identical for every record;
  the course point is identical and sits before record 50, as in the original.
  Lap start/end position, total distance, start time, sport and name match.
  Differences: `total_elapsed_time` 4952.0 s vs 4952.203 s (we write whole
  seconds), `total_descent` 340 vs 339 (different noise filter).

### Decisions

- **Byte order.** The reference is big-endian (architecture byte 1); we write
  little-endian (plan 7.2). So definition messages are not byte-identical to
  the reference. The test checks instead that every field we define exists in
  the reference's definition of the same message with the same number and base
  type (and size, except strings).
- **Fields written.** file_id: type, manufacturer, product, time_created
  (no serial_number, no number). course: sport, name (91-byte field), sub_sport
  (no field 254). lap: the reference order without the derived fields; avg_speed
  is computed (Garmin writes 0). record: position, timestamp, distance, altitude
  (no speed, no 121/122). course_point: all seven reference fields; the name
  field is always `COURSE_POINT_NAME_MAX + 1` bytes, NUL-padded (the reference
  sizes it to the one name). Names are cut again at write time, never inside a
  UTF-8 character, as a safety net. No file_creator.
- **Events added.** The reference has `event` timer start and
  `stop_disable_all`; they are written (not in the original 7.x list).
- **Message order** (answers 7.10): file_id, course, lap, event start, records
  with each course point inserted before the first record whose distance is >=
  the point's distance (a point past the last record goes after it), event stop.
- **Repeated points.** A trackpoint with the same lat, lon and distance as the
  previous one is not written (closes the "duplicate timestamps" item from
  phase 6). Course points are unaffected.
- **Lap.** `timestamp` is the lap end time (Garmin's file has the start time
  there). Ascent/descent use `FIT_ELE_THRESHOLD_M` (1 m) and are the invalid
  value 0xFFFF when no point has elevation. 8.3 should use the same threshold.
  The bounding box (fields 27 to 30) is a plain min/max, so a track crossing the
  antimeridian gets a wide box.
- **Header.** Protocol 0x10, profile 21220, as in the reference.
- **Limits.** `FIT_TOOSHORT` below 2 trackpoints; `FIT_TOOBIG` above 65535
  course points (message_index is a uint16).
- **+180 degrees** encodes as 0x7FFFFFFE, because 0x7FFFFFFF means "invalid".

### Open items for phase 9

- file_id manufacturer is 255 (development) and product 0. If Garmin Connect
  rejects the file in 9.1, try manufacturer 1 (garmin), product 65534.
- `course_point.name` limit (15 bytes) and `COURSE_NAME_MAX` (90) are still
  unverified on a device. The encoder follows `src/names.h`.
- Not written, add only if a device or Connect needs them: record speed, lap
  fields 27 to 30 are written but enhanced speeds are not, file_creator,
  course field 254.
- A zero-message FIT file (header only) is accepted by the Python SDK's
  integrity check but FitCSVTool prints "No messages or message definitions
  found" for it. So 7.3's "done when" was checked with the Python SDK and the
  independent CRC values; real files decode cleanly in both.
