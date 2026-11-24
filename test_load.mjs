import createModule from './web/dist/gpx2fit.js';
const mod = await createModule();
console.log(Object.keys(mod));
