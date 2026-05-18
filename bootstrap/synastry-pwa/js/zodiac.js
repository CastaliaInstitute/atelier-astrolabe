const SIGNS = [
  "Aries",
  "Taurus",
  "Gemini",
  "Cancer",
  "Leo",
  "Virgo",
  "Libra",
  "Scorpio",
  "Sagittarius",
  "Capricorn",
  "Aquarius",
  "Pisces",
];

const GLYPHS = ["♈", "♉", "♊", "♋", "♌", "♍", "♎", "♏", "♐", "♑", "♒", "♓"];
const ABBR = ["Ar", "Ta", "Ge", "Cn", "Le", "Vi", "Li", "Sc", "Sg", "Cp", "Aq", "Pi"];

export function norm360(lon) {
  let x = lon % 360;
  if (x < 0) x += 360;
  return x;
}

export function zodiacAbbr(lon) {
  const idx = Math.floor(norm360(lon) / 30) % 12;
  return ABBR[idx];
}

export function zodiacGlyph(lon) {
  const idx = Math.floor(norm360(lon) / 30) % 12;
  return GLYPHS[idx];
}

export function zodiacSign(lon) {
  const idx = Math.floor(norm360(lon) / 30) % 12;
  return SIGNS[idx];
}

export { GLYPHS, SIGNS };
