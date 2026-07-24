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
export const LUNASAY_GENERATED_DAILY_FACE_IDS = [
  "moon",
  "astrology",
  "transits",
  "synastry",
  "tarot",
  "sky",
] as const satisfies readonly LunaSayDailyFaceId[];
export type LunaSayDailyFaceMode = "daily" | "live_question" | "offline";

export type LunaSayDailyFace = {
  mode: LunaSayDailyFaceMode;
  title: string;
  headline: string;
  display: string;
  spoken: string;
  detail: string;
  /** Exact short quote from the supplied facts, retained for "why this?" UI. */
  evidence?: string;
  /** Synastry-only exact quote supporting temporary relationship weather. */
  weatherEvidence?: string;
  /** Plain-language account of why the time-sensitive pattern matters now. */
  now?: string;
  /** The next evidence-backed shift inside the device's ten-day window. */
  next?: string;
  /** Exact device-computed timing fact retained for a "why now?" view. */
  temporalEvidence?: string;
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
const EVIDENCE_CONTRACT: Record<LunaSayDailyFaceId, string> = {
  moon: "Lunar phase estimate",
  astrology: "Primary natal chart",
  transits:
    "Tight current-to-natal aspects, No current-to-natal major aspect, or Current transit positions are unavailable",
  synastry:
    "The selected relationship, relationship between, or Tight major aspects",
  tarot: "Visible tarot card",
  alethiometer: "",
  sky: "Local time or Current sky positions",
  journal: "",
  conversation: "",
};
const GENERIC_READING_PHRASES = [
  "my dear",
  "trust your intuition",
  "trust in the journey",
  "finding peace",
  "find peace",
  "beautifully aligned",
  "wonderful time",
  "inner peace",
  "embrace this",
  "the universe wants",
  "gentle reminder",
  "moment of peace",
  "intentions are gaining",
] as const;

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
  const temporal = id === "transits" || id === "synastry";
  const generated = LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
    id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
  );
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
            maxLength: 280,
            description:
              "One reciprocal durable natal tendency in plain language. Do not say today, always, compatible, destined, or make one person the problem.",
          },
          weather: {
            type: "string",
            maxLength: 220,
            description:
              "Only temporary relationship context supported by current transit or wellness facts. If none is supplied, explicitly let lived experience lead.",
          },
          practice: {
            type: "string",
            maxLength: 220,
            description:
              "One specific, consent-respecting care or repair action an adult can choose. Never assign a child responsibility for an adult emotion.",
          },
          spoken: {
            type: "string",
            maxLength: 260,
            description:
              "A cohesive TTS script under 260 characters with exactly three short labeled sentences: Pattern:, Today:, and Practice:. Do not include Next; the server inserts device-computed timing.",
          },
        }
        : { spoken: { type: "string", maxLength: 150 } }),
      detail: { type: "string", maxLength: synastry ? 360 : 180 },
      ...(temporal
        ? {
          now: {
            type: "string",
            maxLength: 280,
            description:
              "What the supplied timing may feel like now, in plain conditional language. Do not invent an event or certainty.",
          },
          temporalEvidence: {
            type: "string",
            maxLength: 300,
            description:
              "The exact verbatim Ten-day transit arc, Current relationship transit arc, or explicit no-live-signal sentence copied from DAILY FACTS.",
          },
        }
        : {}),
      ...(generated
        ? {
          evidence: {
            type: "string",
            maxLength: 220,
            description:
              `A short verbatim quote copied from DAILY FACTS that directly supports this reading. It must include: ${
                EVIDENCE_CONTRACT[id]
              }.`,
          },
        }
        : {}),
      ...(synastry
        ? {
          weatherEvidence: {
            type: "string",
            maxLength: 220,
            description:
              "A short verbatim quote copied from DAILY FACTS supporting only today's temporary relationship weather, including an explicit no-live-signal fact when that is all that is supplied.",
          },
        }
        : {}),
      ...(tarot ? { cardName: { type: "string", maxLength: 48 } } : {}),
    },
    required: [
      "headline",
      "display",
      ...(synastry ? ["dynamic", "weather", "practice", "spoken"] : ["spoken"]),
      "detail",
      ...(temporal ? ["now", "temporalEvidence"] : []),
      ...(generated ? ["evidence"] : []),
      ...(synastry ? ["weatherEvidence"] : []),
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

function normalizedEvidence(value: string): string {
  return value.toLowerCase().replace(/\s+/g, " ").trim();
}

function exactFactStartingAt(
  facts: string,
  prefix: string,
  maxChars = 220,
): string | undefined {
  const start = facts.toLowerCase().indexOf(prefix.toLowerCase());
  if (start < 0) return undefined;
  const sentenceEnd = facts.indexOf(". ", start);
  const end = sentenceEnd >= 0 ? sentenceEnd + 1 : facts.length;
  const sentence = facts.slice(start, end).replace(/\s+/g, " ").trim();
  if (!sentence) return undefined;
  if (sentence.length <= maxChars) return sentence;
  const prefixSlice = sentence.slice(0, maxChars);
  const semicolon = prefixSlice.lastIndexOf(";");
  const word = prefixSlice.lastIndexOf(" ");
  const cut = semicolon >= Math.floor(maxChars * 0.5)
    ? semicolon
    : word >= Math.floor(maxChars * 0.5)
    ? word
    : maxChars;
  return sentence.slice(0, cut).replace(/[\s,;:]+$/, "");
}

export function lunaSayRequiredEvidence(
  id: LunaSayDailyFaceId,
  facts: string,
): string | undefined {
  const prefixes: Record<LunaSayDailyFaceId, readonly string[]> = {
    moon: ["Lunar phase estimate:"],
    astrology: [
      "Primary natal chart:",
      "Primary natal chart is not configured",
    ],
    transits: [
      "Tight current-to-natal aspects",
      "No current-to-natal major aspect",
      "Current transit positions are unavailable",
    ],
    synastry: [
      "relationship between",
      "The selected relationship",
      "Tight major aspects:",
    ],
    tarot: ["Visible tarot card"],
    alethiometer: [],
    sky: ["Local time", "Current sky positions:"],
    journal: [],
    conversation: [],
  };
  for (const prefix of prefixes[id]) {
    const evidence = exactFactStartingAt(facts, prefix);
    if (evidence) return evidence;
  }
  return undefined;
}

export function lunaSayRequiredWeatherEvidence(
  id: LunaSayDailyFaceId,
  facts: string,
): string | undefined {
  if (id !== "synastry") return undefined;
  for (
    const prefix of [
      "Family biometrics from rings and Astrolabes:",
      "Current relationship transit",
      "No live relationship signal",
      "Family biometrics:",
    ]
  ) {
    const evidence = exactFactStartingAt(facts, prefix);
    if (evidence) return evidence;
  }
  return undefined;
}

export function lunaSayRequiredTemporalEvidence(
  id: LunaSayDailyFaceId,
  facts: string,
): string | undefined {
  const prefixes = id === "transits"
    ? ["Ten-day transit arc:"]
    : id === "synastry"
    ? [
      "Current relationship transit arc:",
      "No live relationship signal appears",
      "No live relationship signal is available",
    ]
    : [];
  for (const prefix of prefixes) {
    const evidence = exactFactStartingAt(facts, prefix, 300);
    if (evidence) return evidence;
  }
  return undefined;
}

export function lunaSayFocusedFacts(
  id: LunaSayDailyFaceId,
  facts: string,
): string {
  const selected = [
    lunaSayRequiredEvidence(id, facts),
    lunaSayRequiredTemporalEvidence(id, facts),
    ...(id === "synastry"
      ? [
        exactFactStartingAt(facts, "Tight major aspects:"),
        lunaSayRequiredWeatherEvidence(id, facts),
      ]
      : []),
  ].filter((value): value is string => Boolean(value));
  return [...new Set(selected)].join("\n");
}

function evidenceMatchesFace(
  id: LunaSayDailyFaceId,
  evidence: string | undefined,
): boolean {
  if (!evidence) return false;
  const value = normalizedEvidence(evidence);
  switch (id) {
    case "moon":
      return value.includes("lunar phase estimate");
    case "astrology":
      return value.includes("primary natal chart");
    case "transits":
      return (value.includes("transiting") && value.includes("natal")) ||
        value.includes("current-to-natal") ||
        value.includes("current transit positions are unavailable");
    case "synastry":
      return value.includes("selected relationship") ||
        value.includes("relationship between") ||
        value.includes("tight major aspects");
    case "tarot":
      return value.includes("visible tarot card");
    case "sky":
      return value.includes("local time") ||
        value.includes("current sky positions");
    default:
      return true;
  }
}

function containsGenericReadingPhrase(value: string): boolean {
  const normalized = value.toLowerCase();
  return GENERIC_READING_PHRASES.some((phrase) => normalized.includes(phrase));
}

function contentMatchesFace(id: LunaSayDailyFaceId, content: string): boolean {
  const normalized = content.toLowerCase();
  if (id === "sky") {
    return !/\b(natal|transit(?:ing)?|astrolog\w*|trin(?:e|es|ing)|squar(?:e|es|ing)|sextil(?:e|es)|oppos(?:e|es|ing|ition)|conjunct(?:s|ion)?)\b/
      .test(
        normalized,
      ) && !normalized.includes("your moon");
  }
  return true;
}

function completeSpeechSentence(
  value: string | undefined,
  maxChars: number,
): string | undefined {
  if (!value) return undefined;
  const finish = (text: string) =>
    /[.!?]$/.test(text.trim()) ? text.trim() : `${text.trim()}.`;
  if (value.length <= maxChars) return finish(value);
  const sentence = value.match(/^.*?[.!?](?:\s|$)/)?.[0]?.trim();
  if (sentence && sentence.length <= maxChars) return finish(sentence);
  return undefined;
}

function composeSynastrySpoken(params: {
  modelSpoken: string | undefined;
  dynamic: string;
  weather: string;
  practice: string;
  display: string;
  next: string;
  relationshipSubject: string | undefined;
}): string {
  const nextBeat = `Next: ${params.next}`;
  const candidate = params.modelSpoken;
  if (
    candidate &&
    /^Pattern:/i.test(candidate) &&
    /\bToday:/i.test(candidate) &&
    /\bPractice:/i.test(candidate) &&
    !/\bNext:/i.test(candidate)
  ) {
    const practiceAt = candidate.search(/\bPractice:/i);
    const withNext = practiceAt >= 0
      ? `${candidate.slice(0, practiceAt).trim()} ${nextBeat} ${
        candidate.slice(practiceAt).trim()
      }`
      : `${candidate} ${nextBeat}`;
    if (new TextEncoder().encode(withNext).byteLength < 384) return withNext;
  }

  const pattern = completeSpeechSentence(params.dynamic, 100) ??
    completeSpeechSentence(params.display, 100) ??
    "This relationship pattern has more than one side.";
  const today = completeSpeechSentence(params.weather, 90) ??
    (params.relationshipSubject
      ? `${params.relationshipSubject}'s chart carries the current timing; lived experience decides how it feels.`
      : "No current signal can replace what the people involved actually feel.");
  const practice = completeSpeechSentence(params.practice, 90) ??
    "Ask what support would feel useful, then listen without fixing.";
  return `Pattern: ${pattern} Today: ${today} ${nextBeat} Practice: ${practice}`;
}

function canonicalTemporalNext(
  temporalEvidence: string | undefined,
  id: LunaSayDailyFaceId,
): string | undefined {
  if (!temporalEvidence) return undefined;
  if (/no live relationship signal/i.test(temporalEvidence)) {
    return "No sampled relationship shift is available; let lived experience lead.";
  }
  const noAspect = temporalEvidence.match(
    /no major current-to-natal aspect appears .*?\b(day \+\d+)\b/i,
  );
  if (noAspect) {
    return `No major sampled aspect appears through ${
      noAspect[1].toLowerCase()
    }.`;
  }
  const closest = temporalEvidence.match(
    /closest in the daily samples on \b(day \+\d+)\b/i,
  )?.[1]?.toLowerCase();
  const outside = temporalEvidence.match(
    /outside the 4\.5-degree window by \b(day \+\d+)\b/i,
  )?.[1]?.toLowerCase();
  const inside = temporalEvidence.match(
    /still inside the 4\.5-degree window on \b(day \+\d+)\b/i,
  )?.[1]?.toLowerCase();
  if (closest && outside) {
    return `Closest in daily samples on ${closest}; outside the active window by ${outside}.`;
  }
  if (closest && inside) {
    return `Closest in daily samples on ${closest}; still active in the ${inside} sample.`;
  }
  if (closest) return `Closest in daily samples on ${closest}.`;
  return id === "synastry"
    ? "No later sampled shift is available; let lived experience lead."
    : "No later sampled shift is available in the ten-day arc.";
}

function parseLunaSayDailyFaceValue(
  value: unknown,
  id: LunaSayDailyFaceId,
  date: string,
  expectedFacts?: string,
): LunaSayDailyFace {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`Missing LunaSay daily face: ${id}`);
  }
  const face = value as Record<string, unknown>;
  const metadata = FACE_METADATA[id];
  const headline = cleanText(face.headline, 72);
  const display = cleanText(face.display, 160);
  const modelSpoken = cleanText(face.spoken, 360);
  const dynamic = id === "synastry" ? cleanText(face.dynamic, 280) : undefined;
  const weather = id === "synastry" ? cleanText(face.weather, 220) : undefined;
  const practice = id === "synastry"
    ? cleanText(face.practice, 220)
    : undefined;
  const temporal = id === "transits" || id === "synastry";
  const detail = cleanText(face.detail, 480);
  const temporalEvidence = temporal
    ? cleanText(face.temporalEvidence, 300)
    : undefined;
  const evidence = cleanText(face.evidence, 220);
  const weatherEvidence = id === "synastry"
    ? cleanText(face.weatherEvidence, 220)
    : undefined;
  const relationshipSubject = id === "synastry"
    ? temporalEvidence?.match(
      /\b(?:conjunction|sextile|square|trine|opposition)\s+(.+?)\s+natal\b/i,
    )?.[1]?.trim()
    : undefined;
  const modelNow = temporal ? cleanText(face.now, 300) : undefined;
  const now = modelNow && relationshipSubject &&
      !normalizedEvidence(modelNow).includes(
        normalizedEvidence(relationshipSubject),
      )
    ? `${relationshipSubject}'s chart is the one touched. ${modelNow}`
    : modelNow;
  const next = temporal
    ? canonicalTemporalNext(temporalEvidence, id)
    : undefined;
  const spoken = dynamic && weather && practice
    ? composeSynastrySpoken({
      modelSpoken,
      dynamic,
      weather,
      practice,
      display: display ?? "Notice the pattern without assigning blame.",
      next: next ?? "Let lived experience lead.",
      relationshipSubject,
    })
    : modelSpoken;
  const evidenceSupported = expectedFacts === undefined ||
    (evidence !== undefined &&
      normalizedEvidence(expectedFacts).includes(normalizedEvidence(evidence)));
  const evidenceOnContract = expectedFacts === undefined ||
    evidenceMatchesFace(id, evidence);
  const weatherEvidenceSupported = expectedFacts === undefined ||
    id !== "synastry" ||
    (weatherEvidence !== undefined &&
      normalizedEvidence(expectedFacts).includes(
        normalizedEvidence(weatherEvidence),
      ));
  const weatherEvidenceOnContract = expectedFacts === undefined ||
    id !== "synastry" ||
    (weatherEvidence !== undefined &&
      (() => {
        const value = normalizedEvidence(weatherEvidence);
        return value.includes("family biometrics") ||
          value.includes("live wellness") ||
          value.includes("relationship transit") ||
          value.includes("no live");
      })());
  const temporalEvidenceSupported = expectedFacts === undefined ||
    !temporal ||
    (temporalEvidence !== undefined &&
      normalizedEvidence(expectedFacts).includes(
        normalizedEvidence(temporalEvidence),
      ));
  const temporalEvidenceOnContract = expectedFacts === undefined ||
    !temporal ||
    (temporalEvidence !== undefined &&
      (() => {
        const value = normalizedEvidence(temporalEvidence);
        return id === "transits"
          ? value.includes("ten-day transit arc")
          : value.includes("relationship transit arc") ||
            value.includes("no live relationship signal");
      })());
  const temporalLanguageSafe = !temporal ||
    !/\b(will|guaranteed|inevitable|destined|fated|certain(?:ly)?)\b/i.test(
      [now, next].filter(Boolean).join(" "),
    );
  const inventedCalendarDate = temporal &&
    [
      ...(now ?? "").matchAll(
        /\b\d{4}-\d{2}-\d{2}\b/g,
      ),
    ]
      .some((match) => !temporalEvidence?.includes(match[0]));
  const relationshipSubjectPreserved = !relationshipSubject ||
    normalizedEvidence([now, next].filter(Boolean).join(" ")).includes(
      normalizedEvidence(relationshipSubject),
    );
  const spokenIsSpecific = spoken === undefined ||
    !containsGenericReadingPhrase(spoken);
  const spokenBytes = spoken === undefined
    ? 0
    : new TextEncoder().encode(spoken).byteLength;
  const spokenMatchesFace = spoken === undefined ||
    contentMatchesFace(
      id,
      [headline, display, spoken, detail].filter(Boolean).join(" "),
    );
  if (
    !headline || !display || !spoken || !detail ||
    (temporal && (!now || !next || !temporalEvidence)) ||
    !spokenIsSpecific || !spokenMatchesFace ||
    !evidenceSupported || !evidenceOnContract || !weatherEvidenceSupported ||
    !weatherEvidenceOnContract || !temporalEvidenceSupported ||
    !temporalEvidenceOnContract || !temporalLanguageSafe ||
    inventedCalendarDate || !relationshipSubjectPreserved ||
    spokenBytes >= 384 ||
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
      spokenIsSpecific ? undefined : "generic-cliche",
      spokenMatchesFace ? undefined : "cross-face-language",
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
      temporal && !now ? `now:${textLength(face.now)}` : undefined,
      temporal && !next ? `next:${textLength(face.next)}` : undefined,
      temporalEvidenceSupported
        ? undefined
        : temporalEvidence
        ? "unsupported-temporal-evidence"
        : "missing-temporal-evidence",
      temporalEvidenceOnContract ? undefined : "wrong-temporal-evidence",
      temporalLanguageSafe ? undefined : "deterministic-temporal-language",
      inventedCalendarDate ? "invented-calendar-date" : undefined,
      relationshipSubjectPreserved
        ? undefined
        : "relationship-timing-subject-lost",
      spokenBytes < 384 ? undefined : `spoken-cache-bytes:${spokenBytes}`,
      evidenceSupported
        ? undefined
        : evidence
        ? "unsupported-evidence"
        : "missing-evidence",
      evidenceOnContract ? undefined : "wrong-face-evidence",
      weatherEvidenceSupported
        ? undefined
        : weatherEvidence
        ? "unsupported-weather-evidence"
        : "missing-weather-evidence",
      weatherEvidenceOnContract ? undefined : "wrong-weather-evidence",
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
    ...(now ? { now } : {}),
    ...(next ? { next } : {}),
    ...(temporalEvidence ? { temporalEvidence } : {}),
    ...(evidence ? { evidence } : {}),
    ...(weatherEvidence ? { weatherEvidence } : {}),
    accent: metadata.accent,
    ...(cardName ? { cardName } : {}),
  };
}

