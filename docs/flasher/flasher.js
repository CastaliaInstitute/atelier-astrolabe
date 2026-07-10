import { ESPLoader, Transport } from "https://esm.sh/esptool-js@0.6.0";

const localManifestUrl = new URL("../releases/integration/manifest.json", window.location.href);
const publicManifestUrl = new URL(
  "https://astrolabe.castalia.institute/releases/integration/manifest.json"
);

const els = {
  release: document.querySelector("[data-release-select]"),
  summary: document.querySelector("[data-release-summary]"),
  baud: document.querySelector("[data-baud]"),
  address: document.querySelector("[data-address]"),
  eraseAll: document.querySelector("[data-erase-all]"),
  localFile: document.querySelector("[data-local-file]"),
  connect: document.querySelector("[data-connect]"),
  flash: document.querySelector("[data-flash]"),
  disconnect: document.querySelector("[data-disconnect]"),
  progress: document.querySelector("[data-progress]"),
  log: document.querySelector("[data-log]"),
  clearLog: document.querySelector("[data-clear-log]"),
  diagnostics: document.querySelector("[data-diagnostics]"),
  diagnosticOutput: document.querySelector("[data-diagnostic-output]"),
  refreshDiagnostics: document.querySelector("[data-refresh-diagnostics]"),
  webusbProbe: document.querySelector("[data-webusb-probe]"),
  webserialProbe: document.querySelector("[data-webserial-probe]"),
};

let manifest = null;
let release = null;
let port = null;
let transport = null;
let loader = null;

function log(line = "") {
  const stamp = new Date().toLocaleTimeString();
  els.log.textContent += `[${stamp}] ${line}\n`;
  els.log.scrollTop = els.log.scrollHeight;
}

function diagnosticLog(line = "") {
  const stamp = new Date().toLocaleTimeString();
  els.diagnosticOutput.textContent += `[${stamp}] ${line}\n`;
  els.diagnosticOutput.scrollTop = els.diagnosticOutput.scrollHeight;
}

function setBusy(isBusy) {
  els.connect.disabled = isBusy || Boolean(loader);
  els.flash.disabled = isBusy || !loader;
  els.disconnect.disabled = isBusy || !loader;
  els.release.disabled = isBusy;
  els.baud.disabled = isBusy || Boolean(loader);
  els.address.disabled = isBusy;
  els.eraseAll.disabled = isBusy;
  els.localFile.disabled = isBusy;
}

