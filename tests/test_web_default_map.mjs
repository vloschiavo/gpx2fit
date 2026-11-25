/* T4: the Advanced Options info popover must show the embedded default.map
 * rules and behave for hover, click, keyboard focus, Escape, and outside taps.
 * Run via tests/test_web_default_map.sh (or `node tests/test_web_default_map.mjs`). */
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import createModule from '../web/dist/gpx2fit.js';
import { initApp } from '../web/app.js';

const mapText = await readFile(new URL('../data/default.map', import.meta.url), 'utf8');
const indexHtml = await readFile(new URL('../web/index.html', import.meta.url), 'utf8');
/* Parse independently of the app's helper so a helper that drops or mangles
 * rules cannot make this test pass. */
const expectedRules = mapText
  .replace(/\r\n/g, '\n')
  .split('\n')
  .filter((line) => line !== '' && !line.startsWith('#'))
  .join('\n');

/* The rebuilt bundle must expose the embedded map verbatim. */
const realWasm = await createModule();
const mapPointer = realWasm._gpx2fit_get_default_map();
assert.notEqual(mapPointer, 0, 'WASM getter returned a null pointer');
assert.equal(realWasm.UTF8ToString(mapPointer), mapText);

let doc;

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
    this.style = {};
    this.classList = { add() {}, remove() {} };
    this.textContent = '';
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  dispatch(type, overrides = {}) {
    const event = {
      preventDefault() {},
      dataTransfer: null,
      persisted: false,
      target: this,
      relatedTarget: null,
      ...overrides,
    };
    for (const listener of this.listeners.get(type) || []) listener(event);
  }

  setAttribute(name, value) { this.attributes.set(name, value); }
  getAttribute(name) { return this.attributes.get(name) ?? null; }
  removeAttribute(name) { this.attributes.delete(name); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; }

  contains(node) {
    return node === this || this.children.some((child) => child.contains?.(node));
  }

  focus() {
    doc.activeElement = this;
    this.dispatch('focus');
  }
}

function fakeWasm() {
  const bytes = new TextEncoder().encode(mapText);
  const heap = new Uint8Array(bytes.length + 2);
  heap.set(bytes, 1); // wasm pointers are nonzero
  return {
    _malloc: () => 1,
    _free() {},
    HEAPU8: heap,
    UTF8ToString(pointer) {
      const view = heap.subarray(pointer);
      const end = view.indexOf(0);
      return new TextDecoder().decode(end < 0 ? view : view.subarray(0, end));
    },
    _gpx2fit_get_default_map: () => 1,
    ccall() { throw new Error('unexpected conversion'); },
  };
}

function createHarness({ moduleFactory = async () => fakeWasm() } = {}) {
  const ids = [
    'app', 'conversion-controls', 'dropzone', 'gpx-file', 'load-sample',
    'source-name', 'course-name', 'activity-type', 'pace', 'pace-unit',
    'pass-mode', 'snap-distance', 'snap-slider', 'map-rules', 'status', 'error-message',
    'results', 'warnings', 'warning-status', 'download-fit', 'stat-distance',
    'stat-track-points', 'stat-course-points', 'stat-waypoints', 'route-map',
    'map-fallback', 'waypoint-table-body', 'default-map-info',
    'default-map-info-button', 'default-map-popover', 'default-map-content',
    'default-map-close', 'default-map-note',
  ];
  const elements = Object.fromEntries(ids.map((id) => [id, new FakeElement('div', id)]));
  elements['default-map-note'].textContent = 'Custom rules take precedence over these defaults.';
  elements['default-map-info'].append(elements['default-map-info-button'], elements['default-map-popover']);
  elements['default-map-popover'].append(
    elements['default-map-note'], elements['default-map-content'], elements['default-map-close'],
  );
  elements['activity-type'].value = 'trail-running';
  elements.pace.value = '12:00';
  elements['pace-unit'].value = 'min/km';
  elements['pass-mode'].value = 'nearest';
  elements['snap-distance'].value = '250';
  elements['snap-slider'].value = '250';

  doc = {
    activeElement: null,
    getElementById(id) { return elements[id] || null; },
    createElement(tagName) { return new FakeElement(tagName); },
    createTextNode(text) { return { textContent: text }; },
  };
  const listeners = new Map();
  const win = {
    setTimeout,
    clearTimeout,
    URL: { createObjectURL: () => 'blob:test', revokeObjectURL() {} },
    addEventListener(type, listener) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(listener);
    },
    dispatch(type, event) {
      for (const listener of listeners.get(type) || []) listener(event);
    },
  };

  initApp({
    doc,
    win,
    moduleFactory,
    fetchImpl: async () => ({ ok: true, status: 200, text: async () => '' }),
    leaflet: {},
  });

  return { elements, doc, win };
}