/** Validate one independently generated face without invalidating its peers. */
export function parseLunaSayDailyFace(
  text: string,
  id: LunaSayDailyFaceId,
  date: string,
  expectedFacts?: string,
): LunaSayDailyFace {
  const source = text.trim().replace(/^```(?:json)?/i, "").replace(/```$/i, "")
    .trim();
  const parsed = JSON.parse(source) as Record<string, unknown>;
  const envelope = parsed.face ?? objectValue(parsed.faces, id) ?? parsed[id] ??
    parsed;
  return parseLunaSayDailyFaceValue(envelope, id, date, expectedFacts);
}

/** Strictly validates an LLM response before it reaches a device cache. */
export function parseLunaSayDailyPacket(
  text: string,
  expected: { date: string; timezone: string; facts?: string },
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
      expected.facts,
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
    "For each generated daily face provide headline, display, spoken, detail, and evidence: one short verbatim quote copied from DAILY FACTS that directly supports the reading. For tarot also provide cardName. For synastry also provide dynamic, weather, practice, and weatherEvidence; evidence supports the lasting natal dynamic while weatherEvidence supports only temporary weather or the explicit absence of a live signal. Keep those as three distinct beats. Synastry spoken must be a cohesive script under 260 characters with exactly three short labeled sentences: Pattern:, Today:, and Practice:. Omit Next because the server inserts device-computed timing. The server supplies canonical mode, title, and accent metadata.",
    "For Transits and Synastry also provide now and temporalEvidence. temporalEvidence must copy the relevant ten-day arc sentence exactly. now explains why the pattern matters in conditional language. The server derives next directly from the device timing; do not provide or invent a next date, event, certainty, or shared relationship effect.",
    "Use short fields by default: headline <= 42 chars; display <= 80; spoken <= 150; detail <= 180. One sentence per field is normally enough.",
    "Make each face independently useful. spoken must be natural, soft, and ready for TTS; it should not mention JSON or instructions.",
    "Keep the packet coherent without making every face repeat the same sentence: choose one quiet theme supported by the facts, then let Moon, Inner Weather, Transits, Relationship Weather, Tarot, and Sky approach it through their own lens. Do not contradict a concrete fact on another face.",
    "Present astrology as Inner Weather and synastry as Relationship Weather. Their headline must be one friendly condition from Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense, chosen from the supplied chart and transit facts rather than invented mood data.",
    "For both weather faces, display gives one humane orientation and spoken explains what the condition may feel like plus one choice the person can make. detail preserves the astrological depth by naming the one or two supplied natal/transit factors that most support the metaphor. Describe a symbolic outlook, never a deterministic forecast.",
    "If a self-reported mood is supplied, honor it as present-moment first-person context. Never bend the astrology to validate it, turn it into a trait, or imply that LunaSay detected it. When mood and symbolic weather differ, name that gently as two different lenses and preserve the user's authority over their own experience.",
    "Synastry is Family Synastry, not romance with relabeled people. Use only family members and roles present in the facts. Treat the family as a reciprocal system: no person is the problem, and do not rank, compare, blame, diagnose, parentify a child, or make compatibility verdicts.",
    "For synastry only, dynamic must be under 220 characters; weather and practice must each be under 160; detail may be up to 360. dynamic names one mutual natal tendency without words like today, always, compatible, or destined. weather uses only time-specific transit or wellness facts; if those are absent, say the chart cannot know today's lived weather. practice gives one specific adult care or repair choice. Keep astrology as supporting evidence rather than leading with planet jargon.",
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
      "Translate the supplied lunar facts into a grounded daily orientation. evidence must quote the Lunar phase estimate. Do not invent a phase, sign, time, or event.",
    astrology:
      "Present astrology as Inner Weather: the person's durable natal baseline, not the day's transit report. evidence must quote the Primary natal chart fact, or the explicit fact that it is not configured. headline must be one of Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense. Name both a resource and a tension in plain language, then one concrete choice. Do not use the phrases trust your intuition, inner peace, beautifully aligned, or wonderful time. Astrology is a symbolic outlook, never a deterministic forecast.",
    transits:
      "This is the changing daily layer, distinct from Inner Weather. evidence must quote a Tight current-to-natal aspect, or the explicit fact that no such aspect or transit is available. temporalEvidence must copy the Ten-day transit arc exactly. now explains why the strongest supplied aspect matters in conditional language; never turn a daily sample into a guaranteed event or invent a calendar date. The server derives the next shift from temporalEvidence. Choose the most useful supplied transit, name both its pressure and its opening, and give one concrete action. spoken must include the current orientation. Do not use the phrases trust your intuition, inner peace, beautifully aligned, or wonderful time.",
    synastry:
      "This is Family Synastry, not romance with relabeled people. Treat the family as a reciprocal system: never rank, blame, diagnose, parentify a child, or make a compatibility verdict. headline must be one of Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense. evidence must quote the full identifying prefix plus content from The selected relationship, relationship between, or Tight major aspects; quoting only planet names is invalid. evidence supports only dynamic. dynamic names both sides of one mutual natal tendency without today, always, compatible, or destined. weatherEvidence must quote only a supplied live wellness, current relationship transit, or explicit no-live-signal fact. temporalEvidence must copy the Current relationship transit arc or explicit no-live-signal sentence exactly. now must name whose natal chart is touched and must not imply the transit automatically describes the whole relationship. The server derives the next shift from temporalEvidence. weather must not turn natal synastry into today's condition; when weatherEvidence says no signal, explicitly let lived experience lead. practice gives one specific adult care or repair choice. spoken must be a cohesive script under 260 characters with exactly three complete labeled sentences: Pattern: summarizes dynamic; Today: summarizes weather; Practice: gives the action. Do not include Next, day offsets, or fragments in spoken.",
    tarot:
      "Offer one reflective daily draw, not a prediction. evidence must quote the Visible tarot card fact. Use that supplied deterministic card and explain one concrete question or practice it opens. Do not borrow astrology or transit evidence.",
    alethiometer:
      "Offer only an inviting, day-sensitive entry line. Do not pretend a question has already been answered.",
    sky:
      "Use only supplied observable sky, solar, weather, and timing facts. evidence must quote Local time or Current sky positions. Keep this observational and distinct from the Moon face; do not use the Lunar phase estimate as evidence. Connect one concrete observation to a gentle invitation without inventing conditions.",
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
    "For evidence, copy one short, directly relevant phrase verbatim from DAILY FACTS. Never paraphrase it or invent a chart factor. The evidence is retained for a user-visible 'why this?' explanation.",
    "Use short fields: headline <= 42 characters, display <= 80, spoken <= 150, and detail <= 180 unless the schema allows more.",
    "spoken must be natural, soft, and ready for TTS. Do not mention JSON, prompts, models, or instructions.",
    `Avoid stock horoscope language, including: ${
      GENERIC_READING_PHRASES.join(", ")
    }. Prefer one concrete tension, resource, question, or action supported by the evidence.`,
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
  const daily = (
    title: string,
    accent: LunaSayDailyFace["accent"],
    headline: string,
    display: string,
    spoken: string,
    detail: string,
  ): LunaSayDailyFace => ({
    mode: "daily",
    title,
    headline,
    display,
    spoken,
    detail,
    accent,
  });
  return {
    schemaVersion: LUNASAY_DAILY_PACKET_SCHEMA_VERSION,
    date: params.date,
    timezone: params.timezone,
    generatedAt: new Date().toISOString(),
    faces: {
      moon: daily(
        "Moon",
        "moon",
        "Look again tonight",
        "Let the visible Moon be enough until the detailed reading returns.",
        "The detailed Moon reading is resting. Look again tonight and notice what is actually visible.",
        "No lunar claim is made while verified daily evidence is unavailable.",
      ),
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
      transits: {
        ...daily(
          "Transits",
          "amber",
          "No forecast loaded",
          "Keep the day open rather than filling the silence with a prediction.",
          "The detailed transit reading is unavailable. Let the day show you what is real before naming a pattern.",
          "No transit claim is made while verified daily evidence is unavailable.",
        ),
        now: "No verified timing is loaded.",
        next: "Refresh later rather than filling the gap with a prediction.",
        temporalEvidence: "Ten-day transit arc is unavailable.",
      },
      synastry: {
        mode: "daily",
        title: "Relationship Weather",
        headline: "Tender",
        display: "Notice the pattern without making one person the problem.",
        spoken:
          "Meet the family pattern with curiosity. Soften one response, name one need, and leave room for repair.",
        detail:
          "Family Synastry is relationship weather and a prompt for care, never a verdict about any person.",
        now: "No verified relationship timing is loaded.",
        next: "Let lived experience lead until a current signal is available.",
        temporalEvidence: "No live relationship signal is available.",
        accent: "rose",
      },
      tarot: {
        ...daily(
          "Tarot",
          "amber",
          "Hold one clear question",
          "Use the card as a prompt, not a prediction.",
          "Hold one clear question and meet the card as a prompt, not a prediction.",
          "The daily interpretation is unavailable; the card remains a reflective image.",
        ),
        cardName: lunaSayDailyTarotCardName(params.date),
      },
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
      sky: daily(
        "Sky",
        "blue",
        "Look outside",
        "The live sky is more trustworthy than an unavailable reading.",
        "The detailed sky reading is unavailable. Look outside and begin with what you can actually see.",
        "No sky condition is inferred while verified observational facts are unavailable.",
      ),
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
