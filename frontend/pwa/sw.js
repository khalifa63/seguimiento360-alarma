const V = '__V__';
const CACHE_NAME = 's360-alarmas-' + V;
const ASSETS_TO_CACHE = [
  '/app/',
  '/app/index.html',
  '/app/manifest.json?v=' + V,
  '/app/css/styles.css?v=' + V,
  '/app/js/app.js?v=' + V,
  '/app/js/config.js?v=' + V,
  '/app/js/utils.js?v=' + V,
  '/app/js/auth.js?v=' + V,
  '/app/js/api.js?v=' + V,
  '/app/js/views/login.js?v=' + V,
  '/app/js/views/register.js?v=' + V,
  '/app/js/views/dashboard.js?v=' + V
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(ASSETS_TO_CACHE))
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((names) => Promise.all(
      names.map((name) => { if (name !== CACHE_NAME) return caches.delete(name); })
    ))
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;
  if (event.request.url.includes('/api/')) return;

  if (event.request.mode === 'navigate') {
    event.respondWith(
      fetch(event.request)
        .then((res) => {
          const copy = res.clone();
          caches.open(CACHE_NAME).then((c) => c.put('/app/index.html', copy));
          return res;
        })
        .catch(() => caches.match('/app/index.html'))
    );
    return;
  }

  event.respondWith(
    caches.match(event.request).then((cached) => {
      if (cached) return cached;
      return fetch(event.request).then((res) => {
        const copy = res.clone();
        caches.open(CACHE_NAME).then((c) => c.put(event.request, copy));
        return res;
      });
    })
  );
});
