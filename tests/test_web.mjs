import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import createModule from '../web/dist/gpx2fit.js';
import { initApp, gpxTrackBlocks, gpxHasStartWaypoint, reorderGpxTracks } from '../web/app.js';

const wasm = await createModule();
const sampleGpx = await readFile(new URL('./data/caltopo_sample.gpx', import.meta.url), 'utf8');
const largeGpx = await readFile(
  new URL('../reference/Sonora_Pass_to_Tuolumne_Meadows.gpx', import.meta.url),
  'utf8',
);
const indexHtml = await readFile(new URL('../web/index.html', import.meta.url), 'utf8');

class FakeElement {
  constructor(tagName, id = '') {
    this.tagName = tagName;
    this.id = id;
    this.value = '';
    this.hidden = false;
    this.dataset = {};
    this.children = [];
    this.listeners = new Map();
    this.attributes = new Map();
    this.style = { setProperty(name, value) { this[name] = value; } };
    this.className = '';
    this.classList = {
      add(name) {
        if (!this.owner.className.split(/\s+/).includes(name))
          this.owner.className = `${this.owner.className} ${name}`.trim();
      },
      remove(name) {
        this.owner.className = this.owner.className.split(/\s+/)
          .filter((part) => part && part !== name).join(' ');
      },
      owner: this,
    };
    this.textContent = '';
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  dispatch(type) {
    for (const listener of this.listeners.get(type) || [])
      listener({ preventDefault() {}, dataTransfer: null, persisted: false });
  }

  setAttribute(name, value) { this.attributes.set(name, value); }
  getAttribute(name) { return this.attributes.has(name) ? this.attributes.get(name) : null; }
  removeAttribute(name) { this.attributes.delete(name); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; }
}

class FakeBounds {
  constructor() { this.points = []; }

  extend(value) {
    if (value instanceof FakeBounds) this.points.push(...value.points);
    else if (Array.isArray(value) && value.length === 2) this.points.push(value);
  }

