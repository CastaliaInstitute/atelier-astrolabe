export const LUNASAY_RESEARCH_CONSENT_VERSION = "research-v2";

const MOODS = new Set([
  "calm",
  "bright",
  "tender",
  "low",
  "tense",
  "energized",
]);
const SOURCES = new Set(["device-face", "pwa"]);

export type ParsedLunaSayMoodEvent = {
  mood: string;
  arousal: number;
  valence: number;
  source: string;
  occurredAt: string;
  consentVersion: string;
};

export function parseLunaSayMoodEvent(
  body: Record<string, unknown>,
  nowMs = Date.now(),
): ParsedLunaSayMoodEvent | null {
  const mood = typeof body.mood === "string"
    ? body.mood.trim().toLowerCase()
    : "";
  const consentVersion = typeof body.consentVersion === "string"
    ? body.consentVersion.trim()
    : "";
  const source = typeof body.source === "string"
    ? body.source.trim().toLowerCase()
    : "";
  const occurredAt = typeof body.occurredAt === "string" &&
      body.occurredAt.trim()
    ? body.occurredAt.trim()
    : undefined;
  const occurredMs = occurredAt ? Date.parse(occurredAt) : nowMs;
  const arousal = Number(body.arousal);
  const valence = Number(body.valence);
  if (
    body.consent !== true || !MOODS.has(mood) ||
    consentVersion !== LUNASAY_RESEARCH_CONSENT_VERSION ||
    !SOURCES.has(source) || !Number.isFinite(occurredMs) ||
    occurredMs < Date.UTC(2024, 0, 1) ||
    occurredMs > nowMs + 24 * 60 * 60 * 1000 ||
    !Number.isFinite(arousal) || !Number.isFinite(valence) ||
    arousal < 0 || arousal > 100 || valence < 0 || valence > 100
  ) {
    return null;
  }
  return {
    mood,
    arousal: Math.round(arousal),
    valence: Math.round(valence),
    source,
    occurredAt: new Date(occurredMs).toISOString(),
    consentVersion,
  };
}
