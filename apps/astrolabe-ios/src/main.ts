import { BleClient, type BleDevice, type ScanResult } from '@capacitor-community/bluetooth-le';
import { createIcons, icons } from 'lucide';
import './styles.css';

const SERVICE_UUID = '01000000-5017-0065-6261-6c6f72747341';
const SETTINGS_JSON_CHAR_UUID = '03000000-5017-0065-6261-6c6f72747341';
const MAX_CHUNK_BYTES = 80;

type ConnectionState = 'idle' | 'scanning' | 'connecting' | 'connected' | 'sending' | 'sent' | 'error';

type KnownDevice = {
  id: string;
  name: string;
  rssi?: number;
};

type AstrolabeStatus = {
  ok?: boolean;
  tz?: string;
  epoch?: number;
  wifi?: {
    ap?: boolean;
    ssid?: string;
    url?: string;
  };
};

const state: {
  connection: ConnectionState;
  initialized: boolean;
  scanning: boolean;
  devices: KnownDevice[];
  selected?: KnownDevice;
  status?: AstrolabeStatus;
  log: string[];
} = {
  connection: 'idle',
  initialized: false,
  scanning: false,
  devices: [],
  log: ['Ready'],
};

const appRoot = document.querySelector<HTMLDivElement>('#app');

if (!appRoot) {
  throw new Error('App root not found');
}
const app = appRoot;

function setConnection(connection: ConnectionState, message?: string) {
  state.connection = connection;
  if (message) {
    pushLog(message);
  }
  render();
}

function pushLog(message: string) {
  state.log = [`${new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })} ${message}`, ...state.log].slice(0, 5);
}

function encodeText(value: string): DataView {
  const bytes = new TextEncoder().encode(value);
  return new DataView(bytes.buffer);
}

function decodeText(value: DataView): string {
  return new TextDecoder().decode(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
}

function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (char) => {
    const entities: Record<string, string> = {
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      '"': '&quot;',
      "'": '&#39;',
    };
    return entities[char];
  });
}

function deviceName(device: BleDevice): string {
  return device.name || 'Astrolabe';
}

async function ensureBle() {
  if (state.initialized) {
    return;
  }
  await BleClient.initialize({ androidNeverForLocation: true });
  state.initialized = true;
}

async function scan() {
  await ensureBle();
  state.devices = [];
  state.selected = undefined;
  state.status = undefined;
  state.scanning = true;
  setConnection('scanning', 'Scanning for Astrolabe');

  await BleClient.requestLEScan(
    {
      services: [SERVICE_UUID],
      allowDuplicates: false,
    },
    (result: ScanResult) => {
      const known: KnownDevice = {
        id: result.device.deviceId,
        name: deviceName(result.device),
        rssi: result.rssi,
      };
      const next = state.devices.filter((device) => device.id !== known.id);
      state.devices = [...next, known].sort((a, b) => (b.rssi ?? -999) - (a.rssi ?? -999));
      render();
    },
  );

  window.setTimeout(() => {
    void stopScan();
  }, 8000);
}

async function stopScan() {
  if (!state.scanning) {
    return;
  }
  await BleClient.stopLEScan();
  state.scanning = false;
  setConnection(state.devices.length > 0 ? 'idle' : 'error', state.devices.length > 0 ? 'Scan complete' : 'No Astrolabe found');
}

async function connect(device: KnownDevice) {
  await ensureBle();
  if (state.scanning) {
    await stopScan();
  }
  state.selected = device;
  state.status = undefined;
  setConnection('connecting', `Connecting to ${device.name}`);

  await BleClient.connect(device.id, () => {
    state.selected = undefined;
    state.status = undefined;
    setConnection('idle', 'Disconnected');
  });

  const status = await readStatus(device.id);
  state.status = status;
  setConnection('connected', status?.ok ? 'Connected' : 'Connected, status unread');
}

async function readStatus(deviceId: string): Promise<AstrolabeStatus | undefined> {
  try {
    const raw = await BleClient.read(deviceId, SERVICE_UUID, SETTINGS_JSON_CHAR_UUID);
    return JSON.parse(decodeText(raw)) as AstrolabeStatus;
  } catch (error) {
    pushLog(error instanceof Error ? error.message : 'Status read failed');
    return undefined;
  }
}

async function sendProvisioning() {
  const selected = state.selected;
  if (!selected) {
    setConnection('error', 'Connect to Astrolabe first');
    return;
  }

  const ssid = inputValue('ssid');
  const password = inputValue('password');
  const includeClock = checkedValue('include-clock');
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone;
  const payload = {
    wifi: { ssid, password },
    ...(includeClock ? { tz, epoch: Math.floor(Date.now() / 1000) } : {}),
  };
  const json = JSON.stringify(payload);

  if (!ssid) {
    setConnection('error', 'Hotspot name is required');
    return;
  }

  setConnection('sending', 'Sending hotspot credentials');
  await BleClient.write(selected.id, SERVICE_UUID, SETTINGS_JSON_CHAR_UUID, encodeText('BEGIN'));

  for (let offset = 0; offset < json.length; offset += MAX_CHUNK_BYTES) {
    const chunk = json.slice(offset, offset + MAX_CHUNK_BYTES);
    await BleClient.write(selected.id, SERVICE_UUID, SETTINGS_JSON_CHAR_UUID, encodeText(chunk));
  }

  await BleClient.write(selected.id, SERVICE_UUID, SETTINGS_JSON_CHAR_UUID, encodeText('END'));
  state.status = await readStatus(selected.id);
  setConnection('sent', 'Astrolabe saved the hotspot settings');
}