  isValid() { return this.points.length > 0; }
}

function createHarness(gpx, { moduleFactory = async () => wasm, promptImpl = null } = {}) {
  const ids = [
    'app', 'conversion-controls', 'dropzone', 'gpx-file', 'load-sample',
    'source-name', 'course-name', 'activity-type', 'pace', 'pace-unit',
    'pass-mode', 'snap-distance', 'snap-slider', 'snap-field', 'map-rules', 'stitch-tracks',
    'status', 'error-message', 'download-list',
    'results', 'warnings', 'warning-status', 'download-fit', 'stat-distance',
    'stat-track-points', 'stat-course-points', 'stat-waypoints', 'route-map',
    'map-fallback', 'waypoint-table-body', 'waypoint-table-wrap', 'track-list',
  ];
  const elements = Object.fromEntries(ids.map((id) => [id, new FakeElement('div', id)]));
  elements['activity-type'].value = 'trail-running';
  elements.pace.value = '12:00';
  elements['pace-unit'].value = 'min/km';
  elements['pass-mode'].value = 'nearest';
  elements['snap-distance'].value = '250';
  elements['snap-slider'].value = '250';
  elements['stitch-tracks'].checked = true;

  const doc = {
    getElementById(id) { return elements[id] || null; },
    createElement(tagName) { return new FakeElement(tagName); },
    createTextNode(text) { return { textContent: text }; },
  };
  const blobs = [];
  const win = {
    setTimeout,
    clearTimeout,
    URL: {
      createObjectURL(blob) {
        blobs.push(blob);
        return `blob:test-${blobs.length}`;
      },
      revokeObjectURL() {},
    },
    addEventListener() {},
  };
  const mapState = { groups: [], fittedBounds: [] };
  const leaflet = {
    map() {
      return {
        invalidateSize() {},
        fitBounds(bounds) {
          mapState.fittedBounds.push(bounds.points.map((point) => [...point]));
        },
      };
    },
    tileLayer() { return { addTo() { return this; } }; },
    layerGroup() {
      const group = {
        layers: [],
        addTo() { return this; },
        clearLayers() { this.layers = []; },
      };
      mapState.groups.push(group);
      return group;
    },
    latLngBounds() { return new FakeBounds(); },
    polyline(coordinates) {
      const bounds = new FakeBounds();
      coordinates.forEach((point) => bounds.extend(point));
      return {
        addTo(group) { group.layers.push({ kind: 'line', coordinates }); return this; },
        getBounds() { return bounds; },
      };
    },
    divIcon(options) { return options; },
    marker(coordinates, options) {
      const record = {
        kind: 'marker',
        coordinates,
        options,
        addTo(group) { group.layers.push(record); return record; },
        bindPopup(popup) { record.popup = popup; return record; },
      };
      return record;
    },
  };

  let currentGpx = gpx;
  const sampleRequests = [];
  initApp({
    doc,
    win,
    leaflet,
    moduleFactory,
    promptImpl,
    fetchImpl: async (url) => {
      sampleRequests.push(url);
      return { ok: true, status: 200, text: async () => currentGpx };
    },
  });

  async function waitFor(predicate, description) {
    for (let i = 0; i < 300; i++) {
      if (predicate()) return;
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
    throw new Error(`Timed out waiting for ${description}: state=${elements.status.dataset.state} err=${elements['error-message'].textContent} status=${elements.status.textContent}`);
  }

  async function converted() {
    await waitFor(
      () => ['success', 'error'].includes(elements.status.dataset.state),
      'conversion',
    );
    if (elements.status.dataset.state === 'error')
      throw new Error(elements['error-message'].textContent);
  }

  return {
    elements,
    mapState,
    sampleRequests,
    blobs,
    async loadSample() {
      elements['load-sample'].dispatch('click');
      await converted();
    },
    async setSnap(value, event = 'change') {
      elements['snap-distance'].value = String(value);
      elements['snap-slider'].value = String(value);
      elements['snap-distance'].dispatch(event);
      if (event === 'change') await converted();
    },
    markers(groupIndex = 0) {
      const group = mapState.groups[groupIndex];
      return group ? group.layers.filter((layer) => layer.kind === 'marker') : [];
    },
    markerNamed(name, groupIndex = 0) {
      return this.markers(groupIndex).find((marker) => marker.options.title.startsWith(`${name} —`));
    },
    trackCards() {
      return elements['track-list'].children.filter((child) => child.className === 'track-card');
    },
    trackCard(trackIndex) {
      return this.trackCards().find((card) => card.dataset.trackIndex === String(trackIndex)) || null;
    },
    downloads() {
      const cardDownloads = this.trackCards()
        .map((card) => card.children.find((child) => child.tagName === 'a' && child.className.includes('download-button')))
        .filter(Boolean);
      if (cardDownloads.length) return cardDownloads;
      const list = elements['download-list'].children;
      return list.length ? list : (elements['download-fit'].hidden ? [] : [elements['download-fit']]);
    },
    rows() {
      return elements['waypoint-table-body'].children.filter((row) => row.dataset.status);
    },
    rowNamed(name) {
      return this.rows().find((row) => row.children[0].textContent === name);
    },
    waitFor,
    converted,
  };
}

function rowDistance(row) {
  return row.children[2].textContent;
}

function statusButton(row) {
  return row.children[1].children[0];
}

async function latestFitBytes(harness) {
  const blob = harness.blobs.at(-1);
  return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
}

assert.match(
  indexHtml,
  /<p class="sr-only" id="warning-status" role="status" aria-live="polite" aria-atomic="true"><\/p>/,
  'persistent atomic warning live region',
);

/* The sample button loads the reference Rio Del Lago route from the bundle. */
assert.match(indexHtml, /Load sample Rio Del Lago route/, 'sample button names Rio Del Lago');
assert.doesNotMatch(indexHtml, /Load sample CalTopo route/, 'sample button no longer names CalTopo');
const sampleLoader = createHarness(sampleGpx);
await sampleLoader.loadSample();
assert.deepEqual(sampleLoader.sampleRequests, ['./Rio_Del_Lago_100mi_2025.gpx']);
assert.equal(
  sampleLoader.elements['source-name'].textContent,
  'Rio_Del_Lago_100mi_2025.gpx',
  'source label matches the sample file',
);

/* Trail Running is the default activity in the web UI. */
assert.match(
  indexHtml,
  /<option value="trail-running" selected>Trail Running<\/option>/,
  'Trail Running is selected by default',
);
assert.doesNotMatch(
  indexHtml,
  /<option value="hiking" selected>/,
  'Hiking is not selected by default',
);

/* The default selector value must reach WASM as sport=1 / sub_sport=3. */
const sportCalls = [];
const recordingWasm = Object.create(wasm);
recordingWasm.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert')
    sportCalls.push({ sport: args[3][3], subSport: args[3][4] });
  return wasm.ccall(...args);
};
const defaultSport = createHarness(sampleGpx, { moduleFactory: async () => recordingWasm });
await defaultSport.loadSample();
assert.deepEqual(sportCalls, [{ sport: 1, subSport: 3 }]);

/* Snap changes refresh Caution's marker, table status, and both warning outputs. */
const sample = createHarness(sampleGpx);
assert.equal(sample.elements['warning-status'].hidden, false);
await sample.loadSample();
assert.equal(sample.rowNamed('Caution').dataset.status, 'skipped');
assert.match(sample.markerNamed('Caution').options.icon.html, /is-skipped/);
assert.match(sample.elements.warnings.textContent, /Caution.*477 m/s);
assert.equal(sample.elements['warning-status'].textContent, sample.elements.warnings.textContent);
await sample.setSnap(500);
assert.equal(sample.rowNamed('Caution').dataset.status, 'snapped');
assert.doesNotMatch(sample.markerNamed('Caution').options.icon.html, /is-skipped/);
assert.doesNotMatch(sample.elements.warnings.textContent, /Caution|skipped/);
assert.equal(sample.elements['warning-status'].textContent, sample.elements.warnings.textContent);

/* Large GPX files must fit in the wasm heap, not ccall's 64 KB string stack. */
const largeRoute = createHarness(largeGpx);
await largeRoute.loadSample();
assert.equal(largeRoute.elements.status.dataset.state, 'success');
assert.equal(largeRoute.elements['stat-track-points'].textContent, '5159');
assert.equal(largeRoute.elements['stat-course-points'].textContent, '30');
assert.equal(largeRoute.rows().length, 30);

/* A deferred input clears stale warnings, then announces the current result. */
const snapInput = sample.elements['snap-distance'];
snapInput.value = '250';
snapInput.dispatch('input');
assert.equal(sample.elements['warning-status'].textContent, '');
await sample.converted();
assert.match(sample.elements['warning-status'].textContent, /Caution.*477 m/s);

/* Pass changes update the snapped location and route distance on an out-and-back. */
const outBackGpx = `<?xml version="1.0"?><gpx xmlns="http://www.topografix.com/GPX/1/1" version="1.1">
  <wpt lat="0" lon="0.005"><name>Camp</name></wpt><trk><trkseg>
    <trkpt lat="0" lon="0"><ele>1</ele></trkpt>
    <trkpt lat="0" lon="0.01"><ele>1</ele></trkpt>
    <trkpt lat="0.0001" lon="0"><ele>1</ele></trkpt>
  </trkseg></trk>
</gpx>`;
const outBack = createHarness(outBackGpx);
await outBack.loadSample();
const nearestDistance = rowDistance(outBack.rowNamed('Camp'));
const nearestCoordinates = [...outBack.markerNamed('Camp').coordinates];
outBack.elements['pass-mode'].value = 'last';
outBack.elements['pass-mode'].dispatch('change');
await outBack.converted();
assert.notEqual(rowDistance(outBack.rowNamed('Camp')), nearestDistance);
assert.notEqual(outBack.markerNamed('Camp').coordinates[0], nearestCoordinates[0]);

/* Invalid map lines are skipped with warnings; valid rules on either side still apply. */
const customMap = createHarness(sampleGpx);
await customMap.loadSample();
customMap.elements['map-rules'].value = [
  'water = food',
  'not a rule',
  'camp = shelter',
  'waterfall = mystery',
].join('\n');
customMap.elements['map-rules'].dispatch('change');
await customMap.converted();
assert.equal(customMap.elements.status.dataset.state, 'success');
assert.equal(customMap.rowNamed('Water').children[4].children[1].textContent, 'Food');
assert.equal(customMap.rowNamed('Camp').children[4].children[1].textContent, 'Shelter');
assert.match(
  customMap.elements.warnings.textContent,
  /custom\.map:2: malformed line, expected 'keyword = type'/,
);
assert.match(customMap.elements.warnings.textContent, /custom\.map:4: unknown type 'mystery'/);
assert.equal(
  customMap.elements['warning-status'].textContent,
  customMap.elements.warnings.textContent,
);

/* The dateline is a short jump, and every overlay uses the same unwrapped frame. */
const antimeridianGpx = `<?xml version="1.0"?><gpx xmlns="http://www.topografix.com/GPX/1/1" version="1.1">
  <wpt lat="0.001" lon="-179.95"><name>Camp</name></wpt>
  <wpt lat="1" lon="-179.95"><name>Caution</name></wpt>
  <trk><trkseg>
    <trkpt lat="0" lon="179.9"><ele>1</ele></trkpt>
    <trkpt lat="0" lon="-179.9"><ele>1</ele></trkpt>
  </trkseg></trk>
</gpx>`;
const antimeridian = createHarness(antimeridianGpx);
await antimeridian.loadSample();
const line = antimeridian.mapState.groups[0].layers.find((layer) => layer.kind === 'line');
assert.ok(Math.abs(line.coordinates[0][1] - 179.9) < 1e-8);
assert.ok(Math.abs(line.coordinates[1][1] - 180.1) < 1e-8);
assert.ok(Math.abs(line.coordinates[1][1] - line.coordinates[0][1]) < 1);
assert.equal(antimeridian.markers().length, 2);
for (const marker of antimeridian.markers())
  assert.ok(marker.coordinates[1] > 179 && marker.coordinates[1] < 181);
for (const [, lon] of antimeridian.mapState.fittedBounds.at(-1))
  assert.ok(lon > 179 && lon < 181);

/* A superseded conversion waiting on WASM cannot overwrite newer settings. */
let resolveModule;
const moduleGate = new Promise((resolve) => { resolveModule = resolve; });
const deferredConversions = [];
const instrumentedWasm = Object.create(wasm);
instrumentedWasm.ccall = (...args) => {
  deferredConversions.push({ name: args[0], snap: args[3][5] });
  return wasm.ccall(...args);
};
const deferred = createHarness(sampleGpx, { moduleFactory: () => moduleGate });
deferred.elements['load-sample'].dispatch('click');
await deferred.waitFor(
  () => deferred.elements.status.textContent.startsWith('Converting'),
  'deferred conversion',
);
deferred.elements['snap-distance'].value = '500';
deferred.elements['snap-slider'].value = '500';
deferred.elements['snap-distance'].dispatch('change');
resolveModule(instrumentedWasm);
await deferred.converted();
assert.deepEqual(
  deferredConversions,
  [{ name: 'gpx2fit_convert', snap: 500 }],
  'stale 250 m conversion must not reach WASM or render',
);
assert.equal(deferred.rowNamed('Caution').dataset.status, 'snapped');
assert.doesNotMatch(deferred.elements['warning-status'].textContent, /Caution|skipped/);

/* T1: UI checkbox reaches WASM and multi-track output is one or many FITs. */
const multiGpx = await readFile(new URL('./data/multi_track.gpx', import.meta.url), 'utf8');
const noStartGpx = await readFile(
  new URL('./data/multi_track_no_start.gpx', import.meta.url), 'utf8');

assert.match(
  indexHtml,
  /<input id="stitch-tracks"[^>]*type="checkbox"[^>]*checked>/,
  'stitch checkbox is present and checked by default',
);
assert.match(indexHtml, /id="download-list"/, 'split downloads have a container');
assert.deepEqual(
  gpxTrackBlocks(multiGpx).map((track) => track.name),
  ['Head', 'Middle', 'Tail'],
  'tracks are sorted by natural numerical/name order',
);
assert.equal(gpxHasStartWaypoint(multiGpx), true);
assert.equal(gpxHasStartWaypoint(noStartGpx), false);
assert.equal(
  gpxHasStartWaypoint('<gpx><wpt><name>Start Here</name></wpt></gpx>'),
  false,
  'only an exact "Start" name matches the core anchor convention',
);
assert.equal(
  gpxHasStartWaypoint(
    '<gpx><wpt><name>Start</name><desc>trailhead</desc><sym>Dot</sym></wpt></gpx>',
  ),
  true,
  'other <wpt> children do not hide an exact "Start" name',
);
assert.equal(
  gpxHasStartWaypoint(
    '<gpx><wpt><desc>trailhead</desc><name>  start  </name><sym>Dot</sym></wpt></gpx>',
  ),
  true,
  'case and surrounding whitespace are ignored like in the core',
);
assert.ok(
  reorderGpxTracks(noStartGpx, 3).indexOf('<name>Head</name>')
    < reorderGpxTracks(noStartGpx, 3).indexOf('<name>Tail</name>'),
  'reordering moves the chosen first leg ahead of the others',
);

const stitchedCalls = [];
const stitchRecorder = Object.create(wasm);
stitchRecorder.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert')
    stitchedCalls.push({ track: args[3][11], stitch: args[3][12] });
  return wasm.ccall(...args);
};
const stitchedUI = createHarness(multiGpx, { moduleFactory: async () => stitchRecorder });
await stitchedUI.loadSample();
assert.deepEqual(stitchedCalls, [{ track: 0, stitch: 1 }]);
assert.equal(stitchedUI.elements['stat-track-points'].textContent, '9');
assert.deepEqual(
  stitchedUI.rows().map((row) => row.children[0].textContent),
  ['Start', 'Water', 'Camp'],
);
assert.equal(stitchedUI.downloads().length, 1);
assert.equal(stitchedUI.downloads()[0].download, 'Head.fit');
assert.equal(stitchedUI.elements['download-list'].hidden, true);

