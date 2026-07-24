/**
 * The once-a-day LunaSay response.  It is deliberately data, not a spoken
 * briefing: the watch can render or speak an individual face later without
 * spending another LLM call (or pre-generating audio).
 */

export const LUNASAY_DAILY_PACKET_FACE = "lunasay_daily_packet";
export const LUNASAY_DAILY_PACKET_SCHEMA_VERSION = 1;

export const LUNASAY_DAILY_FACE_IDS = [
  "moon",
  "astrology",
  "transits",
  "synastry",
  "tarot",
  "alethiometer",
  "sky",
  "journal",
  "conversation",
] as const;

export type LunaSayDailyFaceId = typeof LUNASAY_DAILY_FACE_IDS[number];
export type LunaSayDailyFaceMode = "daily" | "live_question" | "offline";

export type LunaSayDailyFace = {
  mode: LunaSayDailyFaceMode;
  title: string;
  headline: string;
  display: string;
  spoken: string;
  detail: string;
  /** A simple palette hint for the face renderer, never a CSS color. */
  accent: "moon" | "violet" | "amber" | "blue" | "rose" | "silver";
  /** Tarot only. The device may use this to select an existing deck asset. */
  cardName?: string;
};

export type LunaSayDailyPacket = {
  schemaVersion: typeof LUNASAY_DAILY_PACKET_SCHEMA_VERSION;
  date: string;
  timezone: string;
  generatedAt: string;
  faces: Record<LunaSayDailyFaceId, LunaSayDailyFace>;
};

const ACCENTS = new Set<LunaSayDailyFace["accent"]>([
  "moon",
  "violet",
  "amber",
  "blue",
  "rose",
  "silver",
]);
const MODES = new Set<LunaSayDailyFaceMode>([
  "daily",
  "live_question",
  "offline",
]);
const DAILY_TAROT_CARDS = [
  "The Fool",
  "The Magician",
  "The High Priestess",
  "The Empress",
  "The Emperor",
  "The Hierophant",
  "The Lovers",
  "The Chariot",
  "Strength",
  "The Hermit",
  "Wheel of Fortune",
  "Justice",
  "The Hanged Man",
  "Death",
  "Temperance",
  "The Devil",
  "The Tower",
  "The Star",
  "The Moon",
  "The Sun",
  "Judgement",
  "The World",
];

/** Stable per civil day, so the card does not change on each refresh. */
export function lunaSayDailyTarotCardName(date: string): string {
  let hash = 2166136261;
  for (const ch of date) {
    hash ^= ch.charCodeAt(0);
    hash = Math.imul(hash, 16777619);
  }
  return DAILY_TAROT_CARDS[(hash >>> 0) % DAILY_TAROT_CARDS.length];
}

function cleanText(value: unknown, maxChars: number): string | undefined {
  if (typeof value !== "string") return undefined;
  const text = value.replace(/\s+/g, " ").trim();
  return text && text.length <= maxChars ? text : undefined;
}

