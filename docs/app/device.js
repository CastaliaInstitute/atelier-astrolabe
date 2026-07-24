(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  root.LunaSayDevice = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function formatElapsed(milliseconds) {
    const seconds = Math.max(0, Math.floor(Number(milliseconds) / 1000));
    if (!Number.isFinite(seconds)) return "unknown";
    if (seconds < 60) return `${seconds}s ago`;
    const minutes = Math.floor(seconds / 60);
    if (minutes < 60) return `${minutes}m ago`;
    const hours = Math.floor(minutes / 60);
    if (hours < 48) return `${hours}h ago`;
    return `${Math.floor(hours / 24)}d ago`;
  }

  function view(status, connected) {
    const source = status || {};
    const device = source.device || {};
    const battery = device.battery || {};
    const ble = device.ble || {};
    const ota = device.ota || {};
    const wifi = source.wifi || {};
    const uptime = Number(device.uptimeMs);
    const lastPoll = Number(ota.lastPollUptimeMs);
    const pollAge =
      Number.isFinite(uptime) &&
      Number.isFinite(lastPoll) &&
      lastPoll > 0 &&
      uptime >= lastPoll
        ? formatElapsed(uptime - lastPoll)
        : "not yet";
    let power = "unknown";
    if (battery.charging) power = "charging";
    else if (battery.usbPower) power = "USB";
    else if (battery.present) power = "battery";
    let bleState = "off";
    if (ble.enabled) {
      bleState = ble.advertising ? "advertising" : connected ? "connected" : "on";
    }
    let otaAuto = "off";
    if (ota.active) otaAuto = "installing";
    else if (ota.paused) otaAuto = "paused";
    else if (ota.autoStarted) otaAuto = "on";

    return {
      healthTitle: connected
        ? "LunaSay is responding."
        : "Connect to inspect LunaSay.",
      battery:
        battery.available && Number(battery.percent) >= 0
          ? `${battery.percent}%`
          : "unavailable",
      power,
      wifi: wifi.ssid || wifi.status || "not reported",
      ble: bleState,
      healthNote:
        connected && Number.isFinite(uptime)
          ? `Read directly from LunaSay · uptime ${formatElapsed(uptime).replace(" ago", "")}.`
          : "Health details are read directly from LunaSay and are not uploaded.",
      firmware: device.firmware ? `Firmware ${device.firmware}` : "Firmware —",
      otaAuto,
      otaCheck: pollAge,
      otaNetwork: ota.networkReady && ota.heapReady ? "ready" : "waiting",
      otaNote:
        ota.last ||
        (connected
          ? "No OTA poll result has been reported yet."
          : "Connect to see whether LunaSay is polling its signed update channel."),
      canClearReadingHistory:
        connected &&
        device.capabilities?.clearReadingHistory === true,
    };
  }

  return { formatElapsed, view };
});
