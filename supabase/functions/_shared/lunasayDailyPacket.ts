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

const FACE_METADATA: Record<
  LunaSayDailyFaceId,
  Pick<LunaSayDailyFace, "mode" | "title" | "accent">
> = {
  moon: { mode: "daily", title: "Moon", accent: "moon" },
  astrology: {
    mode: "daily",
    title: "Inner Weather",
    accent: "violet",
  },
  transits: { mode: "daily", title: "Transits", accent: "amber" },
  synastry: {
    mode: "daily",
    title: "Relationship Weather",
    accent: "rose",
  },
  tarot: { mode: "daily", title: "Tarot", accent: "amber" },
  alethiometer: {
    mode: "live_question",
    title: "Alethiometer",
    accent: "silver",
  },
  sky: { mode: "daily", title: "Sky", accent: "blue" },
  journal: { mode: "offline", title: "Journal", accent: "moon" },
  conversation: {
    mode: "live_question",
    title: "Companion",
    accent: "violet",
  },
};
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

const FACE_ID_ALIASES: Record<string, LunaSayDailyFaceId> = {
  "inner_weather": "astrology",
  "relationship_weather": "synastry",
  "family_synastry": "synastry",
  "moon_phase": "moon",
  "companion": "conversation",
};
const WEATHER_HEADLINES = new Set([
  "Clear",
  "Warm",
  "Shifting",
  "Inward",
  "Tender",
  "Changeable",
  "Easy",
  "Open",
  "Intense",
]);

function normalizeFaceId(value: unknown): LunaSayDailyFaceId | undefined {
  if (typeof value !== "string") return undefined;
  const id = value.trim().toLowerCase()
    .replace(/\s+face$/, "")
    .replace(/[\s-]+/g, "_");
  if (LUNASAY_DAILY_FACE_IDS.includes(id as LunaSayDailyFaceId)) {
    return id as LunaSayDailyFaceId;
  }
  return FACE_ID_ALIASES[id];
}

function objectValue(
  value: unknown,
  key: string,
): unknown {
  return value && typeof value === "object" && !Array.isArray(value)
    ? (value as Record<string, unknown>)[key]
    : undefined;
}

/**
 * Gemini structured output schema. The server still validates every field;
 * this schema prevents paid calls from falling back merely because the model
 * renamed or omitted a face key.
 */
export function lunaSayDailyFaceJsonSchema(
  id: LunaSayDailyFaceId,
): Record<string, unknown> {
  const tarot = id === "tarot";
  const synastry = id === "synastry";
  return {
    type: "object",
    additionalProperties: false,
    properties: {
      headline: { type: "string", maxLength: 42 },
      display: { type: "string", maxLength: 80 },
      ...(synastry
        ? {
          dynamic: {
            type: "string",
            maxLength: 90,
            description:
              "One reciprocal durable natal tendency in plain language. Do not say today, always, compatible, destined, or make one person the problem.",
          },
          weather: {
            type: "string",
            maxLength: 90,
            description:
              "Only temporary relationship context supported by current transit or wellness facts. If none is supplied, explicitly let lived experience lead.",
          },
          practice: {
            type: "string",
            maxLength: 90,
            description:
              "One specific, consent-respecting care or repair action an adult can choose. Never assign a child responsibility for an adult emotion.",
          },
        }
        : { spoken: { type: "string", maxLength: 150 } }),
      detail: { type: "string", maxLength: synastry ? 360 : 180 },
      ...(tarot ? { cardName: { type: "string", maxLength: 48 } } : {}),
    },
    required: [
      "headline",
      "display",
      ...(synastry ? ["dynamic", "weather", "practice"] : ["spoken"]),
      "detail",
      ...(tarot ? ["cardName"] : []),
    ],
  };
}

export function lunaSayDailyPacketJsonSchema(): Record<string, unknown> {
  return {
    type: "object",
    additionalProperties: false,
    properties: {
      faces: {
        type: "object",
        additionalProperties: false,
        properties: Object.fromEntries(
          LUNASAY_DAILY_FACE_IDS.map((id) => [
            id,
            lunaSayDailyFaceJsonSchema(id),
          ]),
        ),
        required: [...LUNASAY_DAILY_FACE_IDS],
      },
    },
    required: ["faces"],
  };
}

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

function boundedSentence(value: unknown, maxChars: number): string | undefined {
  if (typeof value !== "string") return undefined;
  const text = value.replace(/\s+/g, " ").trim();
  if (!text) return undefined;
  if (text.length <= maxChars) return text;
  const prefix = text.slice(0, maxChars - 1);
  const punctuation = Math.max(
    prefix.lastIndexOf("."),
    prefix.lastIndexOf("!"),
    prefix.lastIndexOf("?"),
  );
  const word = prefix.lastIndexOf(" ");
  const cut = punctuation >= Math.floor(maxChars * 0.55)
    ? punctuation + 1
    : word >= Math.floor(maxChars * 0.55)
    ? word
    : maxChars - 1;
  const clipped = prefix.slice(0, cut).replace(/[\s,;:—-]+$/, "");
  return /[.!?]$/.test(clipped) ? clipped : `${clipped}.`;
}