/** Strictly validates an LLM response before it reaches a device cache. */
export function parseLunaSayDailyPacket(
  text: string,
  expected: { date: string; timezone: string },
): LunaSayDailyPacket {
  const source = text.trim().replace(/^```(?:json)?/i, "").replace(/```$/i, "")
    .trim();
  const parsed = JSON.parse(source) as Record<string, unknown>;
  /* The server—not the model—owns schema/date/timezone. Gemini may render a
   * local-date label differently around midnight, so accept its face payload
   * and stamp those cache keys authoritatively below. */
  /* Gemini occasionally returns the nine face ids directly at the root even
   * when instructed to nest them under `faces`. Both forms contain the same
   * safe payload; normalize before validating the individual faces. */
  const candidateFaces = parsed.faces ?? parsed;
  if (
    !candidateFaces || typeof candidateFaces !== "object"
  ) {
    throw new Error(
      `Invalid LunaSay daily packet faces envelope (type: ${
        Array.isArray(candidateFaces) ? "array" : typeof candidateFaces
      }; keys: ${Object.keys(parsed).slice(0, 12).join(",")})`,
    );
  }

  const faces = {} as Record<LunaSayDailyFaceId, LunaSayDailyFace>;
  const sourceFaces: Record<string, unknown> = Array.isArray(candidateFaces)
    ? candidateFaces.reduce<Record<string, unknown>>((result, entry) => {
      if (!entry || typeof entry !== "object" || Array.isArray(entry)) {
        return result;
      }
      const record = entry as Record<string, unknown>;
      const rawId = record.id ?? record.faceId ?? record.face ?? record.key ??
        record.faceName ?? record.name;
      const id = typeof rawId === "string"
        ? rawId.trim().toLowerCase().replace(/\s+face$/, "")
        : "";
      if (LUNASAY_DAILY_FACE_IDS.includes(id as LunaSayDailyFaceId)) {
        result[id] = record;
      }
      return result;
    }, {})
    : candidateFaces as Record<string, unknown>;
  for (const id of LUNASAY_DAILY_FACE_IDS) {
    const value = sourceFaces[id];
    if (!value || typeof value !== "object" || Array.isArray(value)) {
      throw new Error(`Missing LunaSay daily face: ${id}`);
    }
    const face = value as Record<string, unknown>;
    const mode = face.mode;
    const title = cleanText(face.title, 24);
    const headline = cleanText(face.headline, 72);
    const display = cleanText(face.display, 160);
    const spoken = cleanText(face.spoken, 360);
    const detail = cleanText(face.detail, 480);
    const accent = face.accent;
    if (
      typeof mode !== "string" || !MODES.has(mode as LunaSayDailyFaceMode) ||
      !title || !headline || !display || !spoken || !detail ||
      typeof accent !== "string" ||
      !ACCENTS.has(accent as LunaSayDailyFace["accent"])
    ) {
      throw new Error(`Invalid LunaSay daily face: ${id}`);
    }
    /* The card is selected deterministically by date, so a model omission
     * cannot turn an otherwise useful packet into a failed refresh. */
    const cardName = cleanText(face.cardName, 48) ??
      (id === "tarot" ? lunaSayDailyTarotCardName(expected.date) : undefined);
    faces[id] = {
      mode: mode as LunaSayDailyFaceMode,
      title,
      headline,
      display,
      spoken,
      detail,
      accent: accent as LunaSayDailyFace["accent"],
      ...(cardName ? { cardName } : {}),
    };
  }
  return {
    schemaVersion: LUNASAY_DAILY_PACKET_SCHEMA_VERSION,
    date: expected.date,
    timezone: expected.timezone,
    generatedAt: new Date().toISOString(),
    faces,
  };
}

export function lunaSayDateForEpoch(
  epochSeconds: number,
  timezone: string,
): string {
  try {
    const parts = new Intl.DateTimeFormat("en-CA", {
      timeZone: timezone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
    }).formatToParts(new Date(epochSeconds * 1000));
    const value = (kind: string) =>
      parts.find((part) => part.type === kind)?.value;
    const year = value("year");
    const month = value("month");
    const day = value("day");
    if (year && month && day) return `${year}-${month}-${day}`;
  } catch {
    // A device may not know its IANA zone yet; UTC remains deterministic.
  }
  return new Date(epochSeconds * 1000).toISOString().slice(0, 10);
}

/** Accept only a usable IANA timezone before reflecting it in a response header. */
export function normalizeLunaSayTimezone(value: unknown): string {
  const timezone = typeof value === "string" ? value.trim() : "";
  if (!timezone || timezone.length > 64 || /[\r\n]/.test(timezone)) {
    return "UTC";
  }
  try {
    new Intl.DateTimeFormat("en-US", { timeZone: timezone }).format();
    return timezone;
  } catch {
    return "UTC";
  }
}

