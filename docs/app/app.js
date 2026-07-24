const SERVICE = '01000000-5017-0065-6261-6c6f72747341';
const SETTINGS = '03000000-5017-0065-6261-6c6f72747341';
const STATE = '04000000-5017-0065-6261-6c6f72747341';
const CHUNK = 80;
const CONSENT_VERSION = 'research-v1';

let characteristic;
let stateCharacteristic;
let deviceStatus = {};

const $ = selector => document.querySelector(selector);
const status = $('#connection');
const setStatus = text => { status.textContent = text; };

function saveDraft() {
  const form = $('#personal-form');
  localStorage.setItem(
    'lunasay.personal',
    JSON.stringify(Object.fromEntries(new FormData(form))),
  );
}

function loadDraft() {
  const data = JSON.parse(localStorage.getItem('lunasay.personal') || '{}');
  for (const [key, value] of Object.entries(data)) {
    const input = $(`#personal-form [name="${key}"]`);
    if (input) input.value = value;
  }
  $('#tz').value = Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
}

async function writeChunk(bytes) {
  if (typeof characteristic.writeValueWithResponse === 'function') {
    return characteristic.writeValueWithResponse(bytes);
  }
  return characteristic.writeValue(bytes);
}

async function write(value) {
  const bytes = new TextEncoder().encode(JSON.stringify(value));
  await writeChunk(new TextEncoder().encode('BEGIN'));
  for (let offset = 0; offset < bytes.length; offset += CHUNK) {
    await writeChunk(bytes.slice(offset, offset + CHUNK));
  }
  await writeChunk(new TextEncoder().encode('END'));
}

async function read() {
  const raw = await characteristic.readValue();
  deviceStatus = JSON.parse(new TextDecoder().decode(raw));
  if (stateCharacteristic) {
    const stateRaw = await stateCharacteristic.readValue();
    Object.assign(
      deviceStatus,
      JSON.parse(new TextDecoder().decode(stateRaw)),
    );
  }
  render();
  return deviceStatus;
}

async function ensureConnected() {
  if (!characteristic) await connect();
}

async function connect() {
  if (!navigator.bluetooth) {
    throw Error('Web Bluetooth needs Chrome or another compatible browser.');
  }
  setStatus('Choose LunaSay in the Bluetooth picker…');
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [SERVICE] }],
    optionalServices: [SERVICE],
  });
  device.addEventListener('gattserverdisconnected', () => {
    characteristic = null;
    stateCharacteristic = null;
    setStatus('Disconnected');
  });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE);
  characteristic = await service.getCharacteristic(SETTINGS);
  stateCharacteristic = await service.getCharacteristic(STATE).catch(() => null);
  await read();
  setStatus(`Connected to ${device.name || 'LunaSay'} · settings save to the device.`);
}

function renderMood() {
  const mood = deviceStatus.mood || {};
  const selected = String(mood.label || '').toLowerCase();
  document.querySelectorAll('.mood').forEach(button => {
    button.classList.toggle('active', button.dataset.mood === selected);
    button.setAttribute(
      'aria-pressed',
      button.dataset.mood === selected ? 'true' : 'false',
    );
  });
  $('#mood-note').textContent = selected
    ? `${mood.label} is selected on LunaSay. Choosing another mood records a deliberate check-in.`
    : 'Connect to choose the same mood shown on LunaSay.';
}

function renderResearch() {
  const research = deviceStatus.research || {};
  const consent = research.consent === true;
  $('#research-consent').checked = consent;
  const state = research.status || (consent ? 'ready' : 'local only');
  const pending = research.pending ? ' One check-in is safely queued on the device.' : '';
  $('#research-status').textContent = consent
    ? `Research sharing is on · ${state}.${pending}`
    : 'Research sharing is off · mood check-ins stay on LunaSay.';
}

function render() {
  const cycle = deviceStatus.cycle || {};
  const ring = deviceStatus.ring || {};
  $('#cycle-day').textContent = cycle.configured
    ? `DAY ${String(cycle.day).padStart(2, '0')}`
    : '—';
  $('#cycle-total').textContent = cycle.configured
    ? `of ${cycle.length} days`
    : 'set a start date';
  $('#cycle-phase').textContent = cycle.configured
    ? cycle.phase
    : 'Your rhythm, on your terms.';
  $('#cycle-summary').textContent = cycle.configured
    ? `Cycle start: ${cycle.startDate}. You can correct this at any time.`
    : 'Connect to LunaSay to see or save a private cycle estimate.';
  $('#hr').textContent = ring.heartRate || '—';
  $('#hrv').textContent = ring.hrv || '—';
  $('#spo2').textContent = ring.spo2 || '—';
  if (cycle.startDate) $('#cycle-form [name=startDate]').value = cycle.startDate;
  if (cycle.length) $('#cycle-form [name=length]').value = cycle.length;
  if (cycle.periodLength) {
    $('#cycle-form [name=periodLength]').value = cycle.periodLength;
  }
  renderMood();
  renderResearch();
}

