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
  const snapInput = requiredElement(doc, 'snap-distance');
  const snapSlider = requiredElement(doc, 'snap-slider');
  const status = requiredElement(doc, 'status');
  const errorMessage = requiredElement(doc, 'error-message');
  const results = requiredElement(doc, 'results');
  const warnings = requiredElement(doc, 'warnings');
  const downloadLink = requiredElement(doc, 'download-fit');
  const statDistance = requiredElement(doc, 'stat-distance');
  const statTrackPoints = requiredElement(doc, 'stat-track-points');
  const statCoursePoints = requiredElement(doc, 'stat-course-points');
  const statWaypoints = requiredElement(doc, 'stat-waypoints');

  let source = null;
  let requestVersion = 0;
  let convertTimer = null;
  let modulePromise = null;
  let downloadUrl = null;
  let dragDepth = 0;

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

    return {
      courseName: courseNameInput.value.trim(),
      sport: activity.sport,
      subSport: activity.subSport,
      pace: paceToMetersPerSecond(paceInput.value, paceUnitSelect.value),
      snap,
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
        'nearest',
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
    results.hidden = false;

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
