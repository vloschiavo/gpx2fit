import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { initUpdateDetector, UPDATE_MESSAGE } from '../web/app.js';

const html = await readFile(new URL('../web/index.html', import.meta.url), 'utf8');
const swSource = await readFile(new URL('../web/sw.js', import.meta.url), 'utf8');
const versionInfo = JSON.parse(
  await readFile(new URL('../web/version.json', import.meta.url), 'utf8'),
);
const buildId = versionInfo.build_id;
assert.equal(typeof buildId, 'string');
assert.ok(buildId.length > 0);

/* Installation must not auto-activate a new worker, even though the precache
 * is populated there. Activation is gated on the Reload button's message. */
const installBlock = swSource.slice(
  swSource.indexOf("addEventListener('install'"),
  swSource.indexOf("addEventListener('activate'"),
).replace(/\/\/[^\n]*/g, '');
assert.ok(installBlock.length > 0);
assert.doesNotMatch(installBlock, /skipWaiting/);
assert.match(swSource, /SKIP_WAITING[\s\S]*self\.skipWaiting\(\)/);
assert.match(swSource, /self\.clients\.claim\(\)/);

/* The banner is present, accessible, and hidden until the detector shows it. */
assert.ok(
  html.includes(`<meta id="gpx2fit-build" name="gpx2fit-build" content="${buildId}">`),
  'the page build id must match web/version.json (before and after stamping)',
);
assert.ok(swSource.includes(`const BUILD_ID = '${buildId}';`));
assert.ok(swSource.includes(`const CACHE_NAME = 'gpx2fit-${buildId}';`));
assert.match(
  html,
  /id="update-banner"[^>]*role="status"[^>]*aria-live="polite"[^>]*aria-atomic="true"/,
);
assert.ok(html.includes(`id="update-message">${UPDATE_MESSAGE}</p>`));
assert.match(html, /<button[^>]*id="update-reload"[^>]*>Reload<\/button>/);

/* The footer carries a compact version label rendered from the checked-in
 * markup (stamped by `make web-version`), so it never appears after a fetch. */
const footerStart = html.indexOf('<footer>');
const footerEnd = html.indexOf('</footer>', footerStart);
const versionLabel = html.match(
  /<span id="gpx2fit-version" class="version-label">([^<]+)<\/span>/,
);
assert.ok(versionLabel, 'the footer has a version label element');
assert.ok(
  footerStart >= 0 && footerEnd > footerStart
    && versionLabel.index > footerStart && versionLabel.index < footerEnd,
  'the version label is rendered inside the page footer',
);
assert.match(versionLabel[1], /^v\S+$/, 'the version label shows a release version');

class FakeElement {
  constructor(id = '', tagName = 'div') {
    this.id = id;
    this.tagName = tagName;
    this.hidden = true;
    this.textContent = '';
    this.attributes = new Map();
    this.listeners = new Map();
  }

  getAttribute(name) { return this.attributes.get(name) ?? null; }
  setAttribute(name, value) { this.attributes.set(name, value); }
  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }
  removeEventListener(type, listener) {
    const listeners = this.listeners.get(type) || [];
    const index = listeners.indexOf(listener);
    if (index >= 0) listeners.splice(index, 1);
  }
  click() { this.dispatch('click', { preventDefault() {} }); }
  dispatch(type, event) {
    for (const listener of this.listeners.get(type) || []) listener(event);
  }
}

function fire(map, type, event = {}) {
  for (const listener of map.get(type) || []) listener(event);
}

function harness({ build = 'dev-local', remote = build, waiting = false, serviceWorker = true } = {}) {
  const ids = ['update-banner', 'update-message', 'update-reload', 'update-dismiss', 'gpx2fit-build'];
  const elements = Object.fromEntries(ids.map((id) => [id, new FakeElement(id)]));
  elements['gpx2fit-build'].setAttribute('content', build);
  elements['update-message'].textContent = UPDATE_MESSAGE;

  const docListeners = new Map();
  const doc = {
    hidden: false,
    getElementById(id) { return elements[id] || null; },
    addEventListener(type, listener) {
      if (!docListeners.has(type)) docListeners.set(type, []);
      docListeners.get(type).push(listener);
    },
    removeEventListener(type, listener) {
      const listeners = docListeners.get(type) || [];
      const index = listeners.indexOf(listener);
      if (index >= 0) listeners.splice(index, 1);
    },
  };

  const winListeners = new Map();
  const swListeners = new Map();
  const regListeners = new Map();
  const storage = new Map();
  const posted = [];
  const makeWorker = (scriptURL) => {
    const listeners = new Map();
    return {
      scriptURL,
      state: 'installed',
      postMessage(data) { posted.push(data); },
      addEventListener(type, listener) {
        if (!listeners.has(type)) listeners.set(type, []);
        listeners.get(type).push(listener);
      },
      fire(type) { fire(listeners, type); },
    };
  };
  const waitingWorker = waiting ? makeWorker('sw.js?v=2') : null;
  const regState = { updates: 0 };
  const registration = {
    waiting: waitingWorker,
    installing: null,
    async update() { regState.updates += 1; },
    addEventListener(type, listener) {
      if (!regListeners.has(type)) regListeners.set(type, []);
      regListeners.get(type).push(listener);
    },
    removeEventListener(type, listener) {
      const listeners = regListeners.get(type) || [];
      const index = listeners.indexOf(listener);
      if (index >= 0) listeners.splice(index, 1);
    },
  };
  const sw = serviceWorker ? {
    controller: { postMessage(data) { posted.push(data); } },
    ready: Promise.resolve(registration),
    register() { return Promise.resolve(registration); },
    addEventListener(type, listener) {
      if (!swListeners.has(type)) swListeners.set(type, []);
      swListeners.get(type).push(listener);
    },
    removeEventListener(type, listener) {
      const listeners = swListeners.get(type) || [];
      const index = listeners.indexOf(listener);
      if (index >= 0) listeners.splice(index, 1);
    },
  } : undefined;

  const state = { remote };
  const fetchCalls = [];
  const win = {
    navigator: serviceWorker ? { serviceWorker: sw } : {},
    location: { reloadCount: 0, reload() { this.reloadCount += 1; } },
    sessionStorage: {
      getItem(key) { return storage.get(key) ?? null; },
      setItem(key, value) { storage.set(key, value); },
    },
    setTimeout(fn) { fn(); return 1; },
    setInterval(fn) { state.interval = fn; return 1; },
    clearInterval() {},
    addEventListener(type, listener) {
      if (!winListeners.has(type)) winListeners.set(type, []);
      winListeners.get(type).push(listener);
    },
    removeEventListener(type, listener) {
      const listeners = winListeners.get(type) || [];
      const index = listeners.indexOf(listener);
      if (index >= 0) listeners.splice(index, 1);
    },
  };

  const stop = initUpdateDetector({
    doc,
    win,
    fetchImpl: async (url, options) => {
      fetchCalls.push([url, options]);
      return { ok: true, json: async () => ({ build_id: state.remote }) };
    },
    intervalMs: 1000,
  });

  const flush = async () => {
    for (let i = 0; i < 6; i++) await new Promise((resolve) => setImmediate(resolve));
  };

  return {
    elements,
    doc,
    posted,
    fetchCalls,
    get updates() { return regState.updates; },
    stop,
    flush,
    set remote(value) { state.remote = value; },
    get reloadCount() { return win.location.reloadCount; },
    fireDoc: (type) => fire(docListeners, type),
    fireWin: (type) => fire(winListeners, type),
    fireSW: (type, event) => fire(swListeners, type, event),
    interval: () => state.interval(),
    addWaitingWorker(scriptURL = 'sw.js?next') {
      const worker = makeWorker(scriptURL);
      registration.waiting = worker;
      registration.installing = worker;
      fire(regListeners, 'updatefound');
      worker.fire('statechange');
      return worker;
    },
  };
}

