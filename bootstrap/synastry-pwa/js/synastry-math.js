import { BODY_COUNT } from "./bodies.js";
import { BODIES } from "./bodies.js";
import { norm360, zodiacAbbr } from "./zodiac.js";

const MAJORS = [0, 60, 90, 120, 180];

function aspectDistance(a, b) {
  let d = Math.abs(norm360(a) - norm360(b));
  if (d > 180) d = 360 - d;
  return d;
}

function aspectLabel(deg) {
  switch (deg) {
    case 0:
      return "conj";
    case 60:
      return "sextile";
    case 90:
      return "square";
    case 120:
      return "trine";
    case 180:
      return "opp";
    default:
      return "aspect";
  }
}

export function aspectColor(deg) {
  switch (deg) {
    case 0:
      return "#ffe696";
    case 60:
      return "#82dcff";
    case 90:
      return "#ff786e";
    case 120:
      return "#91ebaf";
    case 180:
      return "#d296ff";
    default:
      return "#aab4c8";
  }
}

export function rebuildAspects(userLon, targetLon, maxAspects = 10) {
  const aspects = [];
  for (let ui = 0; ui < BODY_COUNT; ui++) {
    for (let ti = 0; ti < BODY_COUNT; ti++) {
      const sep = aspectDistance(userLon[ui], targetLon[ti]);
      for (const deg of MAJORS) {
        const orb = Math.abs(sep - deg);
        if (orb > 4.5) continue;
        aspects.push({ userBody: ui, targetBody: ti, aspectDeg: deg, orb, label: aspectLabel(deg) });
        break;
      }
    }
  }
  aspects.sort((a, b) => a.orb - b.orb);
  return aspects.slice(0, maxAspects);
}

export function bodyLabel(idx) {
  return BODIES[idx]?.label || "?";
}

export function buildVoiceSnapshot(userBirth, targetProfile, userPos, targetPos, aspects) {
  const role = targetProfile.role === "child" ? "child" : "partner";
  let msg =
    `Pocket Mynah synastry snapshot. Positions use Swiss Ephemeris in the browser. ` +
    `User birth: ${userBirth.year}-${String(userBirth.month).padStart(2, "0")}-${String(userBirth.day).padStart(2, "0")} ` +
    `${String(userBirth.hour).padStart(2, "0")}:${String(userBirth.minute).padStart(2, "0")} local at ${userBirth.place || "unknown place"}. ` +
    `Target: ${targetProfile.name} (${role}), ${targetProfile.year}-${String(targetProfile.month).padStart(2, "0")}-${String(targetProfile.day).padStart(2, "0")} ` +
    `${String(targetProfile.hour).padStart(2, "0")}:${String(targetProfile.minute).padStart(2, "0")} local at ${targetProfile.place || "unknown place"}. `;
  msg += "User longitudes: ";
  for (let i = 0; i < BODY_COUNT; i++) {
    msg += `${bodyLabel(i)} ${userPos.lon[i].toFixed(1)} ${zodiacAbbr(userPos.lon[i])}; `;
  }
  msg += "Target longitudes: ";
  for (let i = 0; i < BODY_COUNT; i++) {
    msg += `${bodyLabel(i)} ${targetPos.lon[i].toFixed(1)} ${zodiacAbbr(targetPos.lon[i])}; `;
  }
  msg += "Closest cross-chart aspects: ";
  const n = Math.min(aspects.length, 8);
  if (n === 0) {
    msg += "none within the watch orb; ";
  } else {
    for (let i = 0; i < n; i++) {
      const a = aspects[i];
      msg += `user ${bodyLabel(a.userBody)} ${a.label} target ${bodyLabel(a.targetBody)} (orb ${a.orb.toFixed(1)} deg); `;
    }
  }
  msg += "Please deliver the spoken synastry reading now.";
  return msg;
}

export const SYNastry_VOICE_SYS =
  "You are a warm, articulate astrologer speaking aloud for a tiny round watch. Use tropical zodiac. " +
  "A synastry snapshot is provided below for the user's birth chart and one selected partner/family profile. " +
  "If the user asks a question, answer it using the chart data; if they did not ask a question, give one " +
  "flowing relationship highlight under 90 seconds spoken. Emphasize patterns, care, and agency rather than " +
  "fixed fate. Avoid medical, legal, or deterministic claims. Do not claim arc-minute precision from these " +
  "numbers. Output only words to be spoken aloud; no asterisk stage directions or emotes.";

export const SYNastry_BOOT_USER_MSG =
  "Deliver the spoken synastry relationship highlight now (one flowing mini-reading, under 90 seconds).";