/* Regression: an obvious "Start" with extra <wpt> children must not trigger
 * the first-track prompt or a reorder that the core would then ignore. */
const startWithExtraChildren = multiGpx.replace(
  '<name>Start</name>',
  '<name>Start</name><desc>trailhead</desc><sym>Dot</sym>',
);
let extraChildPrompts = 0;
const extraChildCalls = [];
const extraChildRecorder = Object.create(wasm);
extraChildRecorder.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert') {
    extraChildCalls.push({
      track: args[3][11],
      stitch: args[3][12],
      gpx: wasm.UTF8ToString(args[3][0]),
    });
  }
  return wasm.ccall(...args);
};
const extraChildUI = createHarness(startWithExtraChildren, {
  moduleFactory: async () => extraChildRecorder,
  promptImpl: () => {
    extraChildPrompts += 1;
    return '1';
  },
});
await extraChildUI.loadSample();
assert.equal(extraChildPrompts, 0, 'no prompt when the GPX has an obvious Start waypoint');
assert.deepEqual(
  extraChildCalls.map(({ track, stitch }) => ({ track, stitch })),
  [{ track: 0, stitch: 1 }],
);
assert.ok(
  extraChildCalls[0].gpx.includes('<name>Start</name><desc>trailhead</desc><sym>Dot</sym>'),
  'the original GPX is stitched without reordering',
);
assert.equal(extraChildUI.elements['stat-track-points'].textContent, '9');
assert.deepEqual(
  extraChildUI.rows().map((row) => row.children[0].textContent),
  ['Start', 'Water', 'Camp'],
);
assert.equal(extraChildUI.downloads().length, 1);

