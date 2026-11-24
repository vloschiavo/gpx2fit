import { readFileSync } from 'node:fs';
import createModule from './web/dist/gpx2fit.js';

const gpx = readFileSync('tests/data/caltopo_sample.gpx', 'utf8');
const mod = await createModule();
const resPtr = mod.ccall(
  'gpx2fit_convert', 'number',
  ['string','string','string','number','number','number','string','number','number','number','number'],
  [gpx, null, 'CalTopo Sample', 17, 0, 250.0, 'nearest', 5.0/3.6, 1104624000, 0, 0]
);
console.log('rc', mod._gpx2fit_result_rc(resPtr));
const jsonPtr = mod._gpx2fit_result_json(resPtr);
const json = mod.UTF8ToString(jsonPtr);
console.log('json', json.slice(0, 500));
const len = mod._gpx2fit_result_len(resPtr);
console.log('fit len', len);
const dataPtr = mod._gpx2fit_result_data(resPtr);
const bytes = mod.HEAPU8.slice(dataPtr, dataPtr + Math.min(len, 32));
console.log('first bytes', [...bytes].map(b=>b.toString(16).padStart(2,'0')).join(' '));
mod._gpx2fit_free(resPtr);
