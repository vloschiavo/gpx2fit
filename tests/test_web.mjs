import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import createModule from '../web/dist/gpx2fit.js';
import { initApp } from '../web/app.js';

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
    this.classList = { add() {}, remove() {} };
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

function createHarness(gpx, { moduleFactory = async () => wasm } = {}) {
  const ids = [
    'app', 'conversion-controls', 'dropzone', 'gpx-file', 'load-sample',
    'source-name', 'course-name', 'activity-type', 'pace', 'pace-unit',
    'pass-mode', 'snap-distance', 'snap-slider', 'map-rules', 'status', 'error-message',
    'results', 'warnings', 'warning-status', 'download-fit', 'stat-distance',
    'stat-track-points', 'stat-course-points', 'stat-waypoints', 'route-map',
    'map-fallback', 'waypoint-table-body',
  ];
  const elements = Object.fromEntries(ids.map((id) => [id, new FakeElement('div', id)]));
  elements['activity-type'].value = 'trail-running';
  elements.pace.value = '12:00';
  elements['pace-unit'].value = 'min/km';
  elements['pass-mode'].value = 'nearest';
  elements['snap-distance'].value = '250';
  elements['snap-slider'].value = '250';

  const doc = {
    getElementById(id) { return elements[id] || null; },
    createElement(tagName) { return new FakeElement(tagName); },
    createTextNode(text) { return { textContent: text }; },
  };
  const win = {
    setTimeout,
    clearTimeout,
    URL: { createObjectURL: () => 'blob:test', revokeObjectURL() {} },
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
  initApp({
    doc,
    win,
    leaflet,
    moduleFactory,
    fetchImpl: async () => ({ ok: true, status: 200, text: async () => currentGpx }),
  });

  async function waitFor(predicate, description) {
    for (let i = 0; i < 300; i++) {
      if (predicate()) return;
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
    throw new Error(`Timed out waiting for ${description}`);
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
    markers() {
      return mapState.groups[0].layers.filter((layer) => layer.kind === 'marker');
    },
    markerNamed(name) {
      return this.markers().find((marker) => marker.options.title.startsWith(`${name} —`));
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

assert.match(
  indexHtml,
  /<p class="sr-only" id="warning-status" role="status" aria-live="polite" aria-atomic="true"><\/p>/,
  'persistent atomic warning live region',
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

console.log('test_web.mjs: live map/table/warnings, custom maps, antimeridian, and stale conversion checks passed');
