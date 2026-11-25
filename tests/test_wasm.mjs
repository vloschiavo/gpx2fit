import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import createModule from '../web/dist/gpx2fit.js';

const wasm = await createModule();
const gpx = await readFile(
  new URL('./data/caltopo_sample.gpx', import.meta.url),
  'utf8',
);

const argumentTypes = Array(13).fill('number');
const utf8Encoder = new TextEncoder();

function heapString(value) {
  const bytes = utf8Encoder.encode(value == null ? '' : String(value));
  const pointer = wasm._malloc(bytes.length + 1);
  assert.notEqual(pointer, 0, 'could not allocate wasm string');
  wasm.HEAPU8.set(bytes, pointer);
  wasm.HEAPU8[pointer + bytes.length] = 0;
  return pointer;
}

function convert(gpxXml, {
  snap = 250, pass = 'nearest', maxPoints = 0, strict = 0,
  track = 0, stitch = true,
} = {}) {
  const pointers = [
    heapString(gpxXml),
    heapString(null),
    heapString(null),
    heapString(pass),
  ];
  try {
    const result = wasm.ccall(
      'gpx2fit_convert',
      'number',
      argumentTypes,
      [
        pointers[0], pointers[1], pointers[2],
        17, 0, snap, pointers[3], 5 / 3.6, 1104624000, maxPoints, strict,
        track, stitch ? 1 : 0,
      ],
    );
    assert.notEqual(result, 0, 'gpx2fit_convert returned a null result pointer');

    try {
      const rc = wasm._gpx2fit_result_rc(result);
      const jsonPointer = wasm._gpx2fit_result_json(result);
      assert.notEqual(jsonPointer, 0, 'result has no JSON metadata');
      const metadata = JSON.parse(wasm.UTF8ToString(jsonPointer));
      const length = wasm._gpx2fit_result_len(result);
      const dataPointer = wasm._gpx2fit_result_data(result);
      if (length > 0)
        assert.notEqual(dataPointer, 0, 'nonempty FIT result has a null data pointer');
      const fit = wasm.HEAPU8.slice(dataPointer, dataPointer + length);
      return { rc, metadata, fit };
    } finally {
      wasm._gpx2fit_free(result);
    }
  } finally {
    for (const pointer of pointers) wasm._free(pointer);
  }
}

/* Independent bitwise CRC-16/ARC implementation (reflected polynomial A001). */
function fitCrc16(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? ((crc >>> 1) ^ 0xa001) : (crc >>> 1);
  }
  return crc & 0xffff;
}

assert.equal(fitCrc16(new TextEncoder().encode('123456789')), 0xbb3d);

const converted = convert(gpx);
assert.equal(converted.rc, 0);
assert.equal(converted.metadata.ok, true);
assert.equal(converted.metadata.error, null);

assert.deepEqual(
  {
    distance_m: converted.metadata.distance_m,
    ascent_m: converted.metadata.ascent_m,
    descent_m: converted.metadata.descent_m,
    has_elevation: converted.metadata.has_elevation,
    track_points: converted.metadata.track_points,
    course_points: converted.metadata.course_points,
    waypoints_total: converted.metadata.waypoints_total,
    waypoints_snapped: converted.metadata.waypoints_snapped,
    waypoints_skipped: converted.metadata.waypoints_skipped,
    course_name: converted.metadata.course_name,
    sport: converted.metadata.sport,
    sub_sport: converted.metadata.sub_sport,
  },
  {
    distance_m: 3394.42,
    ascent_m: 44,
    descent_m: 49,
    has_elevation: true,
    track_points: 114,
    course_points: 4,
    waypoints_total: 5,
    waypoints_snapped: 4,
    waypoints_skipped: 1,
    course_name: 'caltopo_sample',
    sport: 17,
    sub_sport: 0,
  },
);
assert.deepEqual(
  converted.metadata.course_points_list.map(({ name, km, type }) => ({ name, km, type })),
  [
    { name: 'Start', km: 5.19716e-5, type: 0 },
    { name: 'Camp', km: 0.990309, type: 27 },
    { name: 'Water', km: 1.55648, type: 3 },
    { name: 'Ranger Station', km: 3.39442, type: 0 },
  ],
);
assert.deepEqual(converted.metadata.skipped_waypoints, ['Caution']);
assert.equal(converted.metadata.track_points_list.length, 114);
assert.ok(converted.metadata.track_points_list.every((point) =>
  Number.isFinite(point.lat) && Number.isFinite(point.lon)));
assert.ok(Math.abs(converted.metadata.track_points_list[0].lat - 38.74343547315443) < 1e-7);
assert.ok(Math.abs(converted.metadata.track_points_list[0].lon - (-121.1515714546992)) < 1e-7);
assert.equal(converted.metadata.skipped_waypoints_list.length, 1);
const [caution] = converted.metadata.skipped_waypoints_list;
assert.equal(caution.name, 'Caution');
assert.equal(caution.type, 5);
assert.equal(Math.round(caution.offset_m), 477);
assert.ok(Math.abs(caution.lat - 38.76396271808355) < 1e-7);
assert.ok(Math.abs(caution.lon - (-121.15371662664155)) < 1e-7);
assert.ok(caution.km > 0 && caution.km < converted.metadata.distance_m / 1000);
assert.ok(Array.isArray(converted.metadata.warnings));

