const test = require("node:test");
const assert = require("node:assert/strict");
const device = require("./device.js");

test("device health view reports live power and signed OTA polling state", () => {
  const view = device.view(
    {
      wifi: { ssid: "Monument" },
      device: {
        firmware: "6ea5fe76",
        uptimeMs: 3_700_000,
        battery: {
          available: true,
          present: true,
          percent: 84,
          charging: true,
          usbPower: true,
        },
        ble: { enabled: true, advertising: false },
        capabilities: { clearReadingHistory: true },
        ota: {
          autoStarted: true,
          networkReady: true,
          heapReady: true,
          lastPollUptimeMs: 3_580_000,
          last: "already current",
        },
      },
    },
    true,
  );
  assert.deepEqual(view, {
    healthTitle: "LunaSay is responding.",
    battery: "84%",
    power: "charging",
    wifi: "Monument",
    ble: "connected",
    healthNote: "Read directly from LunaSay · uptime 1h.",
    firmware: "Firmware 6ea5fe76",
    otaAuto: "on",
    otaCheck: "2m ago",
    otaNetwork: "ready",
    otaNote: "already current",
    canClearReadingHistory: true,
  });
});

test("device health view is honest when disconnected or telemetry is absent", () => {
  const view = device.view({}, false);
  assert.equal(view.healthTitle, "Connect to inspect LunaSay.");
  assert.equal(view.battery, "unavailable");
  assert.equal(view.power, "unknown");
  assert.equal(view.wifi, "not reported");
  assert.equal(view.ble, "off");
  assert.equal(view.otaCheck, "not yet");
  assert.equal(view.otaNetwork, "waiting");
  assert.equal(view.canClearReadingHistory, false);
  assert.match(view.otaNote, /^Connect to see/);
});

test("elapsed telemetry uses compact human time", () => {
  assert.equal(device.formatElapsed(42_000), "42s ago");
  assert.equal(device.formatElapsed(3_600_000), "1h ago");
  assert.equal(device.formatElapsed(3 * 86_400_000), "3d ago");
});