function parseAddress(value) {
  const trimmed = value.trim();
  const address = trimmed.startsWith("0x") || trimmed.startsWith("0X")
    ? Number.parseInt(trimmed.slice(2), 16)
    : Number.parseInt(trimmed, 10);
  if (!Number.isFinite(address) || address < 0) {
    throw new Error(`Invalid flash address: ${value}`);
  }
  return address;
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes)) return "-";
  if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${bytes} B`;
}

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function artifactFor(role) {
  return release?.artifacts?.find((artifact) => artifact.role === role);
}

function boolText(value) {
  return value ? "Available" : "Unavailable";
}

function formatHex(value, width = 4) {
  if (!Number.isFinite(value)) return "-";
  return `0x${value.toString(16).padStart(width, "0")}`;
}

function renderDiagnostics() {
  const serialAvailable = "serial" in navigator;
  const usbAvailable = "usb" in navigator;
  const secure = window.isSecureContext;
  const userAgent = navigator.userAgent || "unknown";
  const platform = navigator.platform || "unknown";

  els.diagnostics.innerHTML = `
    <dl>
      <div><dt>Secure context</dt><dd>${escapeHtml(boolText(secure))}</dd></div>
      <div><dt>WebSerial API</dt><dd>${escapeHtml(boolText(serialAvailable))}</dd></div>
      <div><dt>WebUSB API</dt><dd>${escapeHtml(boolText(usbAvailable))}</dd></div>
      <div><dt>Platform</dt><dd>${escapeHtml(platform)}</dd></div>
      <div><dt>Browser</dt><dd>${escapeHtml(userAgent)}</dd></div>
    </dl>
  `;
  els.webserialProbe.disabled = !serialAvailable;
  els.webusbProbe.disabled = !usbAvailable;
}

async function probeWebSerial() {
  if (!("serial" in navigator)) {
    diagnosticLog("WebSerial is not exposed by this browser.");
    return;
  }
  diagnosticLog("Opening WebSerial chooser with no filters.");
  try {
    const probePort = await navigator.serial.requestPort();
    const info = probePort.getInfo?.() || {};
    diagnosticLog(
      `WebSerial selected: usbVendorId=${formatHex(info.usbVendorId)} usbProductId=${formatHex(info.usbProductId)}`
    );
  } catch (error) {
    diagnosticLog(`WebSerial probe failed: ${error.name || "Error"}: ${error.message || error}`);
  }
}

async function probeWebUSB() {
  if (!("usb" in navigator)) {
    diagnosticLog("WebUSB is not exposed by this browser.");
    return;
  }
  diagnosticLog("Opening WebUSB chooser for Espressif VID 0x303a.");
  try {
    const device = await navigator.usb.requestDevice({
      filters: [{ vendorId: 0x303a }],
    });
    diagnosticLog(
      `WebUSB selected: ${device.productName || "unknown product"} ` +
        `VID=${formatHex(device.vendorId)} PID=${formatHex(device.productId)} ` +
        `class=${formatHex(device.deviceClass, 2)} subclass=${formatHex(device.deviceSubclass, 2)} ` +
        `protocol=${formatHex(device.deviceProtocol, 2)}`
    );
    for (const config of device.configurations || []) {
      diagnosticLog(`Configuration ${config.configurationValue}: ${config.configurationName || "unnamed"}`);
      for (const iface of config.interfaces || []) {
        for (const alt of iface.alternates || []) {
          diagnosticLog(
            `  interface ${iface.interfaceNumber} alt ${alt.alternateSetting}: ` +
              `class=${formatHex(alt.interfaceClass, 2)} subclass=${formatHex(alt.interfaceSubclass, 2)} ` +
              `protocol=${formatHex(alt.interfaceProtocol, 2)} endpoints=${alt.endpoints?.length || 0}`
          );
        }
      }
    }
    await device.close().catch(() => {});
  } catch (error) {
    diagnosticLog(`WebUSB probe failed: ${error.name || "Error"}: ${error.message || error}`);
  }
}

function renderReleaseSummary() {
  if (!release) {
    els.summary.textContent = "No release selected.";
    return;
  }

  const app = artifactFor("app");
  const extras = release.artifacts?.filter((artifact) => artifact.role !== "app") || [];
  const extraText = extras.length
    ? extras.map((artifact) => `${artifact.role} ${artifact.address}`).join(", ")
    : "app-only release";

  els.summary.innerHTML = `
    <dl>
      <div><dt>Variant</dt><dd>${escapeHtml(release.product_name)}</dd></div>
      <div><dt>Platform</dt><dd>${escapeHtml(release.device_platform)}</dd></div>
      <div><dt>Channel</dt><dd>${escapeHtml(release.ota_channel)}</dd></div>
      <div><dt>Size</dt><dd>${formatBytes(app?.bytes || release.firmware_bytes)}</dd></div>
      <div><dt>Published</dt><dd>${escapeHtml(release.git_sha?.slice(0, 7) || "unknown")}</dd></div>
      <div><dt>Artifacts</dt><dd>${escapeHtml(extraText)}</dd></div>
    </dl>
  `;
}

async function loadManifest() {
  if (!("serial" in navigator)) {
    log("WebSerial is not available. Use Chrome or Edge on HTTPS.");
    els.summary.textContent = "WebSerial is not available in this browser.";
    els.connect.disabled = true;
    return;
  }

  log(`Loading ${localManifestUrl}`);
  let response = await fetch(localManifestUrl, { cache: "no-store" });
  if (!response.ok && ["127.0.0.1", "localhost"].includes(window.location.hostname)) {
    log(`Local manifest unavailable; falling back to ${publicManifestUrl}`);
    response = await fetch(publicManifestUrl, { cache: "no-store" });
  }
  if (!response.ok) {
    throw new Error(`Manifest fetch failed: HTTP ${response.status}`);
  }
  manifest = await response.json();
  const releases = manifest.releases || [];

  els.release.replaceChildren(
    ...releases.map((item) => {
      const option = document.createElement("option");
      option.value = item.release_id;
      option.textContent = item.product_name;
      return option;
    })
  );

  release = releases[0] || null;
  renderReleaseSummary();
  log(`Loaded ${releases.length} releases from ${manifest.git_ref}@${manifest.git_sha?.slice(0, 7)}`);
}

async function connect() {
  setBusy(true);
  try {
    port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    loader = new ESPLoader({
      transport,
      baudrate: Number(els.baud.value),
      terminal: {
        clean() {
          els.log.textContent = "";
        },
        write(data) {
          els.log.textContent += data;
          els.log.scrollTop = els.log.scrollHeight;
        },
        writeLine(data) {
          log(data);
        },
      },
    });

    const chip = await loader.main("default_reset");
    log(`Connected: ${chip}`);
  } catch (error) {
    loader = null;
    if (transport) {
      await transport.disconnect().catch(() => {});
    }
    transport = null;
    port = null;
    log(`Connect failed: ${error.message || error}`);
  } finally {
    setBusy(false);
  }
}

async function releaseImage() {
  const app = artifactFor("app");
  const url = new URL(app?.url || release.firmware_url, new URL("../", window.location.href));
  log(`Fetching ${url}`);
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Firmware fetch failed: HTTP ${response.status}`);
  }
  const data = new Uint8Array(await response.arrayBuffer());
  return {
    data,
    address: Number.parseInt((app?.address || "0x10000").replace(/^0x/i, ""), 16),
    name: app?.name || "firmware.bin",
  };
}