/* Decimation changes the FIT track, not the original GPX/snap preview. */
const vShapeGpx = `<?xml version="1.0"?><gpx xmlns="http://www.topografix.com/GPX/1/1" version="1.1">
  <wpt lat="0" lon="0.01"><name>Camp</name></wpt>
  <trk><trkseg>
    <trkpt lat="0" lon="0"></trkpt>
    <trkpt lat="0.01" lon="0.01"></trkpt>
    <trkpt lat="0" lon="0.02"></trkpt>
  </trkseg></trk>
</gpx>`;
const vShapeFull = convert(vShapeGpx);
const vShapeDecimated = convert(vShapeGpx, { maxPoints: 2 });
assert.equal(vShapeDecimated.rc, 0);
assert.equal(vShapeDecimated.metadata.track_points, 2);
assert.equal(vShapeDecimated.metadata.track_points_list.length, 3);
assert.equal(vShapeDecimated.metadata.waypoints_skipped, 1);
assert.equal(vShapeDecimated.metadata.skipped_waypoints_list.length, 1);
assert.equal(Math.round(vShapeDecimated.metadata.skipped_waypoints_list[0].offset_m), 786);
assert.match(vShapeDecimated.metadata.warnings.join('\n'), /786 m.*skipped/);
assert.deepEqual(vShapeDecimated.metadata.track_points_list, vShapeFull.metadata.track_points_list);
assert.deepEqual(vShapeDecimated.metadata.course_points_list, vShapeFull.metadata.course_points_list);
assert.deepEqual(vShapeDecimated.metadata.skipped_waypoints_list, vShapeFull.metadata.skipped_waypoints_list);

const fit = converted.fit;
assert.ok(fit.length > 16, 'FIT output is too short or has no data');
const view = new DataView(fit.buffer, fit.byteOffset, fit.byteLength);
assert.equal(fit[0], 14, 'unexpected FIT header size');
assert.equal(fit[1], 0x10, 'unexpected FIT protocol version');
assert.equal(view.getUint16(2, true), 21220, 'unexpected FIT profile version');
assert.equal(view.getUint32(4, true), fit.length - 16, 'incorrect FIT data size');
assert.deepEqual([...fit.subarray(8, 12)], [0x2e, 0x46, 0x49, 0x54]);
assert.equal(
  view.getUint16(12, true),
  fitCrc16(fit.subarray(0, 12)),
  'incorrect FIT header CRC',
);
assert.equal(
  view.getUint16(fit.length - 2, true),
  fitCrc16(fit.subarray(0, fit.length - 2)),
  'incorrect whole-file FIT CRC',
);

/* A multi-hundred-KB GPX used to overflow ccall's 64 KB string stack. */
const largeGpx = await readFile(
  new URL('../reference/Sonora_Pass_to_Tuolumne_Meadows.gpx', import.meta.url),
  'utf8',
);
assert.ok(largeGpx.length > 100_000);
const large = convert(largeGpx);
assert.equal(large.rc, 0);
assert.equal(large.metadata.ok, true);
assert.equal(large.metadata.track_points, 5159);
assert.equal(large.metadata.course_points, 30);
assert.ok(Math.abs(large.metadata.distance_m - 115716) < 1);
assert.ok(large.fit.length > 90_000);

/* T1: the same GPX produces one stitched course or one course per track. */
const multiGpx = await readFile(new URL('./data/multi_track.gpx', import.meta.url), 'utf8');
const stitched = convert(multiGpx, { snap: 2000, stitch: true });
assert.equal(stitched.rc, 0);
assert.equal(stitched.metadata.ok, true);
assert.equal(stitched.metadata.stitch, true);
assert.equal(stitched.metadata.track, 0);
assert.equal(stitched.metadata.track_points, 9);
assert.equal(stitched.metadata.course_name, 'Head');
assert.deepEqual(
  stitched.metadata.course_points_list.map(({ name }) => name),
  ['Start', 'Water', 'Camp'],
);

const splitTrack = convert(multiGpx, { snap: 2000, stitch: false, track: 2 });
assert.equal(splitTrack.rc, 0);
assert.equal(splitTrack.metadata.stitch, false);
assert.equal(splitTrack.metadata.track, 2);
assert.equal(splitTrack.metadata.track_points, 3);
assert.equal(splitTrack.metadata.course_name, 'Middle');
assert.deepEqual(
  splitTrack.metadata.course_points_list.map(({ name }) => name),
  ['Water'],
);

const badPair = convert(multiGpx, { snap: 2000, stitch: true, track: 2 });
assert.equal(badPair.rc, 1);
assert.equal(badPair.metadata.ok, false);
assert.match(badPair.metadata.error, /choose either one track or stitching/);

const failed = convert('');
assert.equal(failed.rc, 1);
assert.equal(failed.metadata.ok, false);
assert.equal(failed.metadata.error, 'missing GPX input');
assert.equal(failed.fit.length, 0);
assert.equal(failed.metadata.distance_m, 0);
assert.equal(failed.metadata.track_points, 0);

console.log('test_wasm.mjs: FIT header, CRCs, metadata, large GPX, multi-track stitch/split, and errors passed');