export function buildLunaSayDailyPacketInstruction(params: {
  date: string;
  timezone: string;
}): string {
  return [
    "You create the daily LunaSay face packet for a small round lunar companion.",
    "Use only supplied facts. Never claim certainty, destiny, medical advice, or events not in the facts.",
    "Return one strict JSON object only: no markdown, no prose outside JSON.",
    `Set schemaVersion to ${LUNASAY_DAILY_PACKET_SCHEMA_VERSION}, date to ${
      JSON.stringify(params.date)
    }, and timezone to ${JSON.stringify(params.timezone)}.`,
    "faces must contain exactly moon, astrology, transits, synastry, tarot, alethiometer, sky, journal, and conversation.",
    "Each face needs mode, title, headline, display, spoken, detail, and accent.",
    "mode is daily for moon/astrology/transits/synastry/tarot/sky, live_question for alethiometer/conversation, and offline for journal.",
    "accent is one of moon, violet, amber, blue, rose, silver.",
    "Use short fields by default: title <= 24 chars; headline <= 42; display <= 80; spoken <= 150; detail <= 180. One sentence per field is normally enough.",
    "Make each face independently useful. spoken must be natural, soft, and ready for TTS; it should not mention JSON or instructions.",
    "Present astrology as Inner Weather and synastry as Relationship Weather. Their headline must be one friendly condition from Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense, chosen from the supplied chart and transit facts rather than invented mood data.",
    "For both weather faces, display gives one humane orientation and spoken explains what the condition may feel like plus one choice the person can make. detail preserves the astrological depth by naming the one or two supplied natal/transit factors that most support the metaphor. Describe a symbolic outlook, never a deterministic forecast.",
    "Synastry is Family Synastry, not romance with relabeled people. Use only family members and roles present in the facts. Treat the family as a reciprocal system: no person is the problem, and do not rank, compare, blame, diagnose, parentify a child, or make compatibility verdicts.",
    "For synastry only, spoken may be 150–300 characters and detail may be up to 360. Use three compact beats: name one mutual dynamic in plain language; distinguish a durable natal tendency from today's temporary relationship weather; offer one specific care or repair practice. Keep astrology as supporting evidence rather than leading with planet jargon.",
    "If wellness facts are present, translate them into privacy-preserving care context such as lower capacity, need for rest, or need for space. Never recite raw measurements or treat temporary biometrics as personality.",
    "Tarot is a single reflective daily draw: include cardName and do not call it a prediction.",
    "Alethiometer and Conversation must be an inviting day-sensitive entry line only; do not pretend they have answered a question.",
    "Journal must invite private, on-device reflection and must not claim it is saved anywhere unless facts explicitly say so.",
  ].join(" ");
}

/**
 * A network/model failure must not leave the watch without a daily entry.
 * This fallback deliberately makes no astrological claim beyond the supplied
 * device facts; it preserves the cache contract until a later refresh.
 */
export function lunaSayDailyPacketFallback(params: {
  date: string;
  timezone: string;
  facts: string;
}): LunaSayDailyPacket {
  const fact = params.facts.replace(/\s+/g, " ").trim().slice(0, 280) ||
    "Your LunaSay is ready for a quiet moment of attention.";
  const daily = (
    title: string,
    accent: LunaSayDailyFace["accent"],
  ): LunaSayDailyFace => ({
    mode: "daily",
    title,
    headline: "Today, gently held",
    display: fact.slice(0, 160),
    spoken: `${fact} Take what feels useful, and leave the rest open.`,
    detail: fact,
    accent,
  });
  return {
    schemaVersion: LUNASAY_DAILY_PACKET_SCHEMA_VERSION,
    date: params.date,
    timezone: params.timezone,
    generatedAt: new Date().toISOString(),
    faces: {
      moon: daily("Moon", "moon"),
      astrology: {
        mode: "daily",
        title: "Inner Weather",
        headline: "Shifting",
        display:
          "Stay flexible and notice what changes before choosing a direction.",
        spoken:
          "Your inner weather is shifting. Give yourself room to notice what changes, then choose one grounded next step.",
        detail:
          "A symbolic outlook needs current chart facts; this gentle fallback makes no astrological claim.",
        accent: "violet",
      },
      transits: daily("Transits", "amber"),
      synastry: {
        mode: "daily",
        title: "Relationship Weather",
        headline: "Protect the bond",
        display: "Notice the pattern without making one person the problem.",
        spoken:
          "Meet the family pattern with curiosity. Soften one response, name one need, and leave room for repair.",
        detail:
          "Family Synastry is relationship weather and a prompt for care, never a verdict about any person.",
        accent: "rose",
      },
      tarot: { ...daily("Tarot", "amber"), cardName: "The Star" },
      alethiometer: {
        mode: "live_question",
        title: "Alethiometer",
        headline: "Ask what matters",
        display: "Hold a question and let the hands seek their symbols.",
        spoken:
          "Bring one clear question. The hands are ready to search with you.",
        detail: "A live reading begins when you speak your question.",
        accent: "silver",
      },
      sky: daily("Sky", "blue"),
      journal: {
        mode: "offline",
        title: "Journal",
        headline: "Make a small record",
        display: "Speak or write what you want to remember.",
        spoken: "Make a small record of this moment, in your own words.",
        detail: "Journal stays a private, intentional practice.",
        accent: "moon",
      },
      conversation: {
        mode: "live_question",
        title: "Companion",
        headline: "I am listening",
        display: "Begin whenever you are ready.",
        spoken: "I am here. Begin whenever you are ready.",
        detail: "Conversation begins with your voice.",
        accent: "violet",
      },
    },
  };
}
