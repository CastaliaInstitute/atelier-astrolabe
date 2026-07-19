const SPOTIFY_CLIENT_ID = "5c02a959bc894e6ba0d5c338f7bcc60a";
const SPOTIFY_SCOPES = "user-read-playback-state user-modify-playback-state";
const BLE_SERVICE_UUID = "01000000-5017-0065-6261-6c6f72747341";
const BLE_SETTINGS_UUID = "03000000-5017-0065-6261-6c6f72747341";
const CHUNK_BYTES = 80;
const SESSION_PREFIX = "astrolabe.skype.";

const els = {
  card: document.querySelector("[data-card]"),
  kicker: document.querySelector("[data-kicker]"),
  title: document.querySelector("[data-title]"),
  message: document.querySelector("[data-message]"),
  detail: document.querySelector("[data-detail]"),
  authorize: document.querySelector("[data-authorize]"),
  pair: document.querySelector("[data-pair]"),
  retry: document.querySelector("[data-retry]"),
  install: document.querySelector("[data-install]"),
  steps: [...document.querySelectorAll("[data-step]")],
};

let refreshToken = sessionStorage.getItem(`${SESSION_PREFIX}refreshToken`) || "";
let installPrompt;

function redirectUri() {
  return new URL("./", window.location.href).toString().split("?")[0].split("#")[0];
}

function randomUrlSafe(length = 64) {
  const bytes = new Uint8Array(length);
  crypto.getRandomValues(bytes);
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

async function sha256(value) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return btoa(String.fromCharCode(...new Uint8Array(digest)))
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");
}

function setStep(active) {
  const order = ["spotify", "astrolabe", "ready"];
  const current = order.indexOf(active);
  for (const item of els.steps) {
    const index = order.indexOf(item.dataset.step);
    item.classList.toggle("active", index === current);
    item.classList.toggle("complete", index < current);
  }
}

function setView({ kicker, title, message, detail = "", error = false, authorize = false, pair = false, retry = false, busy = false, step = "spotify" }) {
  els.kicker.textContent = kicker;
  els.title.textContent = title;
  els.message.textContent = message;
  els.detail.textContent = detail;
  els.detail.classList.toggle("error", error);
  els.authorize.hidden = !authorize;
  els.pair.hidden = !pair;
  els.retry.hidden = !retry;
  els.authorize.disabled = busy;
  els.pair.disabled = busy;
  els.retry.disabled = busy;
  els.card.setAttribute("aria-busy", String(busy));
  setStep(step);
}

async function authorizeSpotify() {
  const verifier = randomUrlSafe(72);
  const state = randomUrlSafe(24);
  sessionStorage.setItem(`${SESSION_PREFIX}verifier`, verifier);
  sessionStorage.setItem(`${SESSION_PREFIX}state`, state);
  const url = new URL("https://accounts.spotify.com/authorize");
  url.search = new URLSearchParams({
    client_id: SPOTIFY_CLIENT_ID,
    response_type: "code",
    redirect_uri: redirectUri(),
    scope: SPOTIFY_SCOPES,
    state,
    code_challenge_method: "S256",
    code_challenge: await sha256(verifier),
    show_dialog: "true",
  }).toString();
  window.location.assign(url);
}

async function exchangeCode(code) {
  const verifier = sessionStorage.getItem(`${SESSION_PREFIX}verifier`) || "";
  if (!verifier) throw new Error("The pairing session expired. Start again.");
  const response = await fetch("https://accounts.spotify.com/api/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      client_id: SPOTIFY_CLIENT_ID,
      grant_type: "authorization_code",
      code,
      redirect_uri: redirectUri(),
      code_verifier: verifier,
    }),
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok || !body.refresh_token) {
    throw new Error(body.error_description || body.error || `Spotify token exchange failed (${response.status}).`);
  }
  refreshToken = body.refresh_token;
  sessionStorage.setItem(`${SESSION_PREFIX}refreshToken`, refreshToken);
  sessionStorage.removeItem(`${SESSION_PREFIX}verifier`);
  sessionStorage.removeItem(`${SESSION_PREFIX}state`);
  history.replaceState(null, "", redirectUri());
}

async function writeChunk(characteristic, value) {
  const bytes = new TextEncoder().encode(value);
  if (typeof characteristic.writeValueWithResponse === "function") {
    await characteristic.writeValueWithResponse(bytes);
  } else {
    await characteristic.writeValue(bytes);
  }
}

