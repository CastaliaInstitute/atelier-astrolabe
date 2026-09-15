import {
  LUNASAY_GENERATED_DAILY_FACE_IDS,
  type LunaSayDailyFaceId,
} from "./lunasayDailyPacket.ts";

export const LUNASAY_RESONANCE_MAX_COUNT = 24;
export const LUNASAY_RESONANCE_MIN_SAMPLES = 2;

export type LunaSayFaceResonance = {
  helpful: number;
  mixed: number;
  missed: number;
};

export type LunaSayResonanceProfile = Partial<
  Record<LunaSayDailyFaceId, LunaSayFaceResonance>
>;

function boundedCount(value: unknown): number | null {
  const count = Number(value);
  return Number.isInteger(count) &&
      count >= 0 &&
      count <= LUNASAY_RESONANCE_MAX_COUNT
    ? count
    : null;
}

/**
 * Accept only a small, structured preference summary. This is operational
 * personalization context, never evidence about the user's life or chart.
 */
export function parseLunaSayResonanceProfile(
  value: unknown,
): LunaSayResonanceProfile {
  if (!value || typeof value !== "object" || Array.isArray(value)) return {};
  const input = value as Record<string, unknown>;
  const profile: LunaSayResonanceProfile = {};
  for (const face of LUNASAY_GENERATED_DAILY_FACE_IDS) {
    const raw = input[face];
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) continue;
    const counts = raw as Record<string, unknown>;
    const helpful = boundedCount(counts.helpful);
    const mixed = boundedCount(counts.mixed);
    const missed = boundedCount(counts.missed);
    if (helpful === null || mixed === null || missed === null) continue;
    if (helpful + mixed + missed === 0) continue;
    profile[face] = { helpful, mixed, missed };
  }
  return profile;
}

/**
 * Turn ratings into conservative writing calibration. Feedback may adjust
 * specificity and humility, but it may never override the evidence contract
 * or become a claim about mood, personality, or future events.
 */
export function lunaSayResonanceInstruction(
  face: LunaSayDailyFaceId,
  profile: LunaSayResonanceProfile,
): string {
  const signal = profile[face];
  if (!signal) return "";
  const total = signal.helpful + signal.mixed + signal.missed;
  if (total < LUNASAY_RESONANCE_MIN_SAMPLES) return "";
  const prefix =
    `Private reading feedback for this face only: ${signal.helpful} helpful, ${signal.mixed} mixed, ${signal.missed} missed. ` +
    "Use this only to calibrate writing. Never mention feedback, infer a life fact from it, weaken the supplied evidence contract, or claim the rating proves astrology.";
  if (signal.missed >= signal.helpful && signal.missed >= signal.mixed) {
    return `${prefix} Recent responses have missed more often: be especially concrete and modest, distinguish supplied evidence from reflection, avoid assumptions, and end with one observable question or practical choice.`;
  }
  if (signal.mixed >= signal.helpful && signal.mixed >= signal.missed) {
    return `${prefix} Responses have been mixed: lead with the concrete evidence, make uncertainty visible, and offer one specific useful choice instead of broad encouragement.`;
  }
  return `${prefix} Helpful responses predominate: preserve the current balance of evidence, plain language, and one concrete choice without becoming more certain or repetitive.`;
}