const splitCalls = [];
const splitRecorder = Object.create(wasm);
splitRecorder.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert')
    splitCalls.push({ track: args[3][11], stitch: args[3][12] });
  return wasm.ccall(...args);
};
const splitUI = createHarness(multiGpx, { moduleFactory: async () => splitRecorder });
splitUI.elements['stitch-tracks'].checked = false;
await splitUI.loadSample();
assert.deepEqual(
  splitCalls,
  [{ track: 3, stitch: 0 }, { track: 2, stitch: 0 }, { track: 1, stitch: 0 }],
  'unstitched tracks converted in natural name order, preserving original indices',
);
// In unstitched multi-track mode, global stats are not the primary view; check cards.
assert.equal(splitUI.trackCards().length, 3);
assert.match(splitUI.elements.status.textContent, /3 FIT courses are ready/);
assert.deepEqual(
  splitUI.downloads().map((link) => link.download),
  ['Head.fit', 'Middle.fit', 'Tail.fit'],
);
assert.deepEqual(
  splitUI.downloads().map((link) => link.textContent),
  ['Download Head.fit', 'Download Middle.fit', 'Download Tail.fit'],
);

/* Toggling back clears the per-track links and restores the single download. */
splitUI.elements['stitch-tracks'].checked = true;
splitUI.elements['stitch-tracks'].dispatch('change');
await splitUI.converted();
assert.equal(splitUI.downloads().length, 1);
assert.equal(splitUI.elements['download-list'].hidden, true);
assert.equal(splitCalls.at(-1).stitch, 1);

