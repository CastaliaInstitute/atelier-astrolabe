const CACHE_NAME = "astrolabe-pwa-v3";
const CORE = [
  "/",
  "/manifest.webmanifest",
  "/icon.svg",
  "/faculty-voice-sim.js",
  "/ring-face-sim.js",
  "/assets/faculty/a.einstein.png",
  "/assets/faculty/hypatia.png",
  "/assets/faculty/marie-curie.png",
];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(CORE)).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))))
      .then(() => self.clients.claim()),
  );
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin || url.pathname.startsWith("/api/")) {
    return;
  }
  event.respondWith(fetch(event.request).catch(() => caches.match(event.request)));
});
