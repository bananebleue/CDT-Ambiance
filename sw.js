// CDT Ambiance — fonctionnement hors connexion.
// Les fichiers de l'application sont mis en cache à l'installation ;
// les polices Google sont mises en cache au premier chargement.
const VERSION = 'cdt-ambiance-v1';
const APP_FILES = [
  './',
  './index.html',
  './manifest.webmanifest',
  './icons/icon-192.png',
  './icons/icon-512.png',
  './icons/icon-maskable-512.png',
  './icons/apple-touch-icon.png',
  './icons/favicon-32.png'
];

self.addEventListener('install', event => {
  event.waitUntil(caches.open(VERSION).then(cache => cache.addAll(APP_FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', event => {
  const req = event.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);

  // Polices Google : cache d'abord, réseau en secours.
  if (url.hostname === 'fonts.googleapis.com' || url.hostname === 'fonts.gstatic.com') {
    event.respondWith(
      caches.open(VERSION).then(cache =>
        cache.match(req).then(hit => hit || fetch(req).then(res => { cache.put(req, res.clone()); return res; }))
      )
    );
    return;
  }

  // Fichiers de l'application : réseau d'abord (pour recevoir les mises à jour), cache hors connexion.
  if (url.origin === self.location.origin) {
    event.respondWith(
      fetch(req)
        .then(res => { const copy = res.clone(); caches.open(VERSION).then(cache => cache.put(req, copy)); return res; })
        .catch(() => caches.match(req).then(hit => hit || caches.match('./index.html')))
    );
  }
});
