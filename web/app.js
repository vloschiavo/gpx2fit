import createModule from './dist/gpx2fit.js';

const ACTIVITIES = Object.freeze({
  hiking: Object.freeze({ sport: 17, subSport: 0 }),
  running: Object.freeze({ sport: 1, subSport: 0 }),
  'trail-running': Object.freeze({ sport: 1, subSport: 3 }),
  cycling: Object.freeze({ sport: 2, subSport: 0 }),
  'gravel-cycling': Object.freeze({ sport: 2, subSport: 46 }),
  'mountain-biking': Object.freeze({ sport: 2, subSport: 8 }),
  other: Object.freeze({ sport: 0, subSport: 0 }),
});

const ARGUMENT_TYPES = Object.freeze([
  'string', 'string', 'string',
  'number', 'number', 'number', 'string',
  'number', 'number', 'number', 'number',
]);
const DEFAULT_START_FIT = 1104624000; // 2025-01-01T00:00:00Z, as in the CLI.
const FIT_EPOCH_UNIX = 631065600; // 1989-12-31T00:00:00Z.
const FIT_DATE_MIN = 0x10000000;
const FIT_TIME_MAX = 0xfffffffe;
const MAX_WASM_LONG = 0x7fffffff;
const CONVERT_DELAY_MS = 180;
const PASS_MODES = new Set(['nearest', 'ordered', 'first', 'last']);
export const UPDATE_MESSAGE = 'A new version of gpx2fit is available.';
const VERSION_CHECK_INTERVAL_MS = 15 * 1000;
const UPDATE_STORAGE_KEY = 'gpx2fit-acknowledged-build';
const FIT_POINT_TYPES = Object.freeze({
  0: Object.freeze({ label: 'Generic', icon: '•', color: '#52645a' }),
  1: Object.freeze({ label: 'Summit', icon: '▲', color: '#74508c' }),
  2: Object.freeze({ label: 'Valley', icon: '▼', color: '#3867a8' }),
  3: Object.freeze({ label: 'Water', icon: '≈', color: '#1677a6' }),
  4: Object.freeze({ label: 'Food', icon: 'F', color: '#a85b18' }),
  5: Object.freeze({ label: 'Danger', icon: '!', color: '#b02a37' }),
  9: Object.freeze({ label: 'First aid', icon: '+', color: '#b83b63' }),
  27: Object.freeze({ label: 'Campsite', icon: 'C', color: '#765333' }),
  28: Object.freeze({ label: 'Aid station', icon: 'A', color: '#0b7c72' }),
  29: Object.freeze({ label: 'Rest area', icon: 'R', color: '#657321' }),
  35: Object.freeze({ label: 'Checkpoint', icon: '✓', color: '#70469c' }),
  36: Object.freeze({ label: 'Shelter', icon: 'S', color: '#795b42' }),
  37: Object.freeze({ label: 'Meeting spot', icon: 'M', color: '#395ca8' }),
  38: Object.freeze({ label: 'Overlook', icon: 'O', color: '#805b92' }),
  39: Object.freeze({ label: 'Toilet', icon: 'T', color: '#546e7a' }),
  40: Object.freeze({ label: 'Shower', icon: 'H', color: '#00838f' }),
});

function fitPointType(value) {
  const type = Number(value);
  return FIT_POINT_TYPES[type] || {
    label: Number.isInteger(type) ? `FIT type ${type}` : 'Unknown FIT type',
    icon: '•',
    color: '#52645a',
  };
}

