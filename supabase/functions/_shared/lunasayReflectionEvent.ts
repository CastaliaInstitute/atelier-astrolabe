import { LUNASAY_RESEARCH_CONSENT_VERSION } from "./lunasayMoodEvent.ts";

const FACES = new Set([
  "moon",
  "astrology",
  "transits",
  "synastry",
  "tarot",
  "sky",
]);
const RATINGS = new Set(["helpful", "mixed", "missed"]);
const SOURCES = new Set(["pwa", "device-face"]);
const DAY_MS = 24 * 60 * 60 * 1000;

export type ParsedLunaSayReflectionEvent = {
  face: string;
  rating: string;
  readingDate: string;
  source: string;
  occurredAt: string;
  consentVersion: string;
};

export function parseLunaSayReflectionEvent(
  body: Record<string, unknown>,
  nowMs = Date.now(),
): ParsedLunaSayReflectionEvent | null {
  const face = typeof body.face === "string"
    ? body.face.trim().toLowerCase()
    : "";
  const rating = typeof body.rating === "string"
    ? body.rating.trim().toLowerCase()
    : "";
  const source = typeof body.source === "string"
    ? body.source.trim().toLowerCase()
    : "";
  const consentVersion = typeof body.consentVersion === "string"
    ? body.consentVersion.trim()
    : "";
  const readingDate = typeof body.readingDate === "string"
    ? body.readingDate.trim()
    : "";
  const readingMs = /^\d{4}-\d{2}-\d{2}$/.test(readingDate)
    ? Date.parse(`${readingDate}T12:00:00Z`)
    : Number.NaN;
  const occurredAt = typeof body.occurredAt === "string" &&
      body.occurredAt.trim()
    ? body.occurredAt.trim()
    : undefined;
  const occurredMs = occurredAt ? Date.parse(occurredAt) : nowMs;
  if (
    body.event !== "reading_feedback" || body.consent !== true ||
    consentVersion !== LUNASAY_RESEARCH_CONSENT_VERSION ||
    !FACES.has(face) || !RATINGS.has(rating) || !SOURCES.has(source) ||
    !Number.isFinite(readingMs) ||
    new Date(readingMs).toISOString().slice(0, 10) !== readingDate ||
    readingMs < nowMs - 31 * DAY_MS ||
    readingMs > nowMs + DAY_MS || !Number.isFinite(occurredMs) ||
    occurredMs < Date.UTC(2024, 0, 1) || occurredMs > nowMs + DAY_MS
  ) {
    return null;
  }
  return {
    face,
    rating,
    readingDate,
    source,
    occurredAt: new Date(occurredMs).toISOString(),
    consentVersion,
  };
}