function parseLunaSayDailyFaceValue(
  value: unknown,
  id: LunaSayDailyFaceId,
  date: string,
): LunaSayDailyFace {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`Missing LunaSay daily face: ${id}`);
  }
  const face = value as Record<string, unknown>;
  const metadata = FACE_METADATA[id];
  const headline = cleanText(face.headline, 72);
  const display = cleanText(face.display, 160);
  const modelSpoken = cleanText(face.spoken, 360);
  const dynamic = id === "synastry"
    ? boundedSentence(face.dynamic, 90)
    : undefined;
  const weather = id === "synastry"
    ? boundedSentence(face.weather, 90)
    : undefined;
  const practice = id === "synastry"
    ? boundedSentence(face.practice, 90)
    : undefined;
  const spoken = dynamic && weather && practice
    ? `The lasting pattern: ${dynamic} Today's weather: ${weather} A small practice: ${practice}`
    : modelSpoken;
  const detail = cleanText(face.detail, 480);
  if (
    !headline || !display || !spoken || !detail ||
    ((id === "astrology" || id === "synastry") &&
      !WEATHER_HEADLINES.has(headline))
  ) {
    const textLength = (candidate: unknown) =>
      typeof candidate === "string"
        ? candidate.replace(/\s+/g, " ").trim().length
        : -1;
    const problems = [
      headline ? undefined : `headline:${textLength(face.headline)}`,
      display ? undefined : `display:${textLength(face.display)}`,
      spoken ? undefined : `spoken:${textLength(face.spoken)}`,
      id === "synastry" && !dynamic
        ? `dynamic:${textLength(face.dynamic)}`
        : undefined,
      id === "synastry" && !weather
        ? `weather:${textLength(face.weather)}`
        : undefined,
      id === "synastry" && !practice
        ? `practice:${textLength(face.practice)}`
        : undefined,
      detail ? undefined : `detail:${textLength(face.detail)}`,
      (id === "astrology" || id === "synastry") &&
        headline && !WEATHER_HEADLINES.has(headline)
        ? "weather-headline"
        : undefined,
    ].filter(Boolean).join(",");
    throw new Error(`Invalid LunaSay daily face: ${id} (${problems})`);
  }
  const cardName = cleanText(face.cardName, 48) ??
    (id === "tarot" ? lunaSayDailyTarotCardName(date) : undefined);
  return {
    mode: metadata.mode,
    title: metadata.title,
    headline,
    display,
    spoken,
    detail,
    accent: metadata.accent,
    ...(cardName ? { cardName } : {}),
  };
}

