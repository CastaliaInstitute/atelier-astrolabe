const CACHE = "lunasay-app-v12";
const ASSETS = [
  "./",
  "./index.html",
  "./app.css?v=12",
  "./relationship.js",
  "./device.js",
  "./app.js",
  "./manifest.webmanifest",
];
self.addEventListener("install", (event) =>
  event.waitUntil(
    caches
      .open(CACHE)
      .then((cache) => cache.addAll(ASSETS))
      .then(() => self.skipWaiting()),
  ),
);
self.addEventListener("activate", (event) =>
  event.waitUntil(
    Promise.all([
      self.clients.claim(),
      caches
        .keys()
        .then((keys) =>
          Promise.all(
            keys
              .filter((key) => key !== CACHE)
              .map((key) => caches.delete(key)),
          ),
        ),
    ]),
  ),
);
self.addEventListener("fetch", (event) => {
  if (event.request.method === "GET") {
    event.respondWith(
      caches
        .match(event.request)
        .then((response) => response || fetch(event.request)),
    );
  }
});
