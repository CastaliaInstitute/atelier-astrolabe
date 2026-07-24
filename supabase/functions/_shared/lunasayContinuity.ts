import {
  LUNASAY_GENERATED_DAILY_FACE_IDS,
  type LunaSayDailyFace,
  type LunaSayDailyFaceId,
} from "./lunasayDailyPacket.ts";

const DAY_MS = 86_400_000;
export const LUNASAY_CONTINUITY_MAX_AGE_DAYS = 14;

export type LunaSayPriorFace = {
  headline: string;
  action: string;
  evidenceHash: string;
};

export type LunaSayReadingMemory = {
  date: string;
  faces: Partial<Record<LunaSayDailyFaceId, LunaSayPriorFace>>;
};

function civilDayMs(value: string): number | null {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return null;
  const parsed = Date.parse(`${value}T12:00:00Z`);
  return Number.isFinite(parsed) &&
      new Date(parsed).toISOString().slice(0, 10) === value
    ? parsed
    : null;
}

function boundedString(value: unknown, max: number): string | undefined {
  if (typeof value !== "string") return undefined;
  const text = value.replace(/\s+/g, " ").trim();
  return text && text.length <= max ? text : undefined;
}

function normalized(value: string): string {
  return value.toLowerCase().replace(/[^a-z0-9']+/g, " ").trim();
}

export async function lunaSayEvidenceHash(value: string): Promise<string> {
  const digest = await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(value),
  );
  return Array.from(
    new Uint8Array(digest),
    (byte) => byte.toString(16).padStart(2, "0"),
  ).join("");
}

/**
 * Accept only a recent, device-cached reading summary. It contains no user
 * prose: the firmware extracts the prior server-generated headline, action,
 * and a SHA-256 evidence fingerprint from its rotating packet cache.
 */
export function parseLunaSayReadingMemory(
  value: unknown,
  currentDate: string,
): LunaSayReadingMemory | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  const input = value as Record<string, unknown>;
  const date = boundedString(input.date, 10);
  const currentMs = civilDayMs(currentDate);
  if (!date || currentMs === null) return null;
  const priorMs = civilDayMs(date);
  if (priorMs === null) return null;
  const ageDays = (currentMs - priorMs) / DAY_MS;
  if (
    !Number.isInteger(ageDays) || ageDays < 1 ||
    ageDays > LUNASAY_CONTINUITY_MAX_AGE_DAYS
  ) {
    return null;
  }
  if (
    !input.faces || typeof input.faces !== "object" ||
    Array.isArray(input.faces)
  ) {
    return null;
  }
  const rawFaces = input.faces as Record<string, unknown>;
  const faces: LunaSayReadingMemory["faces"] = {};
  for (const id of LUNASAY_GENERATED_DAILY_FACE_IDS) {
    const raw = rawFaces[id];
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) continue;
    const face = raw as Record<string, unknown>;
    const headline = boundedString(face.headline, 72);
    const action = boundedString(face.action, 120);
    const evidenceHash = boundedString(face.evidenceHash, 64);
    if (
      headline && action && evidenceHash &&
      /^[0-9a-f]{64}$/i.test(evidenceHash)
    ) {
      faces[id] = {
        headline,
        action,
        evidenceHash: evidenceHash.toLowerCase(),
      };
    }
  }
  return Object.keys(faces).length ? { date, faces } : null;
}

export async function lunaSayContinuityInstruction(
  face: LunaSayDailyFaceId,
  memory: LunaSayReadingMemory | null,
  currentEvidence: string | undefined,
): Promise<string> {
  const prior = memory?.faces[face];
  if (!memory || !prior || !currentEvidence) return "";
  const evidenceChanged =
    prior.evidenceHash !== await lunaSayEvidenceHash(currentEvidence);
  return [
    `Private device-owned continuity from ${memory.date}; this is prior model output, not a new fact about the user.`,
    `Prior headline: ${JSON.stringify(prior.headline)}.`,
    `Prior action: ${JSON.stringify(prior.action)}.`,
    "The device supplied only an evidence fingerprint; prior evidence text is not available and must not be reconstructed or guessed.",
    "Never treat continuity as support for today's reading and never mention memory, storage, feedback, personalization, or fingerprints.",
    "Do not repeat the prior action verbatim; offer a meaningfully different bounded practice.",
    evidenceChanged
      ? "Today's server-selected evidence differs. You may name a shift only by comparing the supplied evidence; do not invent a lived event."
      : "Today's server-selected evidence is unchanged. Use steady or continuing language rather than inventing a shift.",
  ].join(" ");
}

/** Return a retry-safe diagnostic when a candidate repeats the prior practice. */
export function lunaSayContinuityIssue(
  candidate: LunaSayDailyFace,
  prior: LunaSayPriorFace | undefined,
): string | null {
  if (!prior || !candidate.action) return null;
  return normalized(candidate.action) === normalized(prior.action)
    ? "repeated-prior-action"
    : null;
}
