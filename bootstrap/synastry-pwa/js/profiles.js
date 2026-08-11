const STORAGE_KEY = "synastry.profiles.v1";

/** Demo seeds — same as pm_chart_profiles_ensure_demo_seed in firmware. */
export const DEMO_TARGETS = [
  {
    name: "Camille",
    role: "partner",
    year: 1984,
    month: 9,
    day: 23,
    hour: 12,
    minute: 0,
    latDeg: 42.9814,
    lonDeg: -70.9478,
    tzOffsetSec: -4 * 3600,
    place: "Exeter, NH",
  },
  {
    name: "Aidan",
    role: "child",
    year: 2003,
    month: 9,
    day: 12,
    hour: 12,
    minute: 0,
    latDeg: 39.6133,
    lonDeg: -105.0166,
    tzOffsetSec: -6 * 3600,
    place: "Littleton, CO",
  },
  {
    name: "Finn",
    role: "child",
    year: 2024,
    month: 4,
    day: 30,
    hour: 12,
    minute: 0,
    latDeg: 39.0917,
    lonDeg: -104.8728,
    tzOffsetSec: -6 * 3600,
    place: "Monument, CO",
  },
  {
    name: "Aleia",
    role: "child",
    year: 2025,
    month: 5,
    day: 4,
    hour: 12,
    minute: 0,
    latDeg: 39.0917,
    lonDeg: -104.8728,
    tzOffsetSec: -6 * 3600,
    place: "Monument, CO",
  },
];

export const DEMO_USER_BIRTH = {
  year: 1972,
  month: 5,
  day: 6,
  hour: 11,
  minute: 30,
  latDeg: 30.4383,
  lonDeg: -84.2807,
  tzOffsetSec: -4 * 3600,
  place: "Tallahassee, FL",
  valid: true,
};

function loadRaw() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

function saveRaw(data) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
}

export function ensureStore() {
  let data = loadRaw();
  if (!data) {
    data = {
      userBirth: { ...DEMO_USER_BIRTH },
      targets: DEMO_TARGETS.map((t) => ({ ...t })),
      activeIndex: 0,
    };
    saveRaw(data);
  } else {
    if (!data.userBirth?.valid) {
      data.userBirth = { ...DEMO_USER_BIRTH };
    }
    const names = new Set((data.targets || []).map((t) => t.name));
    for (const seed of DEMO_TARGETS) {
      if (!names.has(seed.name)) {
        data.targets.push({ ...seed });
      }
    }
    if (data.activeIndex == null || data.activeIndex >= data.targets.length) {
      data.activeIndex = 0;
    }
    saveRaw(data);
  }
  return data;
}

export function getStore() {
  return ensureStore();
}

export function getUserBirth() {
  const b = getStore().userBirth;
  return {
    ...b,
    valid: Boolean(b?.year && b?.month && b?.day),
    tzOffsetSec: b.tzOffsetSec ?? 0,
  };
}

export function setUserBirth(birth) {
  const data = getStore();
  data.userBirth = { ...birth, valid: true };
  saveRaw(data);
}

export function listTargets() {
  return getStore().targets || [];
}

export function getActiveTarget() {
  const data = getStore();
  const targets = data.targets || [];
  if (!targets.length) return null;
  const idx = Math.max(0, Math.min(data.activeIndex ?? 0, targets.length - 1));
  return targets[idx];
}

export function setActiveIndex(index) {
  const data = getStore();
  const n = data.targets.length;
  if (n <= 0) return null;
  data.activeIndex = ((index % n) + n) % n;
  saveRaw(data);
  return data.targets[data.activeIndex];
}

export function cycleTarget(delta) {
  const data = getStore();
  return setActiveIndex((data.activeIndex ?? 0) + delta);
}

export function profileToBirth(profile) {
  if (!profile) return { valid: false };
  return {
    year: profile.year,
    month: profile.month,
    day: profile.day,
    hour: profile.hour,
    minute: profile.minute,
    latDeg: profile.latDeg,
    lonDeg: profile.lonDeg,
    tzOffsetSec: profile.tzOffsetSec ?? 0,
    place: profile.place || "",
    valid: true,
  };
}