/* Without a "Start" waypoint, loops ask which leg to ride first. */
const promptCalls = [];
const reorderedGpx = [];
const loopRecorder = Object.create(wasm);
loopRecorder.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert') reorderedGpx.push(wasm.UTF8ToString(args[3][0]));
  return wasm.ccall(...args);
};
const loopUI = createHarness(noStartGpx, {
  moduleFactory: async () => loopRecorder,
  promptImpl: (message, defaultValue) => {
    promptCalls.push({ message, defaultValue });
    return '1'; // first in natural order: Head
  },
});
await loopUI.loadSample();
assert.equal(promptCalls.length, 1);
assert.match(promptCalls[0].message, /no "Start" waypoint/);
assert.equal(promptCalls[0].defaultValue, '1');
assert.ok(
  reorderedGpx[0].indexOf('<name>Head</name>') < reorderedGpx[0].indexOf('<name>Tail</name>'),
  'the chosen leg is sent to WASM first',
);
assert.equal(loopUI.elements['stat-track-points'].textContent, '9');
assert.equal(loopUI.downloads().length, 1);

/* The Tahoe reference has 14 tracks and a "Start" waypoint: the checkbox
 * chooses one stitched course or fourteen individual downloads. */
const tahoeGpx = await readFile(
  new URL('../reference/Tahoe_200__Official_Race_Map_2022.gpx', import.meta.url), 'utf8');
