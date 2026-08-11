import { BODIES } from "./bodies.js";
import { norm360 } from "./zodiac.js";

let sweInstance = null;
let initPromise = null;

export function isEphemerisReady() {
  return sweInstance != null;
}

export async function initEphemeris(onProgress) {
  if (sweInstance) return sweInstance;
  if (initPromise) return initPromise;
  initPromise = (async () => {
    onProgress?.("Loading Swiss Ephemeris…");
    const { default: SwissEph } = await import("https://esm.sh/swisseph-wasm@0.0.5");
    const swe = new SwissEph();
    await swe.initSwissEph();
    sweInstance = swe;
    onProgress?.(null);
    return swe;
  })();
  return initPromise;
}

export function birthToUtcDate(birth) {
  const civil = Date.UTC(
    birth.year,
    birth.month - 1,
    birth.day,
    birth.hour,
    birth.minute,
    0,
  );
  return new Date(civil - (birth.tzOffsetSec || 0) * 1000);
}

export function computeBirthPositions(birth) {
  if (!sweInstance || !birth?.valid) {
    return { ok: false, lon: [] };
  }
  const utc = birthToUtcDate(birth);
  const ut =
    utc.getUTCHours() +
    utc.getUTCMinutes() / 60 +
    utc.getUTCSeconds() / 3600;
  const jd = sweInstance.julday(
    utc.getUTCFullYear(),
    utc.getUTCMonth() + 1,
    utc.getUTCDate(),
    ut,
  );
  const flag = sweInstance.SEFLG_SWIEPH | sweInstance.SEFLG_SPEED;
  const lon = [];
  for (const body of BODIES) {
    const raw = sweInstance.calc_ut(jd, sweInstance[body.swe], flag);
    lon.push(norm360(raw[0]));
  }
  return { ok: true, lon };
}