/** Validate one independently generated face without invalidating its peers. */
export function parseLunaSayDailyFace(
  text: string,
  id: LunaSayDailyFaceId,
  date: string,
): LunaSayDailyFace {
  const source = text.trim().replace(/^```(?:json)?/i, "").replace(/```$/i, "")
    .trim();
  const parsed = JSON.parse(source) as Record<string, unknown>;
  const envelope = parsed.face ?? objectValue(parsed.faces, id) ?? parsed[id] ??
    parsed;
  return parseLunaSayDailyFaceValue(envelope, id, date);
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
  const packetEnvelope = parsed.packet ?? parsed.dailyPacket ??
    parsed.lunasayDailyPacket;
  const candidateFaces = parsed.faces ?? objectValue(packetEnvelope, "faces") ??
    packetEnvelope ?? parsed;
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
      const id = normalizeFaceId(rawId);
      if (id) {
        result[id] = record;
      }
      return result;
    }, {})
    : Object.entries(candidateFaces as Record<string, unknown>).reduce<
      Record<string, unknown>
    >((result, [rawId, value]) => {
      const id = normalizeFaceId(rawId) ??
        normalizeFaceId(objectValue(value, "id")) ??
        normalizeFaceId(objectValue(value, "name"));
      if (id) result[id] = value;
      return result;
    }, {});
  for (const id of LUNASAY_DAILY_FACE_IDS) {
    faces[id] = parseLunaSayDailyFaceValue(
      sourceFaces[id],
      id,
      expected.date,
    );
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
    "For each face except synastry, provide headline, display, spoken, and detail. For tarot also provide cardName. For synastry provide headline, display, dynamic, weather, practice, and detail; the server composes spoken from those three distinct beats. The server supplies canonical mode, title, and accent metadata.",
    "Use short fields by default: headline <= 42 chars; display <= 80; spoken <= 150; detail <= 180. One sentence per field is normally enough.",
    "Make each face independently useful. spoken must be natural, soft, and ready for TTS; it should not mention JSON or instructions.",
    "Keep the packet coherent without making every face repeat the same sentence: choose one quiet theme supported by the facts, then let Moon, Inner Weather, Transits, Relationship Weather, Tarot, and Sky approach it through their own lens. Do not contradict a concrete fact on another face.",
    "Present astrology as Inner Weather and synastry as Relationship Weather. Their headline must be one friendly condition from Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense, chosen from the supplied chart and transit facts rather than invented mood data.",
    "For both weather faces, display gives one humane orientation and spoken explains what the condition may feel like plus one choice the person can make. detail preserves the astrological depth by naming the one or two supplied natal/transit factors that most support the metaphor. Describe a symbolic outlook, never a deterministic forecast.",
    "If a self-reported mood is supplied, honor it as present-moment first-person context. Never bend the astrology to validate it, turn it into a trait, or imply that LunaSay detected it. When mood and symbolic weather differ, name that gently as two different lenses and preserve the user's authority over their own experience.",
    "Synastry is Family Synastry, not romance with relabeled people. Use only family members and roles present in the facts. Treat the family as a reciprocal system: no person is the problem, and do not rank, compare, blame, diagnose, parentify a child, or make compatibility verdicts.",
    "For synastry only, dynamic, weather, and practice must each be under 90 characters and detail may be up to 360. dynamic names one mutual natal tendency without words like today, always, compatible, or destined. weather uses only time-specific transit or wellness facts; if those are absent, say the chart cannot know today's lived weather. practice gives one specific adult care or repair choice. Keep astrology as supporting evidence rather than leading with planet jargon.",
    "If wellness facts are present, translate them into privacy-preserving care context such as lower capacity, need for rest, or need for space. Never recite raw measurements or treat temporary biometrics as personality.",
    "Tarot is a single reflective daily draw: include cardName and do not call it a prediction.",
    "Alethiometer and Conversation must be an inviting day-sensitive entry line only; do not pretend they have answered a question.",
    "Journal must invite private, on-device reflection and must not claim it is saved anywhere unless facts explicitly say so.",
  ].join(" ");
}

export function buildLunaSayDailyFaceInstruction(params: {
  id: LunaSayDailyFaceId;
  date: string;
  timezone: string;
}): string {
  const faceGuidance: Record<LunaSayDailyFaceId, string> = {
    moon:
      "Translate the supplied lunar facts into a grounded daily orientation. Do not invent a phase, sign, time, or event.",
    astrology:
      "Present astrology as Inner Weather. headline must be one of Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense. Explain what the supplied natal and transit factors may feel like and one choice the person can make. Astrology is a symbolic outlook, never a deterministic forecast.",
    transits:
      "Choose the one or two supplied transits most useful today. Explain their tension or invitation in plain language and include one grounded choice.",
    synastry:
      "This is Family Synastry, not romance with relabeled people. Treat the family as a reciprocal system: never rank, blame, diagnose, parentify a child, or make a compatibility verdict. headline must be one of Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense. dynamic names one mutual natal tendency without today, always, compatible, or destined. weather uses only supplied time-specific transit or wellness facts; if absent, let lived experience lead. practice gives one specific adult care or repair choice.",
    tarot:
      "Offer one reflective daily draw, not a prediction. Use the supplied deterministic card when present and explain a question or practice it opens.",
    alethiometer:
      "Offer only an inviting, day-sensitive entry line. Do not pretend a question has already been answered.",
    sky:
      "Use only supplied observable sky, solar, lunar, weather, and timing facts. Connect one concrete observation to a gentle invitation without inventing conditions.",
    journal:
      "Invite private, on-device reflection. Do not claim anything is saved or uploaded unless the facts explicitly say so.",
    conversation:
      "Offer only an inviting, day-sensitive entry line. Do not pretend a conversation has already occurred.",
  };
  return [
    `You create only the ${params.id} face for LunaSay, a small round lunar companion.`,
    `The local date is ${params.date} in ${params.timezone}.`,
    "Use only supplied facts. Never claim certainty, destiny, medical advice, or events not in the facts.",
    "Return one strict JSON object only, matching the supplied schema. No markdown and no wrapper object.",
    "Use short fields: headline <= 42 characters, display <= 80, spoken <= 150, and detail <= 180 unless the schema allows more.",
    "spoken must be natural, soft, and ready for TTS. Do not mention JSON, prompts, models, or instructions.",
    "If a self-reported mood is supplied, honor it as first-person context. Never imply LunaSay detected it or bend symbolic material to validate it.",
    "If wellness facts are present, translate them into care context such as lower capacity, rest, or space. Never recite raw measurements or turn temporary biometrics into personality.",
    faceGuidance[params.id],
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
        headline: "Tender",
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