async function sendAndRefresh(payload, message) {
  await ensureConnected();
  await write(payload);
  await read();
  setStatus(message);
}

async function sendCycle(payload, message) {
  return sendAndRefresh({ cycle: payload }, message);
}

document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab,.page').forEach(element => {
      element.classList.remove('active');
    });
    tab.classList.add('active');
    $(`#${tab.dataset.page}`).classList.add('active');
  });
});

$('#connect').addEventListener('click', () => {
  connect().catch(error => setStatus(error.message));
});
$('#connect-secondary').addEventListener('click', () => {
  connect().catch(error => setStatus(error.message));
});

document.querySelectorAll('.mood').forEach(button => {
  button.addEventListener('click', () => {
    const label = button.dataset.mood;
    sendAndRefresh(
      { mood: { label, checkIn: true } },
      `${button.textContent.trim()} check-in saved to LunaSay.`,
    ).catch(error => setStatus(error.message));
  });
});

$('#research-consent').addEventListener('change', event => {
  const consent = event.currentTarget.checked;
  if (consent && !confirm(
    'Share future mood check-ins for LunaSay research? No names, birth data, family data, journals, biometrics, or location are included.',
  )) {
    event.currentTarget.checked = false;
    return;
  }
  sendAndRefresh(
    { research: { consent, consentVersion: CONSENT_VERSION } },
    consent
      ? 'Research sharing enabled. Only future deliberate mood check-ins may export.'
      : 'Research sharing disabled. Any unsent check-in was deleted from LunaSay.',
  ).catch(error => {
    event.currentTarget.checked = !consent;
    setStatus(error.message);
  });
});

$('#personal-form').addEventListener('submit', event => {
  event.preventDefault();
  saveDraft();
  const personal = Object.fromEntries(new FormData(event.currentTarget));
  sendAndRefresh(
    { personal },
    'Personal context saved privately to LunaSay.',
  ).catch(() => {
    setStatus('Personal draft saved in this browser. Connect LunaSay to save it there too.');
  });
});

$('#cycle-form').addEventListener('submit', event => {
  event.preventDefault();
  const form = Object.fromEntries(new FormData(event.currentTarget));
  sendCycle({
    startDate: form.startDate,
    length: Number(form.length),
    periodLength: Number(form.periodLength),
  }, 'Cycle settings saved privately to LunaSay.').catch(error => {
    setStatus(error.message);
  });
});

$('#bleeding-start').addEventListener('click', () => {
  if (confirm('Log bleeding as starting today?')) {
    sendCycle(
      { bleedingStarted: true },
      'Bleeding start logged as cycle day 1.',
    ).catch(error => setStatus(error.message));
  }
});

$('#bleeding-stop').addEventListener('click', () => {
  if (confirm('Log bleeding as stopping today?')) {
    sendCycle(
      { bleedingStopped: true },
      'Bleeding stop logged.',
    ).catch(error => setStatus(error.message));
  }
});

$('#use-location').addEventListener('click', () => {
  navigator.geolocation?.getCurrentPosition(position => {
    $('#location').value =
      `${position.coords.latitude.toFixed(5)}, ${position.coords.longitude.toFixed(5)}`;
  }, () => setStatus('Location permission was not granted.'));
});

$('#device-form').addEventListener('submit', event => {
  event.preventDefault();
  const [lat, lon] = $('#location').value.split(',').map(Number);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    setStatus('Enter latitude, longitude or use current location.');
    return;
  }
  sendAndRefresh({
    tz: $('#tz').value,
    epoch: Math.floor(Date.now() / 1000),
    location: { lat, lon, source: 'lunasay-pwa' },
  }, 'Time and place saved to LunaSay.').catch(error => {
    setStatus(error.message);
  });
});

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('sw.js').catch(() => {});
}
loadDraft();
render();