function pointCoordinates(point) {
  const lat = Number(point?.lat);
  const lon = Number(point?.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return null;
  return [lat, lon];
}

function longitudeDelta(lon, referenceLon) {
  return ((lon - referenceLon + 540) % 360) - 180;
}

function unwrapTrackCoordinates(rawTrack) {
  const track = [];
  for (let i = 0; i < rawTrack.length; i++) {
    const [lat, lon] = rawTrack[i];
    const unwrappedLon = i === 0
      ? lon
      : track[i - 1][1] + longitudeDelta(lon, rawTrack[i - 1][1]);
    track.push([lat, unwrappedLon]);
  }
  return track;
}

function coordinatesInTrackFrame(point, rawTrack, track) {
  const coordinates = pointCoordinates(point);
  if (!coordinates || !track.length) return coordinates;

  let nearest = 0;
  let nearestDistance = Infinity;
  for (let i = 0; i < rawTrack.length; i++) {
    const latDelta = rawTrack[i][0] - coordinates[0];
    const lonDelta = longitudeDelta(rawTrack[i][1], coordinates[1]);
    const distance = latDelta * latDelta + lonDelta * lonDelta;
    if (distance < nearestDistance) {
      nearest = i;
      nearestDistance = distance;
    }
  }
  coordinates[1] = track[nearest][1]
    + longitudeDelta(coordinates[1], rawTrack[nearest][1]);
  return coordinates;
}

function formatKilometers(value) {
  const distance = Number(value);
  return Number.isFinite(distance) ? distance.toFixed(3) : '—';
}

function formatOffset(value) {
  const offset = Number(value);
  return Number.isFinite(offset) ? String(Math.round(offset)) : '—';
}

/** Convert a user-entered pace to the meters/second expected by the C API. */
export function paceToMetersPerSecond(value, unit) {
  const text = String(value).trim();
  if (!text) throw new Error('Enter a pace.');

  if (unit === 'km/h') {
    const speed = Number(text);
    if (!Number.isFinite(speed) || speed <= 0)
      throw new Error('Speed must be a number greater than 0 km/h.');
    return speed / 3.6;
  }

  if (unit !== 'min/km') throw new Error('Choose a valid pace unit.');

  let minutes;
  if (text.includes(':')) {
    const parts = text.split(':');
    if (parts.length !== 2 || !/^\d+$/.test(parts[0]) || !/^\d+$/.test(parts[1]))
      throw new Error('Pace must be minutes, or minutes and seconds such as 12:00.');
    const seconds = Number(parts[1]);
    if (seconds >= 60)
      throw new Error('Seconds in a min/km pace must be between 0 and 59.');
    minutes = Number(parts[0]) + seconds / 60;
  } else {
    minutes = Number(text);
  }

  if (!Number.isFinite(minutes) || minutes <= 0)
    throw new Error('Pace must be greater than 0 min/km.');
  const metersPerSecond = 1000 / (minutes * 60);
  if (!Number.isFinite(metersPerSecond) || metersPerSecond <= 0)
    throw new Error('Enter a finite pace greater than zero.');
  return metersPerSecond;
}

/** Convert a UTC start date/time to the seconds-since-FIT-epoch value used by C. */
export function startToFitTime(value) {
  const text = String(value ?? '').trim();
  if (!text) return DEFAULT_START_FIT;

  if (/[+-]\d{2}:\d{2}$/.test(text)) {
    throw new Error('Start time must be UTC; time zone offsets are not supported.');
  }

  const match = /^(\d{4})-(\d{2})-(\d{2})(?:T(\d{2}):(\d{2})(?::(\d{2}))?Z?)?$/.exec(text);
  if (!match) {
    throw new Error('Start time must use YYYY-MM-DD or YYYY-MM-DDTHH:MM[:SS][Z] (UTC).');
  }

  const year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  const hour = Number(match[4] || 0);
  const minute = Number(match[5] || 0);
  const second = Number(match[6] || 0);
  const leapYear = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const daysInMonth = [31, leapYear ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];

  if (month < 1 || month > 12 || day < 1 || day > daysInMonth[month - 1])
    throw new Error('Start time is not a valid calendar date.');
  if (hour > 23 || minute > 59 || second > 59)
    throw new Error('Start time is not a valid UTC time of day.');

  // Date.UTC treats years 0 through 99 specially. They are outside FIT's date
  // range anyway, so reject them before doing the calendar conversion.
  if (year < 100) {
    throw new Error('Start time is too early; FIT dates begin at 1998-07-03T21:24:16Z.');
  }

  const unixSeconds = Date.UTC(year, month - 1, day, hour, minute, second) / 1000;
  const fitTime = unixSeconds - FIT_EPOCH_UNIX;
  if (!Number.isSafeInteger(fitTime) || fitTime < FIT_DATE_MIN) {
    throw new Error('Start time is too early; FIT dates begin at 1998-07-03T21:24:16Z.');
  }
  if (fitTime > FIT_TIME_MAX) {
    throw new Error('Start time is too late; FIT dates end at 2126-02-06T06:28:14Z.');
  }
  return fitTime;
}

/** Enforce fatal custom-map constraints; individual rule lines are parsed by C. */
export function validateMapRules(value) {
  const text = String(value ?? '');
  if (text.includes('\0')) throw new Error('Custom map rules cannot contain a NUL character.');
  const byteLength = typeof TextEncoder === 'function'
    ? new TextEncoder().encode(text).length
    : text.length;
  if (byteLength > 1024 * 1024) throw new Error('Custom map rules cannot exceed 1 MiB.');
  return text;
}

/** Make a safe, readable download name without inserting text into the DOM. */
export function fitFileName(courseName, sourceName) {
  const sourceBase = String(sourceName || 'course').replace(/\.gpx$/i, '');
  const rawName = String(courseName || sourceBase || 'course').trim();
  const safeName = rawName
    .replace(/[\\/:*?"<>|\u0000-\u001f]/g, '_')
    .replace(/[. ]+$/g, '')
    .trim();
  return `${safeName || 'course'}.fit`;
}

function errorText(error) {
  if (error instanceof Error && error.message) return error.message;
  return String(error || 'Unknown conversion error.');
}

function requiredElement(doc, id) {
  const element = doc.getElementById(id);
  if (!element) throw new Error(`Missing required interface element: ${id}`);
  return element;
}

function optionalElement(doc, id) {
  return doc.getElementById(id);
}

function acknowledgedBuild(win) {
  try {
    return win.sessionStorage?.getItem(UPDATE_STORAGE_KEY) || '';
  } catch {
    return '';
  }
}

function acknowledgeBuild(win, buildId) {
  try {
    win.sessionStorage?.setItem(UPDATE_STORAGE_KEY, buildId);
  } catch {
    // Session storage can be disabled; the banner simply reappears on reload.
  }
}

/**
 * Detect a newly deployed build and offer a clean reload. The page-side check
 * works even without a Service Worker; when one controls the page it also asks
 * the worker to check version.json and watches for a waiting worker.
 * Returns a cleanup function (useful for tests).
 */
export function initUpdateDetector({
  doc = document,
  win = window,
  fetchImpl = win.fetch ? win.fetch.bind(win) : fetch,
  intervalMs = VERSION_CHECK_INTERVAL_MS,
} = {}) {
  const banner = optionalElement(doc, 'update-banner');
  const reloadButton = optionalElement(doc, 'update-reload');
  const dismissButton = optionalElement(doc, 'update-dismiss');
  const buildMeta = optionalElement(doc, 'gpx2fit-build');
  if (!banner || !reloadButton) return () => {};

  const runningBuild = String(
    buildMeta?.getAttribute?.('content') ?? buildMeta?.content ?? '',
  ).trim();
  const serviceWorker = win.navigator?.serviceWorker;
  const hadController = Boolean(serviceWorker?.controller);
  const workerIdentities = new WeakMap();
  let nextWorkerIdentity = 0;
  let registration = null;
  let knownRemoteBuild = '';
  let detectedBuild = '';
  let reloadStarted = false;
  let reloaded = false;
  let disposed = false;
  let updateFoundHandler = null;

  /**
   * Prefer the deployed build id from version.json (or a worker's VERSION_INFO
   * message). Worker objects get distinct fallback ids so a dismissed prompt
   * cannot suppress a later, different waiting worker when version.json is
   * unavailable.
   */
  function workerIdentity(worker) {
    if (knownRemoteBuild) return knownRemoteBuild;
    if (!worker || typeof worker !== 'object') return 'service worker';
    if (!workerIdentities.has(worker))
      workerIdentities.set(worker, `service worker ${++nextWorkerIdentity}`);
    return workerIdentities.get(worker);
  }

  function showUpdate(buildId) {
    if (disposed || reloadStarted) return;
    if (buildId) {
      if (acknowledgedBuild(win) === buildId) return;
      detectedBuild = buildId;
    }
    const message = optionalElement(doc, 'update-message');
    if (message) message.textContent = UPDATE_MESSAGE;
    banner.hidden = false;
  }

  async function checkVersion() {
    if (disposed || reloadStarted) return '';
    if (serviceWorker) {
      serviceWorker.controller?.postMessage?.({ type: 'CHECK_VERSION' });
      // Ask the worker to compare its build id too; a new sw.js becomes a
      // waiting worker and triggers the updatefound signal below.
      serviceWorker.ready
        .then((reg) => reg.update?.()?.catch?.(() => {}))
        .catch?.(() => {});
    }
    try {
      const response = await fetchImpl('./version.json', { cache: 'no-store' });
      if (!response || !response.ok) return '';
      const info = await response.json();
      const remoteBuild = info && (info.build_id || info.build);
      if (typeof remoteBuild !== 'string' || !remoteBuild) return '';
      knownRemoteBuild = remoteBuild;
      if (runningBuild && remoteBuild === runningBuild) {
        if (detectedBuild === remoteBuild) {
          detectedBuild = '';
          banner.hidden = true;
        }
        return remoteBuild;
      }
      showUpdate(remoteBuild);
      return remoteBuild;
    } catch {
      // Offline or a host without version.json: Service Worker signals still work.
      return '';
    }
  }

  async function showWorkerUpdate(worker) {
    if (disposed || reloadStarted) return;
    // Refresh first: a waiting worker's scriptURL is not a build id.
    const remoteBuild = await checkVersion();
    showUpdate(remoteBuild || workerIdentity(worker));
  }

  async function activateAndReload() {
    if (reloadStarted) return;
    reloadStarted = true;
    if (detectedBuild) acknowledgeBuild(win, detectedBuild);

    if (serviceWorker) {
      if (registration?.waiting) {
        const controllerChanged = new Promise((resolve) => {
          serviceWorker.addEventListener?.('controllerchange', resolve, { once: true });
        });
        const timeout = new Promise((resolve) => {
          const schedule = typeof win.setTimeout === 'function'
            ? win.setTimeout.bind(win)
            : setTimeout;
          schedule(resolve, 2000);
        });
        registration.waiting.postMessage?.({ type: 'SKIP_WAITING' });
        await Promise.race([controllerChanged, timeout]);
      } else {
        try {
          registration = registration || await serviceWorker.ready;
          await registration?.update?.();
        } catch {
          // Activation without a worker is still a normal page reload.
        }
      }
    }

    if (!reloaded) {
      reloaded = true;
      win.location.reload();
    }
  }

  function onReloadClick() {
    void activateAndReload();
  }

  function onDismissClick() {
    if (detectedBuild) acknowledgeBuild(win, detectedBuild);
    banner.hidden = true;
  }

  reloadButton.addEventListener('click', onReloadClick);
  dismissButton?.addEventListener('click', onDismissClick);

  function onControllerChange() {
    if (reloadStarted) {
      if (!reloaded) {
        reloaded = true;
        win.location.reload();
      }
    } else if (hadController || detectedBuild || registration?.waiting) {
      void showWorkerUpdate(registration?.active || serviceWorker.controller);
    }
  }

  function onVersionMessage(event) {
    const buildId = event?.data?.build_id;
    if (typeof buildId === 'string' && buildId) {
      knownRemoteBuild = buildId;
      if (buildId !== runningBuild) showUpdate(buildId);
    }
  }

  if (serviceWorker) {
    if (typeof serviceWorker.register === 'function') {
      try {
        serviceWorker.register('./sw.js')?.catch?.(() => {});
      } catch {
        // file:// or an unsupported context: the page still works without a SW.
      }
    }

    serviceWorker.ready.then((registrationFromBrowser) => {
      if (disposed) return;
      registration = registrationFromBrowser;
      if (registration.waiting && (hadController || detectedBuild))
        void showWorkerUpdate(registration.waiting);
      updateFoundHandler = () => {
        const installing = registration.installing;
        if (!installing) return;
        installing.addEventListener?.('statechange', () => {
          if (installing.state === 'installed' && (hadController || registration.waiting))
            void showWorkerUpdate(installing);
        });
      };
      registration.addEventListener?.('updatefound', updateFoundHandler);
    }).catch?.(() => {});

    serviceWorker.addEventListener?.('controllerchange', onControllerChange);
    serviceWorker.addEventListener?.('message', onVersionMessage);
  }

  function onVisible() {
    if (!doc.hidden) void checkVersion();
  }

  doc.addEventListener?.('visibilitychange', onVisible);
  win.addEventListener?.('focus', onVisible);
  win.addEventListener?.('pageshow', onVisible);
  const scheduleInterval = (fn, ms) => (typeof win.setInterval === 'function'
    ? win.setInterval.call(win, fn, ms)
    : setInterval(fn, ms));
  const cancelInterval = (id) => {
    if (typeof win.clearInterval === 'function') win.clearInterval.call(win, id);
    else clearInterval(id);
  };
  const timer = scheduleInterval(() => { void checkVersion(); }, intervalMs);

  void checkVersion(); // Page load.

  return () => {
    disposed = true;
    cancelInterval(timer);
    doc.removeEventListener?.('visibilitychange', onVisible);
    win.removeEventListener?.('focus', onVisible);
    win.removeEventListener?.('pageshow', onVisible);
    reloadButton.removeEventListener?.('click', onReloadClick);
    dismissButton?.removeEventListener?.('click', onDismissClick);
    serviceWorker?.removeEventListener?.('controllerchange', onControllerChange);
    serviceWorker?.removeEventListener?.('message', onVersionMessage);
    if (registration && updateFoundHandler)
      registration.removeEventListener?.('updatefound', updateFoundHandler);
  };
}

/**
 * Wire the static interface. Dependencies are injectable so the browser logic
 * can be exercised without changing or replacing the generated WASM bundle.
 */
export function initApp({
  doc = document,
  win = window,
  moduleFactory = createModule,
  fetchImpl = fetch,
  leaflet = win.L,
} = {}) {
  const app = requiredElement(doc, 'app');
  const form = requiredElement(doc, 'conversion-controls');
  const dropzone = requiredElement(doc, 'dropzone');
  const fileInput = requiredElement(doc, 'gpx-file');
  const sampleButton = requiredElement(doc, 'load-sample');
  const sourceNameElement = requiredElement(doc, 'source-name');
  const courseNameInput = requiredElement(doc, 'course-name');
  const activitySelect = requiredElement(doc, 'activity-type');
  const paceInput = requiredElement(doc, 'pace');
  const paceUnitSelect = requiredElement(doc, 'pace-unit');
  const passModeSelect = requiredElement(doc, 'pass-mode');
  const snapInput = requiredElement(doc, 'snap-distance');
  const snapSlider = requiredElement(doc, 'snap-slider');
  const maxPointsInput = optionalElement(doc, 'max-points');
  const startTimeInput = optionalElement(doc, 'start-time');
  const strictModeCheckbox = optionalElement(doc, 'strict-mode');
  const mapRulesInput = optionalElement(doc, 'map-rules');
  const status = requiredElement(doc, 'status');
  const errorMessage = requiredElement(doc, 'error-message');
  const results = requiredElement(doc, 'results');
  const warnings = requiredElement(doc, 'warnings');
  const warningStatus = requiredElement(doc, 'warning-status');
  const downloadLink = requiredElement(doc, 'download-fit');
  const transferInstructions = optionalElement(doc, 'transfer-instructions');
  const statDistance = requiredElement(doc, 'stat-distance');
  const statTrackPoints = requiredElement(doc, 'stat-track-points');
  const statCoursePoints = requiredElement(doc, 'stat-course-points');
  const statWaypoints = requiredElement(doc, 'stat-waypoints');
  const mapElement = requiredElement(doc, 'route-map');
  const mapFallback = requiredElement(doc, 'map-fallback');
  const waypointTableBody = requiredElement(doc, 'waypoint-table-body');

  let source = null;
  let requestVersion = 0;
  let convertTimer = null;
  let modulePromise = null;
  let downloadUrl = null;
  let dragDepth = 0;
  let routeMap = null;
  let routeLayers = null;
  let mappedSource = null;

  function clearConvertTimer() {
    if (convertTimer !== null) {
      win.clearTimeout(convertTimer);
      convertTimer = null;
    }
  }

  function clearDownload() {
    downloadLink.hidden = true;
    downloadLink.removeAttribute('href');
    downloadLink.removeAttribute('download');
    if (downloadUrl !== null) {
      win.URL.revokeObjectURL(downloadUrl);
      downloadUrl = null;
    }
  }

  function invalidateOutput() {
    clearConvertTimer();
    clearDownload();
    if (transferInstructions) transferInstructions.hidden = true;
    results.hidden = true;
    warnings.hidden = true;
    warnings.textContent = '';
    warningStatus.textContent = '';
  }

  function setStatus(message, state = 'idle') {
    status.textContent = message;
    status.dataset.state = state;
    app.setAttribute('aria-busy', String(state === 'busy'));
  }

  function showError(message) {
    setStatus('The route could not be converted.', 'error');
    errorMessage.textContent = message;
    errorMessage.hidden = false;
  }

  function clearError() {
    errorMessage.textContent = '';
    errorMessage.hidden = true;
  }

  function getModule() {
    if (!modulePromise) {
      modulePromise = Promise.resolve()
        .then(() => moduleFactory())
        .catch((error) => {
          modulePromise = null;
          throw error;
        });
    }
    return modulePromise;
  }

  function readOptions() {
    const activity = ACTIVITIES[activitySelect.value];
    if (!activity) throw new Error('Choose a valid activity type.');

    const snapText = snapInput.value.trim();
    const snap = Number(snapText);
    if (!snapText || !Number.isFinite(snap) || !Number.isInteger(snap) || snap < 0 || snap > 1000)
      throw new Error('Snap distance must be a whole number from 0 to 1000 meters.');
    if (!PASS_MODES.has(passModeSelect.value))
      throw new Error('Choose a valid pass selection.');

    let maxPoints = 0;
    if (maxPointsInput) {
      if (maxPointsInput.validity?.badInput) {
        throw new Error('Maximum track points must be a whole number of at least 2.');
      }
      const maxPointsText = String(maxPointsInput.value || '').trim();
      if (maxPointsText) {
        const value = Number(maxPointsText);
        if (!/^\d+$/.test(maxPointsText) || !Number.isSafeInteger(value) || value < 2) {
          throw new Error('Maximum track points must be a whole number of at least 2.');
        }
        if (value > MAX_WASM_LONG) {
          throw new Error('Maximum track points is too large.');
        }
        maxPoints = value;
      }
    }

    const startFit = startTimeInput
      ? startToFitTime(startTimeInput.value)
      : DEFAULT_START_FIT;
    const strict = strictModeCheckbox?.checked ? 1 : 0;
    const mapText = mapRulesInput ? validateMapRules(mapRulesInput.value) : '';

    return {
      courseName: courseNameInput.value.trim(),
      sport: activity.sport,
      subSport: activity.subSport,
      pace: paceToMetersPerSecond(paceInput.value, paceUnitSelect.value),
      snap,
      passMode: passModeSelect.value,
      mapText,
      startFit,
      maxPoints,
      strict,
    };
  }

  function convert(wasm, gpxXml, options) {
    const result = wasm.ccall(
      'gpx2fit_convert',
      'number',
      [...ARGUMENT_TYPES],
      [
        gpxXml,
        options.mapText || null,
        options.courseName || null,
        options.sport,
        options.subSport,
        options.snap,
        options.passMode,
        options.pace,
        options.startFit,
        options.maxPoints,
        options.strict,
      ],
    );
    if (!result) throw new Error('The converter could not allocate a result.');

    try {
      const jsonPointer = wasm._gpx2fit_result_json(result);
      if (!jsonPointer) throw new Error('The converter returned no metadata.');
      const metadata = JSON.parse(wasm.UTF8ToString(jsonPointer));
      const rc = wasm._gpx2fit_result_rc(result);
      if (rc !== 0 || !metadata.ok)
        throw new Error(metadata.error || 'The GPX route could not be converted.');

      const length = Number(wasm._gpx2fit_result_len(result));
      const dataPointer = wasm._gpx2fit_result_data(result);
      if (!Number.isSafeInteger(length) || length <= 0 || !dataPointer)
        throw new Error('The converter returned an empty FIT course.');
      const fit = wasm.HEAPU8.slice(dataPointer, dataPointer + length);
      return { fit, metadata };
    } finally {
      wasm._gpx2fit_free(result);
    }
  }

  function renderWaypointTable(metadata) {
    const snapped = Array.isArray(metadata.course_points_list) ? metadata.course_points_list : [];
    const skipped = Array.isArray(metadata.skipped_waypoints_list) ? metadata.skipped_waypoints_list : [];
    const points = [
      ...snapped.map((point) => ({ point, skipped: false })),
      ...skipped.map((point) => ({ point, skipped: true })),
    ].sort((a, b) => Number(a.point.km) - Number(b.point.km));

    waypointTableBody.replaceChildren();
    if (!points.length) {
      const row = doc.createElement('tr');
      const cell = doc.createElement('td');
      cell.colSpan = 5;
      cell.className = 'empty-waypoints';
      cell.textContent = 'No waypoints in this GPX route.';
      row.append(cell);
      waypointTableBody.append(row);
      return;
    }

    for (const { point, skipped: isSkipped } of points) {
      const type = fitPointType(point.type);
      const row = doc.createElement('tr');
      row.dataset.status = isSkipped ? 'skipped' : 'snapped';

      const name = doc.createElement('th');
      name.scope = 'row';
      name.textContent = point.name || '(unnamed)';
      row.append(name);

      const state = doc.createElement('td');
      const badge = doc.createElement('span');
      badge.className = `waypoint-status ${isSkipped ? 'is-skipped' : 'is-snapped'}`;
      badge.textContent = isSkipped ? 'Skipped' : 'Snapped';
      state.append(badge);
      row.append(state);

      const distance = doc.createElement('td');
      distance.className = 'numeric-cell';
      distance.textContent = formatKilometers(point.km);
      row.append(distance);

      const offset = doc.createElement('td');
      offset.className = 'numeric-cell';
      offset.textContent = formatOffset(point.offset_m);
      row.append(offset);

      const iconType = doc.createElement('td');
      const typeIcon = doc.createElement('span');
      typeIcon.className = 'table-type-icon';
      typeIcon.style.setProperty('--type-color', type.color);
      typeIcon.setAttribute('aria-hidden', 'true');
      typeIcon.textContent = type.icon;
      iconType.append(typeIcon, doc.createTextNode(type.label));
      row.append(iconType);
      waypointTableBody.append(row);
    }
  }

  function waypointPopup(point, isSkipped) {
    const type = fitPointType(point.type);
    const popup = doc.createElement('div');
    popup.className = 'waypoint-popup';
    const name = doc.createElement('strong');
    name.textContent = point.name || '(unnamed)';
    const details = doc.createElement('span');
    details.textContent = `${type.label} · ${formatKilometers(point.km)} km · ${formatOffset(point.offset_m)} m offset`;
    popup.append(name, details);
    if (isSkipped) {
      const note = doc.createElement('span');
      note.className = 'skipped-popup-note';
      note.textContent = 'Skipped from the FIT course. Increase Snap distance to include it.';
      popup.append(note);
    }
    return popup;
  }

  function renderRouteMap(metadata, loadedSource) {
    if (!leaflet) {
      mapElement.hidden = true;
      mapFallback.hidden = false;
      return;
    }

    try {
      mapElement.hidden = false;
      mapFallback.hidden = true;
      if (!routeMap) {
        routeMap = leaflet.map(mapElement, { scrollWheelZoom: false });
        leaflet.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
          maxZoom: 19,
          attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
        }).addTo(routeMap);
        routeLayers = leaflet.layerGroup().addTo(routeMap);
      }
      routeLayers.clearLayers();
      routeMap.invalidateSize({ pan: false });

      const rawTrack = (Array.isArray(metadata.track_points_list) ? metadata.track_points_list : [])
        .map(pointCoordinates)
        .filter(Boolean);
      const track = unwrapTrackCoordinates(rawTrack);
      const snapped = Array.isArray(metadata.course_points_list) ? metadata.course_points_list : [];
      const skipped = Array.isArray(metadata.skipped_waypoints_list) ? metadata.skipped_waypoints_list : [];
      const bounds = leaflet.latLngBounds([]);

      if (track.length > 1) {
        const line = leaflet.polyline(track, {
          color: '#205c43',
          weight: 4,
          opacity: 0.85,
        }).addTo(routeLayers);
        bounds.extend(line.getBounds());
      }

      const addMarker = (point, isSkipped) => {
        const coordinates = coordinatesInTrackFrame(point, rawTrack, track);
        if (!coordinates) return;
        const type = fitPointType(point.type);
        const name = point.name || '(unnamed)';
        const icon = leaflet.divIcon({
          className: 'waypoint-marker',
          html: `<span class="waypoint-pin${isSkipped ? ' is-skipped' : ''}" style="--marker-color:${type.color}" aria-hidden="true">${type.icon}</span>`,
          iconSize: [30, 30],
          iconAnchor: [15, 15],
          popupAnchor: [0, -13],
        });
        const marker = leaflet.marker(coordinates, {
          icon,
          keyboard: true,
          riseOnHover: true,
          title: `${name} — ${type.label}${isSkipped ? ' (skipped)' : ''}`,
          alt: `${name}, ${type.label}${isSkipped ? ', skipped' : ''}`,
        }).addTo(routeLayers);
        marker.bindPopup(waypointPopup(point, isSkipped));
        bounds.extend(coordinates);
      };
      snapped.forEach((point) => addMarker(point, false));
      skipped.forEach((point) => addMarker(point, true));

      if (mappedSource !== loadedSource && bounds.isValid()) {
        routeMap.fitBounds(bounds, { padding: [20, 20], maxZoom: 16, animate: false });
      }
      mappedSource = loadedSource;
    } catch {
      mapElement.hidden = true;
      mapFallback.hidden = false;
    }
  }

  function showResults(output, options, loadedSource) {
    const { metadata } = output;
    statDistance.textContent = `${(metadata.distance_m / 1000).toFixed(2)} km`;
    statTrackPoints.textContent = String(metadata.track_points);
    statCoursePoints.textContent = String(metadata.course_points);
    statWaypoints.textContent = `${metadata.waypoints_snapped}/${metadata.waypoints_total} snapped`;

    const warningText = Array.isArray(metadata.warnings)
      ? metadata.warnings.map((warning) => String(warning).trim()).filter(Boolean).join('\n')
      : '';
    warnings.textContent = warningText;
    warnings.hidden = !warningText;
    warningStatus.textContent = warningText;
    results.hidden = false;
    renderWaypointTable(metadata);
    renderRouteMap(metadata, loadedSource);

    const blob = new Blob([output.fit], { type: 'application/octet-stream' });
    downloadUrl = win.URL.createObjectURL(blob);
    downloadLink.href = downloadUrl;
    downloadLink.download = fitFileName(options.courseName || metadata.course_name, loadedSource.name);
    downloadLink.hidden = false;
    if (transferInstructions) transferInstructions.hidden = false;
    setStatus(`${loadedSource.name} converted successfully. Your FIT course is ready.`, 'success');
  }

  async function runConversion(version, loadedSource) {
    clearConvertTimer();
    invalidateOutput();
    clearError();

    let options;
    try {
      options = readOptions();
    } catch (error) {
      showError(errorText(error));
      return;
    }

    setStatus(`Converting ${loadedSource.name} locally…`, 'busy');
    try {
      const wasm = await getModule();
      if (version !== requestVersion || loadedSource !== source) return;
      const output = convert(wasm, loadedSource.xml, options);
      if (version !== requestVersion || loadedSource !== source) return;
      showResults(output, options, loadedSource);
    } catch (error) {
      if (version !== requestVersion || loadedSource !== source) return;
      invalidateOutput();
      showError(errorText(error));
    }
  }

  function requestConversion({ immediate = false } = {}) {
    if (!source) return;
    const version = ++requestVersion;
    const loadedSource = source;
    invalidateOutput();
    clearError();
    setStatus('Applying course settings…', 'busy');
    if (immediate) {
      void runConversion(version, loadedSource);
    } else {
      convertTimer = win.setTimeout(() => {
        void runConversion(version, loadedSource);
      }, CONVERT_DELAY_MS);
    }
  }

  async function loadSource(loader, name) {
    const version = ++requestVersion;
    source = null;
    invalidateOutput();
    clearError();
    sourceNameElement.textContent = name;
    setStatus(`Reading ${name}…`, 'busy');

    let loadedSource;
    try {
      const xml = await loader();
      if (version !== requestVersion) return;
      if (!xml.trim()) throw new Error('The GPX file is empty.');
      loadedSource = { xml, name };
      source = loadedSource;
    } catch (error) {
      if (version !== requestVersion) return;
      sourceNameElement.textContent = 'No route loaded';
      showError(errorText(error));
      return;
    }

    await runConversion(version, loadedSource);
  }

  function validGpxFile(file) {
    return file && typeof file.name === 'string' && /\.gpx$/i.test(file.name);
  }

  function loadFile(file) {
    if (!validGpxFile(file)) {
      ++requestVersion;
      source = null;
      invalidateOutput();
      sourceNameElement.textContent = 'No route loaded';
      showError('Choose a file with a .gpx extension.');
      return;
    }
    if (typeof file.text !== 'function') {
      ++requestVersion;
      source = null;
      invalidateOutput();
      sourceNameElement.textContent = 'No route loaded';
      showError('This browser cannot read the selected file. Try a current browser.');
      return;
    }
    void loadSource(() => file.text(), file.name);
  }

  fileInput.addEventListener('change', () => {
    const [file] = fileInput.files || [];
    fileInput.value = '';
    if (file) loadFile(file);
  });

  sampleButton.addEventListener('click', () => {
    void loadSource(async () => {
      const response = await fetchImpl('./caltopo_sample.gpx', {
        credentials: 'same-origin',
        cache: 'force-cache',
      });
      if (!response.ok) throw new Error(`The sample route could not be loaded (HTTP ${response.status}).`);
      return response.text();
    }, 'caltopo_sample.gpx');
  });

  dropzone.addEventListener('dragenter', (event) => {
    event.preventDefault();
    dragDepth += 1;
    dropzone.classList.add('is-dragging');
  });
  dropzone.addEventListener('dragover', (event) => {
    event.preventDefault();
    if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy';
    dropzone.classList.add('is-dragging');
  });
  dropzone.addEventListener('dragleave', (event) => {
    event.preventDefault();
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) dropzone.classList.remove('is-dragging');
  });
  dropzone.addEventListener('drop', (event) => {
    event.preventDefault();
    dragDepth = 0;
    dropzone.classList.remove('is-dragging');
    const files = event.dataTransfer ? event.dataTransfer.files : [];
    if (!files || files.length !== 1) {
      ++requestVersion;
      source = null;
      invalidateOutput();
      sourceNameElement.textContent = 'No route loaded';
      showError('Drop exactly one GPX file.');
      return;
    }
    loadFile(files[0]);
  });

  form.addEventListener('submit', (event) => event.preventDefault());
  courseNameInput.addEventListener('input', () => requestConversion());
  courseNameInput.addEventListener('change', () => requestConversion({ immediate: true }));
  paceInput.addEventListener('input', () => requestConversion());
  paceInput.addEventListener('change', () => requestConversion({ immediate: true }));
  paceUnitSelect.addEventListener('change', () => requestConversion({ immediate: true }));
  activitySelect.addEventListener('change', () => requestConversion({ immediate: true }));
  passModeSelect.addEventListener('change', () => requestConversion({ immediate: true }));

  if (maxPointsInput) {
    maxPointsInput.addEventListener('input', () => requestConversion());
    maxPointsInput.addEventListener('change', () => requestConversion({ immediate: true }));
  }
  if (startTimeInput) {
    startTimeInput.addEventListener('input', () => requestConversion());
    startTimeInput.addEventListener('change', () => requestConversion({ immediate: true }));
  }
  if (strictModeCheckbox) {
    strictModeCheckbox.addEventListener('change', () => requestConversion({ immediate: true }));
  }
  if (mapRulesInput) {
    mapRulesInput.addEventListener('input', () => requestConversion());
    mapRulesInput.addEventListener('change', () => requestConversion({ immediate: true }));
  }

  snapSlider.addEventListener('input', () => {
    snapInput.value = snapSlider.value;
    requestConversion();
  });
  snapSlider.addEventListener('change', () => {
    snapInput.value = snapSlider.value;
    requestConversion({ immediate: true });
  });
  snapInput.addEventListener('input', () => {
    const value = Number(snapInput.value);
    if (snapInput.value.trim() && Number.isInteger(value) && value >= 0 && value <= 1000)
      snapSlider.value = String(value);
    requestConversion();
  });
  snapInput.addEventListener('change', () => {
    const value = Number(snapInput.value);
    if (snapInput.value.trim() && Number.isInteger(value) && value >= 0 && value <= 1000)
      snapSlider.value = String(value);
    requestConversion({ immediate: true });
  });

  win.addEventListener('pagehide', clearDownload);
  win.addEventListener('pageshow', (event) => {
    if (event.persisted && source) requestConversion({ immediate: true });
  });
}

if (typeof window !== 'undefined' && typeof document !== 'undefined') {
  // Start update detection first so a stale cached UI can still offer a reload.
  initUpdateDetector();
  initApp();
}