function inputValue(id: string): string {
  return document.querySelector<HTMLInputElement>(`#${id}`)?.value.trim() ?? '';
}

function checkedValue(id: string): boolean {
  return document.querySelector<HTMLInputElement>(`#${id}`)?.checked ?? false;
}

function statusLabel(): string {
  switch (state.connection) {
    case 'scanning':
      return 'Scanning';
    case 'connecting':
      return 'Connecting';
    case 'connected':
      return 'Connected';
    case 'sending':
      return 'Sending';
    case 'sent':
      return 'Saved';
    case 'error':
      return 'Needs attention';
    case 'idle':
      return 'Ready';
  }
}

function iconForStatus(): string {
  switch (state.connection) {
    case 'connected':
    case 'sent':
      return 'check-circle-2';
    case 'error':
      return 'alert-circle';
    case 'scanning':
    case 'connecting':
    case 'sending':
      return 'loader-circle';
    case 'idle':
      return 'bluetooth';
  }
}

function renderDevices(): string {
  if (state.scanning && state.devices.length === 0) {
    return '<div class="empty">Looking for Astrolabe Faculty...</div>';
  }
  if (state.devices.length === 0) {
    return '<div class="empty">No devices yet</div>';
  }
  return state.devices
    .map((device) => {
      const selected = state.selected?.id === device.id;
      return `
        <button class="device-row ${selected ? 'selected' : ''}" data-connect="${escapeHtml(device.id)}">
          <span>
            <strong>${escapeHtml(device.name)}</strong>
            <small>${escapeHtml(device.id)}</small>
          </span>
          <span class="rssi">${device.rssi ?? '--'} dBm</span>
        </button>
      `;
    })
    .join('');
}

function renderRemoteStatus(): string {
  const status = state.status;
  if (!status) {
    return '<span class="muted">No watch status yet</span>';
  }
  const wifi = status.wifi?.ssid ? escapeHtml(status.wifi.ssid) : 'not set';
  const mode = status.wifi?.ap ? 'setup AP' : 'station';
  return `
    <span>${wifi}</span>
    <span>${mode}</span>
    <span>${status.tz ? escapeHtml(status.tz) : 'time zone unknown'}</span>
  `;
}

function render() {
  const busy = ['scanning', 'connecting', 'sending'].includes(state.connection);
  const connected = Boolean(state.selected);
  app.innerHTML = `
    <main class="shell">
      <section class="topbar">
        <div class="brand">
          <div class="mark"><i data-lucide="orbit"></i></div>
          <div>
            <h1>Astrolabe Link</h1>
            <p>BLE hotspot provisioning</p>
          </div>
        </div>
        <div class="state ${state.connection}">
          <i data-lucide="${iconForStatus()}"></i>
          <span>${statusLabel()}</span>
        </div>
      </section>

      <section class="hero">
        <div class="hotspot-panel">
          <div class="panel-title">
            <i data-lucide="wifi"></i>
            <h2>iPhone Hotspot</h2>
          </div>
          <label>
            <span>Network Name</span>
            <input id="ssid" autocomplete="off" placeholder="Daniel's iPhone" />
          </label>
          <label>
            <span>Password</span>
            <input id="password" autocomplete="off" type="password" placeholder="Hotspot password" />
          </label>
          <label class="check">
            <input id="include-clock" type="checkbox" checked />
            <span>Sync clock and time zone</span>
          </label>
          <button id="send" class="primary" ${!connected || busy ? 'disabled' : ''}>
            <i data-lucide="send"></i>
            <span>Send to Astrolabe</span>
          </button>
        </div>

        <div class="device-panel">
          <div class="panel-title">
            <i data-lucide="bluetooth"></i>
            <h2>Astrolabe</h2>
          </div>
          <div class="actions">
            <button id="scan" class="icon-button" title="Scan" ${busy ? 'disabled' : ''}>
              <i data-lucide="refresh-cw"></i>
            </button>
            <button id="stop" class="icon-button" title="Stop scan" ${state.scanning ? '' : 'disabled'}>
              <i data-lucide="circle-stop"></i>
            </button>
          </div>
          <div class="devices">${renderDevices()}</div>
          <div class="watch-status">${renderRemoteStatus()}</div>
        </div>
      </section>

      <section class="notes">
        <div>
          <i data-lucide="settings"></i>
          <p>Enable Personal Hotspot in iOS Settings, then use that network name and password here.</p>
        </div>
        <div class="log">
          ${state.log.map((entry) => `<span>${escapeHtml(entry)}</span>`).join('')}
        </div>
      </section>
    </main>
  `;

  bindEvents();
  createIcons({ icons });
}

function bindEvents() {
  document.querySelector<HTMLButtonElement>('#scan')?.addEventListener('click', () => {
    void scan().catch(handleError);
  });
  document.querySelector<HTMLButtonElement>('#stop')?.addEventListener('click', () => {
    void stopScan().catch(handleError);
  });
  document.querySelector<HTMLButtonElement>('#send')?.addEventListener('click', () => {
    void sendProvisioning().catch(handleError);
  });
  document.querySelectorAll<HTMLButtonElement>('[data-connect]').forEach((button) => {
    button.addEventListener('click', () => {
      const id = button.dataset.connect;
      const device = state.devices.find((candidate) => candidate.id === id);
      if (device) {
        void connect(device).catch(handleError);
      }
    });
  });
}

function handleError(error: unknown) {
  const message = error instanceof Error ? error.message : 'Unexpected BLE error';
  setConnection('error', message);
}

render();
