const CACHE = "synastry-pwa-v1";
const SHELL = [
  "./",
  "./index.html",
  "./about.html",
  "./css/app.css",
  "./js/app.js",
  "./js/auth.js",
  "./js/bodies.js",
  "./js/chart.js",
  "./js/config.js",
  "./js/ephemeris.js",
  "./js/profiles.js",
  "./js/synastry-math.js",
  "./js/voice.js",
  "./js/zodiac.js",
  "./manifest.webmanifest",
  "./icons/icon-192.png",
  "./icons/icon-512.png",
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE).then((cache) => cache.addAll(SHELL)).then(() => self.skipWaiting()),
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))),
    ).then(() => self.clients.claim()),
  );
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) {
    return;
  }
  event.respondWith(
    caches.match(event.request).then((cached) => {
      const network = fetch(event.request)
        .then((res) => {
          if (res.ok && event.request.method === "GET") {
            const copy = res.clone();
            caches.open(CACHE).then((c) => c.put(event.request, copy));
          }
          return res;
        })
        .catch(() => cached);
      return cached || network;
    }),
  );
});