assert.equal(gpxTrackBlocks(tahoeGpx).length, 14);
assert.equal(gpxHasStartWaypoint(tahoeGpx), false);
const tahoeUI = createHarness(tahoeGpx);
await tahoeUI.loadSample();
assert.equal(tahoeUI.elements.status.dataset.state, 'success');
assert.equal(tahoeUI.elements['stat-track-points'].textContent, '17602');
assert.equal(tahoeUI.downloads().length, 1);
tahoeUI.elements['stitch-tracks'].checked = false;
tahoeUI.elements['stitch-tracks'].dispatch('change');
await tahoeUI.converted();
assert.equal(tahoeUI.elements.status.dataset.state, 'success');
assert.equal(tahoeUI.downloads().length, 14);
assert.equal(tahoeUI.downloads()[0].download, 'Armstrong Pass Aid to Heavenly Aid.fit');

/* T2: the Status pill is an accessible toggle that forces snapping/skipping
 * in the map, the stats, the downloaded FIT, and later --snap changes. */
const overrideCalls = [];
const overrideRecorder = Object.create(wasm);
overrideRecorder.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert')
    overrideCalls.push(JSON.parse(wasm.UTF8ToString(args[3][13])));
  return wasm.ccall(...args);
};
const toggleUI = createHarness(sampleGpx, {
  moduleFactory: async () => overrideRecorder,
});
await toggleUI.loadSample();
const initialCount = toggleUI.elements['stat-course-points'].textContent;
const autoFit = await latestFitBytes(toggleUI);
let cautionBadge = statusButton(toggleUI.rowNamed('Caution'));
assert.equal(cautionBadge.tagName, 'button');
assert.equal(cautionBadge.textContent, 'Skipped');
assert.equal(cautionBadge.getAttribute('aria-pressed'), 'false');
assert.match(cautionBadge.getAttribute('aria-label'), /Caution: skipped/);
assert.doesNotMatch(cautionBadge.getAttribute('aria-label'), /manual override/);

cautionBadge.dispatch('click');
await toggleUI.converted();
assert.equal(toggleUI.rowNamed('Caution').dataset.status, 'snapped');
cautionBadge = statusButton(toggleUI.rowNamed('Caution'));
assert.equal(cautionBadge.textContent, 'Snapped');
assert.equal(cautionBadge.getAttribute('aria-pressed'), 'true');
assert.match(cautionBadge.getAttribute('aria-label'), /manual override/);
assert.match(cautionBadge.className, /is-overridden/);
assert.doesNotMatch(toggleUI.markerNamed('Caution').options.icon.html, /is-skipped/);
assert.equal(
  Number(toggleUI.elements['stat-course-points'].textContent),
  Number(initialCount) + 1,
);
assert.equal(toggleUI.elements['stat-waypoints'].textContent, '5/5 snapped');
assert.deepEqual(overrideCalls.at(-1), [0, 0, 0, 1]);
assert.notDeepEqual([...(await latestFitBytes(toggleUI))], [...autoFit]);

await toggleUI.setSnap(500);
assert.equal(toggleUI.rowNamed('Caution').dataset.status, 'snapped');
assert.match(statusButton(toggleUI.rowNamed('Caution')).getAttribute('aria-label'),
  /manual override/);