async function pairAstrolabe() {
  if (!refreshToken) throw new Error("Authorize Spotify first.");
  if (!navigator.bluetooth) {
    throw new Error("Web Bluetooth is unavailable. Use Chrome on Android, macOS, Windows, or Linux.");
  }

  setView({
    kicker: "Nearby Bluetooth",
    title: "Choose your Astrolabe",
    message: "Select the Astrolabe shown by the browser. Keep the watch awake and nearby.",
    detail: "Waiting for the Bluetooth chooser…",
    busy: true,
    step: "astrolabe",
  });

  const device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [BLE_SERVICE_UUID] }],
    optionalServices: [BLE_SERVICE_UUID],
  });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(BLE_SERVICE_UUID);
  const characteristic = await service.getCharacteristic(BLE_SETTINGS_UUID);
  const payload = JSON.stringify({
    spotify: {
      clientId: SPOTIFY_CLIENT_ID,
      refreshToken,
    },
  });

  await writeChunk(characteristic, "BEGIN");
  for (let offset = 0; offset < payload.length; offset += CHUNK_BYTES) {
    await writeChunk(characteristic, payload.slice(offset, offset + CHUNK_BYTES));
  }
  await writeChunk(characteristic, "END");
  device.gatt.disconnect();
  refreshToken = "";
  sessionStorage.removeItem(`${SESSION_PREFIX}refreshToken`);

  setView({
    kicker: "Pairing complete",
    title: `${device.name || "Astrolabe"} is ready`,
    message: "Open the Spotify face and tap the record to play or pause the active Spotify Connect device.",
    detail: "The browser copy of the refresh credential has been cleared.",
    retry: true,
    step: "ready",
  });
  els.card.dataset.playing = "true";
}

function reset() {
  refreshToken = "";
  for (const key of ["refreshToken", "verifier", "state"]) sessionStorage.removeItem(`${SESSION_PREFIX}${key}`);
  history.replaceState(null, "", redirectUri());
  els.card.dataset.playing = "false";
  setView({
    kicker: "Ready to begin",
    title: "Connect your Spotify account",
    message: "Spotify will ask permission to see playback state and control your existing Spotify devices.",
    authorize: true,
    step: "spotify",
  });
}

async function initialize() {
  const params = new URLSearchParams(window.location.search);
  const error = params.get("error");
  if (error) throw new Error(error === "access_denied" ? "Spotify permission was not granted." : `Spotify authorization failed: ${error}`);

  const code = params.get("code");
  if (code) {
    const expectedState = sessionStorage.getItem(`${SESSION_PREFIX}state`) || "";
    if (!expectedState || params.get("state") !== expectedState) throw new Error("Spotify returned an invalid pairing state.");
    setView({
      kicker: "Spotify authorization",
      title: "Finishing the connection",
      message: "Exchanging the temporary authorization code…",
      busy: true,
      step: "spotify",
    });
    await exchangeCode(code);
  }

  if (refreshToken) {
    setView({
      kicker: "Spotify authorized",
      title: "Now choose your Astrolabe",
      message: "The credential is held only for this browser session until it is sent over Bluetooth.",
      pair: true,
      retry: true,
      step: "astrolabe",
    });
    return;
  }
  reset();
}

els.authorize.addEventListener("click", () => authorizeSpotify().catch(showError));
els.pair.addEventListener("click", () => pairAstrolabe().catch(showError));
els.retry.addEventListener("click", reset);

function showError(error) {
  setView({
    kicker: "Needs attention",
    title: "Pairing did not finish",
    message: "Nothing was changed on Astrolabe.",
    detail: error instanceof Error ? error.message : String(error),
    error: true,
    retry: true,
    step: refreshToken ? "astrolabe" : "spotify",
  });
}

window.addEventListener("beforeinstallprompt", (event) => {
  event.preventDefault();
  installPrompt = event;
  els.install.hidden = false;
});

els.install.addEventListener("click", async () => {
  if (!installPrompt) return;
  await installPrompt.prompt();
  installPrompt = undefined;
  els.install.hidden = true;
});

if ("serviceWorker" in navigator) navigator.serviceWorker.register("service-worker.js").catch(() => {});
initialize().catch(showError);
