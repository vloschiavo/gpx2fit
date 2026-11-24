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
const CONVERT_DELAY_MS = 180;
const PASS_MODES = new Set(['nearest', 'ordered', 'first', 'last']);
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
  const status = requiredElement(doc, 'status');
  const errorMessage = requiredElement(doc, 'error-message');
  const results = requiredElement(doc, 'results');
  const warnings = requiredElement(doc, 'warnings');
  const warningStatus = requiredElement(doc, 'warning-status');
  const downloadLink = requiredElement(doc, 'download-fit');
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

    return {
      courseName: courseNameInput.value.trim(),
      sport: activity.sport,
      subSport: activity.subSport,
      pace: paceToMetersPerSecond(paceInput.value, paceUnitSelect.value),
      snap,
      passMode: passModeSelect.value,
    };
  }

  function convert(wasm, gpxXml, options) {
    const result = wasm.ccall(
      'gpx2fit_convert',
      'number',
      [...ARGUMENT_TYPES],
      [
        gpxXml,
        null,
        options.courseName || null,
        options.sport,
        options.subSport,
        options.snap,
        options.passMode,
        options.pace,
        DEFAULT_START_FIT,
        0,
        0,
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

if (typeof window !== 'undefined' && typeof document !== 'undefined')
  initApp();