async function selectedImage() {
  const file = els.localFile.files?.[0];
  if (file) {
    return {
      data: new Uint8Array(await file.arrayBuffer()),
      address: parseAddress(els.address.value),
      name: file.name,
    };
  }
  if (!release) {
    throw new Error("No release selected");
  }
  return releaseImage();
}

async function flash() {
  if (!loader) {
    log("Connect before flashing.");
    return;
  }

  setBusy(true);
  els.progress.value = 0;
  try {
    const image = await selectedImage();
    if (els.eraseAll.checked && image.address !== 0) {
      throw new Error("Erase-all is only allowed when flashing a recovery image at 0x0.");
    }
    log(`Writing ${image.name} to 0x${image.address.toString(16)} (${formatBytes(image.data.length)})`);
    await loader.writeFlash({
      fileArray: [{ data: image.data, address: image.address }],
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "detect",
      eraseAll: els.eraseAll.checked,
      compress: true,
      reportProgress: (_fileIndex, written, total) => {
        els.progress.value = total ? Math.round((written / total) * 100) : 0;
      },
    });
    log("Flash complete. Resetting device.");
    await loader.after("hard_reset");
    log("Reset complete.");
  } catch (error) {
    log(`Flash failed: ${error.message || error}`);
  } finally {
    setBusy(false);
  }
}

async function disconnect() {
  setBusy(true);
  try {
    if (transport) {
      await transport.disconnect();
    }
    log("Disconnected.");
  } catch (error) {
    log(`Disconnect failed: ${error.message || error}`);
  } finally {
    loader = null;
    transport = null;
    port = null;
    setBusy(false);
  }
}

els.release.addEventListener("change", () => {
  release = manifest?.releases?.find((item) => item.release_id === els.release.value) || null;
  renderReleaseSummary();
});
els.connect.addEventListener("click", connect);
els.flash.addEventListener("click", flash);
els.disconnect.addEventListener("click", disconnect);
els.clearLog.addEventListener("click", () => {
  els.log.textContent = "";
});
els.refreshDiagnostics.addEventListener("click", renderDiagnostics);
els.webusbProbe.addEventListener("click", probeWebUSB);
els.webserialProbe.addEventListener("click", probeWebSerial);

renderDiagnostics();
loadManifest().catch((error) => {
  log(`Manifest error: ${error.message || error}`);
  els.summary.textContent = "Could not load the integration release manifest.";
});