statusButton(toggleUI.rowNamed('Caution')).dispatch('click');
await toggleUI.converted();
assert.equal(toggleUI.rowNamed('Caution').dataset.status, 'skipped');
assert.match(toggleUI.markerNamed('Caution').options.icon.html, /is-skipped/);
assert.match(statusButton(toggleUI.rowNamed('Caution')).getAttribute('aria-label'),
  /manual override/);
assert.deepEqual(overrideCalls.at(-1), [0, 0, 0, 2]);

/* Loading another GPX clears the override map. */
await toggleUI.loadSample();
assert.equal(toggleUI.rowNamed('Caution').dataset.status, 'snapped');
assert.doesNotMatch(statusButton(toggleUI.rowNamed('Caution')).getAttribute('aria-label'),
  /manual override/);
assert.deepEqual(overrideCalls.at(-1), []);

/* T3: natural numerical track ordering and independent per-track controls. */
const dayGpx = `<?xml version="1.0"?><gpx xmlns="http://www.topografix.com/GPX/1/1" version="1.1">
  <wpt lat="0.0002" lon="0.0005"><name>Camp</name></wpt>
  <wpt lat="0.0002" lon="0.0105"><name>Water</name></wpt>
  <trk><name>Day 10</name><trkseg>
    <trkpt lat="0" lon="0.02"><ele>10</ele></trkpt>
    <trkpt lat="0" lon="0.021"><ele>10</ele></trkpt>
  </trkseg></trk>
  <trk><name>Day 2</name><trkseg>
    <trkpt lat="0" lon="0.01"><ele>2</ele></trkpt>
    <trkpt lat="0" lon="0.011"><ele>2</ele></trkpt>
  </trkseg></trk>
  <trk><name>Day 1</name><trkseg>
    <trkpt lat="0" lon="0"><ele>1</ele></trkpt>
    <trkpt lat="0" lon="0.001"><ele>1</ele></trkpt>
  </trkseg></trk>
</gpx>`;
assert.deepEqual(gpxTrackBlocks(dayGpx).map((t) => t.name), ['Day 1', 'Day 2', 'Day 10']);

const t3Calls = [];
const t3Recorder = Object.create(wasm);
t3Recorder.ccall = (...args) => {
  if (args[0] === 'gpx2fit_convert') {
    t3Calls.push({
      track: args[3][11],
      stitch: args[3][12],
      snap: args[3][5],
      overrides: JSON.parse(wasm.UTF8ToString(args[3][13])),
    });
  }
  return wasm.ccall(...args);
};
const t3UI = createHarness(dayGpx, { moduleFactory: async () => t3Recorder });
t3UI.elements['stitch-tracks'].checked = false;
await t3UI.loadSample();
assert.equal(t3UI.elements.status.dataset.state, 'success');
assert.deepEqual(t3Calls.map((c) => ({ track: c.track, stitch: c.stitch })), [
  { track: 3, stitch: 0 }, // Day 1 original index
  { track: 2, stitch: 0 }, // Day 2
  { track: 1, stitch: 0 }, // Day 10
]);
const cards = t3UI.trackCards();
assert.equal(cards.length, 3);
assert.deepEqual(cards.map((c) => c.children[0].children[0].textContent), ['Day 1', 'Day 2', 'Day 10']);
assert.equal(t3UI.elements['snap-field'].hidden, true, 'global snap hidden in unstitched multi-track');
assert.equal(t3UI.elements['waypoint-table-wrap'].hidden, true, 'single table hidden in unstitched multi-track');
assert.equal(t3UI.elements['route-map'].hidden, true, 'single map hidden in unstitched multi-track');

function cardRows(card) {
  const tbody = card.children.find((c) => c.className === 'waypoint-table-wrap')
    ?.children[0]?.children?.find((n) => n.tagName === 'tbody');
  return (tbody?.children || []).filter((r) => r.dataset.status);
}
function cardRowNamed(card, name) {
  return cardRows(card).find((r) => r.children[0].textContent === name);
}
function cardStatusButton(card, name) {
  return cardRowNamed(card, name).children[1].children[0];
}
function cardSnap(card) {
  return {
    slider: card.children.find((c) => c.className.includes('track-snap-field')).children[1].children[0],
    number: card.children.find((c) => c.className.includes('track-snap-field')).children[1].children[1].children[0],
  };
}