/* A matching build stays quiet; the version probe never uses the HTTP cache. */
{
  const h = harness();
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, true);
  assert.deepEqual(h.fetchCalls[0], ['./version.json', { cache: 'no-store' }]);
  assert.ok(h.updates > 0, 'the worker should also check for a new sw.js');
  h.stop();
}

/* A new build is detected on load, visibility focus, focus, and the interval. */
{
  const h = harness({ remote: 'old-build' });
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  assert.equal(h.elements['update-message'].textContent, UPDATE_MESSAGE);

  h.remote = 'new-build';
  h.elements['update-banner'].hidden = true;
  h.doc.hidden = true;
  h.fireDoc('visibilitychange');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, true, 'hidden tab must not check');

  h.elements['update-banner'].hidden = true;
  h.doc.hidden = false;
  h.fireWin('focus');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);

  h.remote = 'newer-build';
  h.elements['update-banner'].hidden = true;
  h.interval();
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.stop();
}

/* Explicit visibilitychange with a visible document checks immediately. */
{
  const h = harness({ remote: 'old-build' });
  await h.flush();
  h.remote = 'visible-build';
  h.elements['update-banner'].hidden = true;
  h.fireDoc('visibilitychange');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.stop();
}

/* A waiting worker prompts even when version.json still matches. Reload asks
 * it to skip waiting exactly once and then reloads the page. */
{
  const h = harness({ waiting: true });
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.elements['update-reload'].click();
  await h.flush();
  assert.ok(h.posted.some((message) => message.type === 'SKIP_WAITING'));
  assert.equal(h.reloadCount, 1);
  h.fireSW('controllerchange');
  assert.equal(h.reloadCount, 1, 'a late controllerchange must not reload again');
  h.stop();
}

/* Dismissing suppresses further prompts for that build id, but not later ones. */
{
  const h = harness({ remote: 'new-build' });
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.elements['update-dismiss'].click();
  assert.equal(h.elements['update-banner'].hidden, true);
  h.fireWin('focus');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, true);
  h.remote = 'another-build';
  h.fireWin('focus');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.stop();
}

/* Without a Service Worker the banner and a plain reload still work. */
{
  const h = harness({ remote: 'new-build', serviceWorker: false });
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.elements['update-reload'].click();
  await h.flush();
  assert.equal(h.reloadCount, 1);
  assert.deepEqual(h.posted, []);
  h.stop();
}

/* A dismissed prompt must not suppress a later waiting worker / controller
 * change for a different deployed build id. */
{
  const h = harness({ remote: 'build-a' });
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false);
  h.elements['update-dismiss'].click();
  assert.equal(h.elements['update-banner'].hidden, true);

  h.remote = 'build-b';
  h.addWaitingWorker('sw.js?b');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false, 'second build must prompt');
  assert.equal(h.elements['update-message'].textContent, UPDATE_MESSAGE);

  h.elements['update-dismiss'].click();
  h.remote = 'build-c';
  h.fireSW('controllerchange');
  await h.flush();
  assert.equal(h.elements['update-banner'].hidden, false, 'third build must prompt');
  h.stop();
}

/* Cleanup stops checks and unregisters listeners. */
{
  const h = harness({ remote: 'build-a' });
  await h.flush();
  const calls = h.fetchCalls.length;
  h.stop();
  h.remote = 'after-stop';
  h.fireWin('focus');
  h.interval();
  await h.flush();
  assert.equal(h.fetchCalls.length, calls, 'cleanup must stop checks');
}

console.log('test_web_update.mjs: update banner, version checks, and reload activation passed');
