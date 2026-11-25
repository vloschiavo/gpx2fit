/* gpx2fit service worker (task 15.4).
 *
 * BUILD_ID and CACHE_NAME are stamped by `make web-version`; the checked-in
 * values are sensible defaults so the app still works without a build step.
 * A new cache name activates only after the page explicitly sends
 * SKIP_WAITING from the update banner's Reload button.
 */
'use strict';

const BUILD_ID = 'dev-local';
const CACHE_NAME = 'gpx2fit-dev-local';
const CACHE_PREFIX = 'gpx2fit-';
const VERSION_URL = './version.json';

const APP_SHELL = [
  './',
  './index.html',
  './favicon.svg',
  './app.js',
  './style.css',
  './Rio_Del_Lago_100mi_2025.gpx',
  './dist/gpx2fit.js',
  './dist/gpx2fit.wasm',
];

async function readVersion() {
  try {
    // Never let an HTTP or runtime cache hide a newly deployed build id.
    const response = await fetch(new Request(VERSION_URL, { cache: 'no-store' }));
    if (!response || !response.ok) return null;
    const info = await response.json();
    return info && typeof info.build_id === 'string' && info.build_id
      ? info
      : null;
  } catch {
    return null;
  }
}

self.addEventListener('install', (event) => {
  // Intentionally no self.skipWaiting() here: activation is user-controlled.
  event.waitUntil((async () => {
    const cache = await caches.open(CACHE_NAME);
    // Best effort: a missing generated wasm bundle must not break install.
    // CDN assets are intentionally excluded and handled at fetch time.
    await Promise.allSettled(
      APP_SHELL.map((url) => cache.add(new Request(url, { cache: 'reload' }))),
    );
  })());
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names
      .filter((name) => name.startsWith(CACHE_PREFIX) && name !== CACHE_NAME)
      .map((name) => caches.delete(name)));
    await self.clients.claim();
  })());
});

self.addEventListener('message', (event) => {
  const data = event.data;
  if (!data || typeof data !== 'object') return;

  if (data.type === 'SKIP_WAITING') {
    event.waitUntil(self.skipWaiting());
    return;
  }

  if (data.type === 'CHECK_VERSION') {
    event.waitUntil((async () => {
      const info = await readVersion();
      if (!info || !info.build_id || info.build_id === BUILD_ID) return;
      const clients = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
      for (const client of clients)
        client.postMessage({ type: 'VERSION_INFO', build_id: info.build_id });
    })());
  }
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET') return;

  let url;
  try {
    url = new URL(request.url);
  } catch {
    return;
  }

  if (url.pathname.endsWith('/version.json')) {
    // The update detector must always see the live file, even when offline
    // caching is active. Never cache it in the runtime cache.
    event.respondWith(fetch(new Request(request, { cache: 'no-store' })));
    return;
  }

  if (url.origin !== self.location.origin) {
    // Leaflet is loaded from a CDN. Cache only successful, readable responses;
    // opaque responses are deliberately not replayed from the precache.
    event.respondWith((async () => {
      const cache = await caches.open(CACHE_NAME);
      const cached = await cache.match(request);
      if (cached) return cached;
      const response = await fetch(request);
      if (response && response.ok
          && (response.type === 'basic' || response.type === 'cors'))
        cache.put(request, response.clone());
      return response;
    })());
    return;
  }

  if (request.mode === 'navigate') {
    event.respondWith((async () => {
      try {
        const response = await fetch(request);
        if (response && response.ok) {
          const cache = await caches.open(CACHE_NAME);
          await cache.put('./index.html', response.clone());
        }
        return response;
      } catch {
        return (await caches.match(request)) || (await caches.match('./index.html'));
      }
    })());
    return;
  }

  event.respondWith((async () => {
    const cache = await caches.open(CACHE_NAME);
    const cached = await cache.match(request);
    if (cached) return cached;
    const response = await fetch(request);
    if (response && response.ok) await cache.put(request, response.clone());
    return response || (await caches.match(request));
  })());
});