const day1Card = t3UI.trackCard(3);
const day2Card = t3UI.trackCard(2);
const day10Card = t3UI.trackCard(1);
assert.ok(day1Card && day2Card && day10Card);
assert.equal(cardRowNamed(day1Card, 'Camp').dataset.status, 'snapped');
assert.equal(cardRowNamed(day1Card, 'Water').dataset.status, 'skipped');
assert.equal(cardRowNamed(day2Card, 'Camp').dataset.status, 'skipped');
assert.equal(cardRowNamed(day2Card, 'Water').dataset.status, 'snapped');
// Day 10 is far from both waypoints; they appear as skipped in its own table.
assert.equal(cardRowNamed(day10Card, 'Camp').dataset.status, 'skipped');
assert.equal(cardRowNamed(day10Card, 'Water').dataset.status, 'skipped');

// Toggle waypoint on Day 1 only.
t3Calls.length = 0;
cardStatusButton(day1Card, 'Water').dispatch('click');
await t3UI.converted();
assert.equal(cardRowNamed(t3UI.trackCard(3), 'Water').dataset.status, 'snapped');
assert.equal(cardRowNamed(t3UI.trackCard(2), 'Water').dataset.status, 'snapped'); // Day 2 unchanged
assert.equal(cardRowNamed(t3UI.trackCard(2), 'Camp').dataset.status, 'skipped');
assert.equal(cardRowNamed(t3UI.trackCard(1), 'Water').dataset.status, 'skipped', 'Day 10 unaffected');
assert.ok(t3Calls.every((c) => c.track === 3), 'only Track 1 (original index 3) reconverted on toggle');
assert.deepEqual(t3Calls.at(-1).overrides[1], 1, 'Water override forced snap on Day 1');

// Adjust snap on Day 1 only.
t3Calls.length = 0;
const day1Snap = cardSnap(t3UI.trackCard(3));
day1Snap.number.value = '0';
day1Snap.number.dispatch('change');
await t3UI.converted();
assert.ok(t3Calls.every((c) => c.track === 3), 'only Day 1 reconverted on snap change');
assert.equal(t3Calls.at(-1).snap, 0);
assert.equal(cardRowNamed(t3UI.trackCard(3), 'Camp').dataset.status, 'skipped', 'Day 1 Camp now too far with snap 0');
assert.equal(cardRowNamed(t3UI.trackCard(2), 'Camp').dataset.status, 'skipped', 'Day 2 unaffected by Day 1 snap');
assert.equal(cardSnap(t3UI.trackCard(2)).number.value, '250', 'Day 2 snap unchanged');
assert.equal(cardRowNamed(t3UI.trackCard(1), 'Camp').dataset.status, 'skipped', 'Day 10 unaffected by Day 1 snap');

// AC3 regression: per-track update must refresh that track's downloadable FIT
// (card download) and must not leave stale global download-list entries.
const day1Dl = t3UI.trackCard(3).children.find((c) => c.tagName === 'a' && c.className.includes('download-button'));
const day2DlBefore = (await t3UI.blobs.at(t3UI.blobs.findIndex((b) => b) /* noop */), null); // placeholder, compare by URL below
const day1HrefBefore = day1Dl.href;
const day2HrefBefore = t3UI.trackCard(2).children.find((c) => c.tagName === 'a' && c.className.includes('download-button')).href;
t3Calls.length = 0;
cardStatusButton(t3UI.trackCard(3), 'Camp').dispatch('click');
await t3UI.converted();
const day1HrefAfter = t3UI.trackCard(3).children.find((c) => c.tagName === 'a' && c.className.includes('download-button')).href;
const day2HrefAfter = t3UI.trackCard(2).children.find((c) => c.tagName === 'a' && c.className.includes('download-button')).href;
assert.notEqual(day1HrefAfter, day1HrefBefore, 'Track 1 download refreshed after per-track update');
assert.equal(day2HrefAfter, day2HrefBefore, 'Track 2 download unchanged after Track 1 update');
assert.equal(t3UI.elements['download-list'].hidden, true, 'no stale global download-list in unstitched mode');

console.log('test_web.mjs: live map/table/warnings, status toggles, custom maps, multi-track stitch/split, Tahoe 14-track reference, antimeridian, stale conversion, and T3 per-track checks passed');
