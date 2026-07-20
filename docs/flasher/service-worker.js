const CACHE_NAME = "astrolabe-flasher-v1";
const SHELL = [
  "./",
  "./index.html",
  "./flasher.js?v=20260716a",
  "./manifest.webmanifest",
  "./icon-192.png",
  "./icon-512.png",
  "../styles.css",
  "../vendor/esptool-js-0.6.0.bundle.js",
];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(SHELL)));
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((names) =>
      Promise.all(names.filter((name) => name !== CACHE_NAME).map((name) => caches.delete(name)))
    )
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") return;
  const url = new URL(event.request.url);
  if (url.origin === self.location.origin &&
      (url.pathname.startsWith("/bridge/") || url.pathname.startsWith("/releases/"))) {
    return;
  }
  event.respondWith(
    fetch(event.request)
      .then((response) => {
        if (response.ok || response.type === "opaque") {
          const copy = response.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
        }
        return response;
      })
      .catch(() => caches.match(event.request))
  );
});