async function waitFor(predicate, description) {
  for (let i = 0; i < 300; i++) {
    if (predicate()) return;
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  throw new Error(`Timed out waiting for ${description}`);
}

/* Markup: an accessible trigger lives beside the custom-rules input. */
assert.match(indexHtml, /aria-label="View built-in waypoint mapping rules"/);
assert.match(indexHtml, /aria-controls="default-map-popover"/);
assert.match(indexHtml, /Custom rules take precedence over these defaults\./);
assert.ok(expectedRules.includes('water   = water'));
assert.ok(expectedRules.includes('first aid = first_aid'));
assert.doesNotMatch(expectedRules, /^#/m);
const ruleLines = expectedRules.split('\n');
assert.equal(ruleLines.length, 62, 'every built-in rule is included');
assert.deepEqual(ruleLines.map((line) => {
  const match = /^([^=]+?)\s*=\s*(\S+)\s*$/.exec(line);
  assert.ok(match, `expected rule line is malformed: ${line}`);
  return [match[1].trimEnd(), match[2]];
}), [
  ['water', 'water'],
  ['h2o', 'water'],
  ['camp', 'campsite'],
  ['caution', 'danger'],
  ['hazard', 'danger'],
  ['danger', 'danger'],
  ['first aid', 'first_aid'],
  ['summit', 'summit'],
  ['overlook', 'overlook'],
  ['shelter', 'shelter'],
  ['toilet', 'toilet'],
  ['food', 'food'],
  ['info', 'info'],
  ['service', 'service'],
  ['aid station', 'aid_station'],
  ['energy gel', 'energy_gel'],
  ['sports drink', 'sports_drink'],
  ['sprint', 'sprint'],
  ['hc climb', 'hors_category'],
  ['category 1 climb', 'first_category'],
  ['category 2 climb', 'second_category'],
  ['category 3 climb', 'third_category'],
  ['category 4 climb', 'fourth_category'],
  ['shower', 'shower'],
  ['gear', 'gear'],
  ['navaid', 'navaid'],
  ['transport', 'transport'],
  ['transition', 'transition'],
  ['checkpoint', 'checkpoint'],
  ['meeting spot', 'meeting_spot'],
  ['campsite', 'campsite'],
  ['rest area', 'rest_area'],
  ['obstacle start', 'obstacle'],
  ['obstacle end', 'obstacle'],
  ['tunnel', 'tunnel'],
  ['bridge', 'bridge'],
  ['valley', 'valley'],
  ['store', 'store'],
  ['alert', 'alert'],
  ['obstacle', 'obstacle'],
  ['crossing', 'crossing'],
  ['steep incline', 'steep_incline'],
  ['sharp curve', 'sharp_curve'],
  ['convenience', 'generic'],
  ['coffee shops', 'generic'],
  ['bike shop and repair', 'generic'],
  ['park', 'generic'],
  ['winery or brewery', 'generic'],
  ['bank or atm', 'generic'],
  ['restaurant', 'generic'],
  ['emergency', 'generic'],
  ['recreation', 'generic'],
  ['attraction', 'generic'],
  ['lodging', 'generic'],
  ['lodgement', 'generic'],
  ['shopping', 'store'],
  ['community', 'generic'],
  ['creek', 'water'],
  ['lake', 'water'],
  ['river', 'water'],
  ['spring', 'water'],
  ['market', 'shopping'],
]);

const harness = createHarness();
const { elements, win } = harness;
const info = elements['default-map-info'];
const button = elements['default-map-info-button'];
const popover = elements['default-map-popover'];
const content = elements['default-map-content'];

/* Hover opens the popover and fills it from the WASM getter. */
button.dispatch('mouseenter');
assert.equal(popover.hidden, false, 'hover opens the popover');
assert.equal(button.getAttribute('aria-expanded'), 'true');
await waitFor(() => content.textContent === expectedRules, 'rules from WASM');
assert.equal(content.textContent, expectedRules);
assert.match(elements['default-map-note'].textContent, /custom rules take precedence/i);

/* Leaving with focus elsewhere closes an unpinned popover. */
harness.doc.activeElement = elements['map-rules'];
info.dispatch('mouseleave');
assert.equal(popover.hidden, true, 'mouseleave closes an unpinned popover');

/* Click pins it; a tap outside closes it even on touch devices. */
button.dispatch('click');
assert.equal(popover.hidden, false, 'click opens the popover');
info.dispatch('mouseleave');
assert.equal(popover.hidden, false, 'click-pinned popover survives mouseleave');
win.dispatch('pointerdown', { target: elements['map-rules'] });
assert.equal(popover.hidden, true, 'pointerdown outside closes the popover');

/* Keyboard focus opens it, focus may move inside, and Escape restores focus. */
button.dispatch('focus');
assert.equal(popover.hidden, false, 'keyboard focus opens the popover');
button.dispatch('blur', { relatedTarget: content });
assert.equal(popover.hidden, false, 'focus inside the popover keeps it open');
win.dispatch('keydown', { key: 'Escape' });
assert.equal(popover.hidden, true, 'Escape closes the popover');
assert.equal(harness.doc.activeElement, button, 'Escape restores focus to the trigger');
assert.equal(button.getAttribute('aria-expanded'), 'false');
await waitFor(() => true, 'one task');
assert.equal(popover.hidden, true, 'restored focus must not reopen the popover');
button.dispatch('focus');
assert.equal(popover.hidden, false, 'focus works again after Escape');
win.dispatch('keydown', { key: 'Enter' });
assert.equal(popover.hidden, false, 'unrelated keys do not close the popover');

/* A build without the export fails gracefully instead of showing stale rules. */
const missingExport = createHarness({
  moduleFactory: async () => ({ UTF8ToString: () => '', _gpx2fit_get_default_map: undefined }),
});
missingExport.elements['default-map-info-button'].dispatch('click');
await waitFor(
  () => missingExport.elements['default-map-content'].textContent.startsWith('Built-in rules unavailable:'),
  'missing-export message',
);

console.log('test_web_default_map.mjs: built-in rule popover, hover/click/focus, Escape, and outside-close checks passed');
