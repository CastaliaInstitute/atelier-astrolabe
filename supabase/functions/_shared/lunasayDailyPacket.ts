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
  /**
   * One bounded practice the person can actually choose. Generated faces
   * always carry this separately even though the server also composes it into
   * spoken for current firmware.
   */
  action?: string;
  /** Exact short quote from the supplied facts, retained for "why this?" UI. */
  evidence?: string;
  /** Synastry-only exact quote supporting temporary relationship weather. */
  weatherEvidence?: string;
  /** Synastry-only, bounded account of the first person's side of the pattern. */
  perspectiveA?: string;
  /** Synastry-only, bounded account of the second person's side of the pattern. */
  perspectiveB?: string;
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
const DIRECT_ACTION_OPENING_RE =
  /^(acknowledge|ask|breathe|check|choose|compare|connect|consider|create|explore|find|focus|gaze|give|hold|honor|identify|listen|look|mark|name|notice|observe|offer|pause|place|protect|reach|reflect|release|rest|review|say|set|share|speak|step|take|tell|track|try|use|wait|watch|write)\b/i;

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
          perspectiveA: {
            type: "string",
            maxLength: 48,
            description:
              "The first named person's side of the durable pattern, under 48 characters. Begin with their exact supplied name. Use may, can, or tends to; do not blame or diagnose.",
          },
          perspectiveB: {
            type: "string",
            maxLength: 48,
            description:
              "The second named person's distinct side of the durable pattern, under 48 characters. Begin with their exact supplied name. Use may, can, or tends to; do not blame or diagnose.",
          },
          weather: {
            type: "string",
            maxLength: 220,
            description:
              "Only temporary relationship context supported by current transit or wellness facts. If none is supplied, explicitly let lived experience lead.",
          },
          practice: {
            type: "string",
            maxLength: 96,
            description:
              "One specific, consent-respecting care or repair action under 96 characters. Its first word must be Ask, Check, Choose, Give, Listen, Name, Notice, Offer, Pause, Say, Share, or Wait. Never assign a child responsibility for an adult emotion.",
          },
          spoken: {
            type: "string",
            maxLength: 260,
            description:
              "A cohesive TTS script under 260 characters with exactly three short labeled sentences: Pattern:, Today:, and Practice:. Do not include Next; the server inserts device-computed timing.",
          },
        }
        : {
          spoken: { type: "string", maxLength: 150 },
          ...(generated
            ? {
              action: {
                type: "string",
                maxLength: 96,
                description:
                  "One specific, bounded practice under 96 characters. Its first word must be Notice, Ask, Choose, Write, Pause, Look, Consider, Breathe, Compare, Name, Observe, or Wait.",
              },
            }
            : {}),
        }),
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
      ...(synastry
        ? ["perspectiveA", "perspectiveB", "weather", "practice", "spoken"]
        : ["spoken", ...(generated ? ["action"] : [])]),
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

function actionKey(value: string | undefined): string {
  return (value ?? "").toLowerCase().replace(/[^a-z0-9']+/g, " ").trim();
}

function completeSentence(value: string): string {
  const text = value.trim();
  return /[.!?]$/.test(text) ? text : `${text}.`;
}

function speechSentences(value: string): string[] {
  const matches = value.match(/[^.!?]+[.!?]+(?:["'”’])?/g);
  if (!matches?.length) return [completeSentence(value)];
  return matches.map((sentence) => sentence.trim()).filter(Boolean);
}

function boundedProse(
  value: unknown,
  maxChars: number,
): string | undefined {
  if (typeof value !== "string") return undefined;
  const text = value.replace(/\s+/g, " ").trim();
  if (!text) return undefined;
  if (text.length <= maxChars) return text;
  const kept: string[] = [];
  for (const sentence of speechSentences(text)) {
    const candidate = [...kept, sentence].join(" ");
    if (candidate.length > maxChars) break;
    kept.push(sentence);
  }
  return kept.join(" ") || undefined;
}

function composeActionSpoken(
  modelSpoken: string | undefined,
  action: string | undefined,
): string | undefined {
  if (!modelSpoken || !action) return undefined;
  const spoken = completeSentence(modelSpoken);
  const actionNormalized = actionKey(action);
  const sentences = speechSentences(spoken);
  if (
    actionKey(spoken).includes(actionNormalized) &&
    sentences.length <= 4 &&
    new TextEncoder().encode(spoken).byteLength < 384
  ) {
    return spoken;
  }
  // Keep a short interpretive prelude, then preserve the exact validated
  // action. This prevents a verbose model prelude from turning server
  // composition into a fifth sentence or overflowing the device cache.
  const prelude = sentences
    .filter((sentence) => !actionKey(sentence).includes(actionNormalized))
    .slice(0, 3)
    .join(" ");
  const composed = `${prelude ? `${prelude} ` : ""}${completeSentence(action)}`;
  return new TextEncoder().encode(composed).byteLength < 384
    ? composed
    : undefined;
}

function exactFactStartingAt(
  facts: string,
  prefix: string,
  maxChars = 220,
): string | undefined {
  const start = facts.toLowerCase().indexOf(prefix.toLowerCase());
  if (start < 0) return undefined;
  const boundaryCandidates = [
    facts.indexOf(". ", start),
    facts.indexOf("\n", start),
    facts.indexOf("\r", start),
  ].filter((index) => index >= 0);
  const boundary = boundaryCandidates.length
    ? Math.min(...boundaryCandidates)
    : -1;
  const end = boundary >= 0
    ? boundary + (facts[boundary] === "." ? 1 : 0)
    : facts.length;
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
      "Tight major aspects:",
      "relationship between",
      "The selected relationship",
    ],
    tarot: ["Visible tarot card"],
    alethiometer: [],
    sky: ["Current sky positions:", "Local time"],
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
    ...(id === "sky"
      ? [
        exactFactStartingAt(facts, "Local time"),
        exactFactStartingAt(facts, "Current sky positions:"),
      ]
      : []),
    ...(id === "synastry"
      ? [
        exactFactStartingAt(facts, "The selected relationship"),
        exactFactStartingAt(facts, "relationship between"),
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

function relationshipParticipants(
  value: string | undefined,
): [string, string] | undefined {
  if (!value) return undefined;
  const match = value.match(
    /\b(?:the selected relationship|relationship)\s+between\s+([^,.;:\n]+?)\s+and\s+([^,.;:\n]+?)\s+(?:is|has|shows|includes|:)/i,
  );
  if (!match) return undefined;
  const first = cleanText(match[1], 48);
  const second = cleanText(match[2], 48);
  return first && second ? [first, second] : undefined;
}

const RELATIONSHIP_BLAME_RE =
  /\b(?:causes?|forces?|makes?|provokes?|triggers?|at fault|to blame|is the problem|too (?:demanding|emotional|much|sensitive))\b/i;

function perspectiveTerms(value: string, participant: string): Set<string> {
  const participantTerms = new Set(
    normalizedEvidence(participant).match(/[a-z][a-z'-]{1,}/g) ?? [],
  );
  const structural = new Set([
    "can",
    "may",
    "tend",
    "tends",
    "the",
    "their",
    "through",
    "to",
    "with",
  ]);
  return new Set(
    normalizedEvidence(value).match(/[a-z][a-z'-]{2,}/g)
      ?.filter((term) =>
        !participantTerms.has(term) && !structural.has(term)
      ) ?? [],
  );
}

function perspectivesMeaningfullyDistinct(
  perspectiveA: string | undefined,
  perspectiveB: string | undefined,
  participants: [string, string] | undefined,
): boolean {
  if (!perspectiveA || !perspectiveB) return false;
  if (normalizedEvidence(perspectiveA) === normalizedEvidence(perspectiveB)) {
    return false;
  }
  const left = perspectiveTerms(perspectiveA, participants?.[0] ?? "");
  const right = perspectiveTerms(perspectiveB, participants?.[1] ?? "");
  if (left.size === 0 || right.size === 0) return false;
  let shared = 0;
  for (const term of left) {
    if (right.has(term)) shared++;
  }
  return shared / Math.min(left.size, right.size) < 0.5;
}

function practiceAssignsChildEmotionalLabor(
  practice: string | undefined,
  facts: string | undefined,
  participants: [string, string] | undefined,
): boolean {
  if (!practice || !facts || !participants) return false;
  const parentChild = new RegExp(
    `relationship\\s+between\\s+${
      participants.map((name) => name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"))
        .join("\\s+and\\s+")
    }\\s+is\\s+parent\\s+and\\s+child`,
    "i",
  );
  if (!parentChild.test(facts)) return false;
  const child = participants[1].replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return new RegExp(
    `\\b(?:ask|expect|have|tell)\\s+${child}\\s+to\\s+(?:calm|comfort|fix|manage|regulate|reassure|soothe|support)\\b`,
    "i",
  ).test(practice);
}

function composeSynastrySpoken(params: {
  perspectiveA: string;
  perspectiveB: string;
  weather: string;
  practice: string;
  display: string;
  next: string;
  relationshipSubject: string | undefined;
}): string {
  const nextBeat = `Next: ${params.next}`;
  // Structured beats are the source of truth. Server composition guarantees
  // the four labels and exact action even when Gemini's redundant spoken field
  // varies punctuation or omits a beat.
  const pairedPerspectives = [params.perspectiveA, params.perspectiveB]
    .map((value) => value.replace(/[.!?]+$/, ""))
    .join("; ");
  const pattern = completeSpeechSentence(
    pairedPerspectives,
    100,
  ) ??
    completeSpeechSentence(params.display, 100) ??
    "This relationship pattern has more than one side.";
  const today = completeSpeechSentence(params.weather, 65) ??
    (params.relationshipSubject
      ? `${params.relationshipSubject}'s chart carries the current timing; lived experience decides how it feels.`
      : "No current signal can replace what the people involved actually feel.");
  const practice = completeSpeechSentence(params.practice, 72) ??
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
  const perspectiveA = id === "synastry"
    ? cleanText(face.perspectiveA, 48)
    : undefined;
  const perspectiveB = id === "synastry"
    ? cleanText(face.perspectiveB, 48)
    : undefined;
  const weather = id === "synastry" ? cleanText(face.weather, 220) : undefined;
  const practice = id === "synastry"
    ? cleanText(face.practice, 220)
    : undefined;
  const generated = LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
    id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
  );
  const action = id === "synastry"
    ? cleanText(practice ?? face.action, 120)
    : generated
    ? cleanText(face.action, 120)
    : undefined;
  const temporal = id === "transits" || id === "synastry";
  const detail = boundedProse(face.detail, 480);
  const temporalEvidence = temporal
    ? cleanText(face.temporalEvidence, 300)
    : undefined;
  const evidence = cleanText(face.evidence, 220);
  const weatherEvidence = id === "synastry"
    ? cleanText(face.weatherEvidence, 220)
    : undefined;
  const relationshipParticipantsExpected = id === "synastry"
    ? relationshipParticipants(expectedFacts)
    : undefined;
  const perspectivesDistinct = id !== "synastry" ||
    perspectivesMeaningfullyDistinct(
      perspectiveA,
      perspectiveB,
      relationshipParticipantsExpected,
    );
  const perspectivesConditional = id !== "synastry" ||
    Boolean(
      perspectiveA && perspectiveB &&
        /\b(?:may|can|tends to)\b/i.test(perspectiveA) &&
        /\b(?:may|can|tends to)\b/i.test(perspectiveB),
    );
  const perspectivesIdentifyPeople = id !== "synastry" ||
    !relationshipParticipantsExpected ||
    Boolean(
      perspectiveA && perspectiveB &&
        normalizedEvidence(perspectiveA).startsWith(
          normalizedEvidence(relationshipParticipantsExpected[0]),
        ) &&
        normalizedEvidence(perspectiveB).startsWith(
          normalizedEvidence(relationshipParticipantsExpected[1]),
        ),
    );
  const perspectivesNonBlaming = id !== "synastry" ||
    Boolean(
      perspectiveA && perspectiveB &&
        !RELATIONSHIP_BLAME_RE.test(perspectiveA) &&
        !RELATIONSHIP_BLAME_RE.test(perspectiveB),
    );
  const practiceKeepsAdultResponsibility = id !== "synastry" ||
    !practiceAssignsChildEmotionalLabor(
      practice,
      expectedFacts,
      relationshipParticipantsExpected,
    );
  const relationshipSubject = id === "synastry"
    ? temporalEvidence?.match(
      /\b(?:conjunction|sextile|square|trine|opposition)\s+(.+?)\s+natal\b/i,
    )?.[1]?.trim()
    : undefined;
  // "Will" is a common otherwise-valid model slip. Normalize it to the
  // explicitly conditional "may"; stronger certainty language still fails.
  const modelNow = temporal
    ? cleanText(face.now, 300)?.replace(/\bwill\b/gi, "may")
    : undefined;
  const now = modelNow && relationshipSubject &&
      !normalizedEvidence(modelNow).includes(
        normalizedEvidence(relationshipSubject),
      )
    ? `${relationshipSubject}'s chart is the one touched. ${modelNow}`
    : modelNow;
  const next = temporal
    ? canonicalTemporalNext(temporalEvidence, id)
    : undefined;
  const spoken = perspectiveA && perspectiveB && weather && practice
    ? composeSynastrySpoken({
      perspectiveA,
      perspectiveB,
      weather,
      practice,
      display: display ?? "Notice the pattern without assigning blame.",
      next: next ?? "Let lived experience lead.",
      relationshipSubject,
    })
    : generated
    ? composeActionSpoken(modelSpoken, action)
    : modelSpoken;
  const actionIsDirect = !generated ||
    Boolean(action && DIRECT_ACTION_OPENING_RE.test(action));
  const actionIsSpoken = !generated ||
    Boolean(
      action && spoken &&
        actionKey(spoken).includes(actionKey(action)),
    );
  const perspectivesAreSpoken = id !== "synastry" ||
    Boolean(
      perspectiveA && perspectiveB && spoken &&
        actionKey(spoken).includes(actionKey(perspectiveA)) &&
        actionKey(spoken).includes(actionKey(perspectiveB)),
    );
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
    !/\b(will|guaranteed|inevitabl(?:e|y)|destined|fated|certain(?:ly)?)\b/i
      .test(
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
  const speechForSafety = spoken ?? modelSpoken;
  const spokenIsSpecific = speechForSafety === undefined ||
    !containsGenericReadingPhrase(speechForSafety);
  const spokenBytes = spoken === undefined
    ? 0
    : new TextEncoder().encode(spoken).byteLength;
  const spokenMatchesFace = speechForSafety === undefined ||
    contentMatchesFace(
      id,
      [headline, display, speechForSafety, detail].filter(Boolean).join(" "),
    );
  if (
    !headline || !display || !spoken || !detail ||
    !actionIsDirect || !actionIsSpoken ||
    !perspectivesDistinct || !perspectivesConditional ||
    !perspectivesIdentifyPeople || !perspectivesNonBlaming ||
    !practiceKeepsAdultResponsibility || !perspectivesAreSpoken ||
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
      actionIsDirect
        ? undefined
        : action
        ? "action-not-imperative"
        : `action:${textLength(face.action)}`,
      actionIsSpoken ? undefined : "action-not-spoken",
      spokenIsSpecific ? undefined : "generic-cliche",
      spokenMatchesFace ? undefined : "cross-face-language",
      id === "synastry" && !perspectiveA
        ? `perspectiveA:${textLength(face.perspectiveA)}`
        : undefined,
      id === "synastry" && !perspectiveB
        ? `perspectiveB:${textLength(face.perspectiveB)}`
        : undefined,
      perspectivesDistinct ? undefined : "perspectives-not-distinct",
      perspectivesConditional ? undefined : "perspectives-not-conditional",
      perspectivesIdentifyPeople ? undefined : "perspectives-wrong-people",
      perspectivesNonBlaming ? undefined : "perspective-blames-person",
      practiceKeepsAdultResponsibility
        ? undefined
        : "practice-assigns-child-emotional-labor",
      perspectivesAreSpoken ? undefined : "perspectives-not-spoken",
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
    ...(action ? { action } : {}),
    ...(now ? { now } : {}),
    ...(next ? { next } : {}),
    ...(temporalEvidence ? { temporalEvidence } : {}),
    ...(evidence ? { evidence } : {}),
    ...(weatherEvidence ? { weatherEvidence } : {}),
    ...(perspectiveA ? { perspectiveA } : {}),
    ...(perspectiveB ? { perspectiveB } : {}),
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
    "For each generated daily face provide headline, display, spoken, detail, and evidence: one short verbatim quote copied from DAILY FACTS that directly supports the reading. Except for synastry, also provide action: one specific practice under 96 characters beginning with a direct imperative verb. The server appends action to spoken when needed. For tarot also provide cardName. For synastry provide perspectiveA, perspectiveB, weather, practice, and weatherEvidence; each perspective begins with the corresponding person's exact supplied name and states their distinct side of the pattern in under 48 characters. practice is its action and must be under 96 characters beginning with a direct imperative verb. evidence supports only the durable two-sided pattern while weatherEvidence supports only temporary weather or the explicit absence of a live signal. Keep those as three distinct spoken beats. Synastry spoken must be a cohesive script under 260 characters with exactly three short labeled sentences: Pattern:, Today:, and Practice:. Omit Next because the server inserts device-computed timing. The server supplies canonical mode, title, and accent metadata.",
    "For Transits and Synastry also provide now and temporalEvidence. temporalEvidence must copy the relevant ten-day arc sentence exactly. now explains why the pattern matters in conditional language. The server derives next directly from the device timing; do not provide or invent a next date, event, certainty, or shared relationship effect.",
    "Use short fields by default: headline <= 42 chars; display <= 80; spoken <= 150; action/practice <= 96; detail <= 180. One sentence per field is normally enough.",
    "Make each face independently useful. spoken must be natural, soft, and ready for TTS; it should not mention JSON or instructions.",
    "Keep the packet coherent without making every face repeat the same sentence: choose one quiet theme supported by the facts, then let Moon, Inner Weather, Transits, Relationship Weather, Tarot, and Sky approach it through their own lens. Do not contradict a concrete fact on another face.",
    "Present astrology as Inner Weather and synastry as Relationship Weather. Their headline must be one friendly condition from Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense, chosen from the supplied chart and transit facts rather than invented mood data.",
    "For both weather faces, display gives one humane orientation and spoken explains what the condition may feel like plus one choice the person can make. detail preserves the astrological depth by naming the one or two supplied natal/transit factors that most support the metaphor. Describe a symbolic outlook, never a deterministic forecast.",
    "If a self-reported mood is supplied, honor it as present-moment first-person context. Never bend the astrology to validate it, turn it into a trait, or imply that LunaSay detected it. When mood and symbolic weather differ, name that gently as two different lenses and preserve the user's authority over their own experience.",
    "Synastry is Family Synastry, not romance with relabeled people. Use only family members and roles present in the facts. Treat the family as a reciprocal system: no person is the problem, and do not rank, compare, blame, diagnose, parentify a child, or make compatibility verdicts.",
    "For synastry only, perspectiveA and perspectiveB must each be under 48 characters; weather must be under 160; practice must be under 96; detail may be up to 360. The two perspectives must be meaningfully different, name both supplied people, and describe how their needs or styles meet without words like today, always, compatible, or destined. Never say one person causes, triggers, forces, or is the problem. detail integrates the two sides without deciding which one is right. weather uses only time-specific transit or wellness facts; if those are absent, say the chart cannot know today's lived weather. practice begins with a direct imperative verb and gives one specific adult care or repair choice. In a parent-child relationship, never make the child responsible for calming, comforting, reassuring, or regulating the adult. Keep astrology as supporting evidence rather than leading with planet jargon.",
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
      "Translate the supplied lunar facts into a grounded daily orientation. evidence must quote the Lunar phase estimate. Give one direct sensory observation or bounded action for today, using an imperative verb such as look, notice, compare, or mark. Do not invent a phase, sign, visibility, time, or event.",
    astrology:
      "Present astrology as Inner Weather: the person's durable natal baseline, not the day's transit report. evidence must quote the Primary natal chart fact, or the explicit fact that it is not configured. headline must be one of Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense. Name both a resource and a tension in plain language. spoken must end with one short imperative action sentence beginning Try, Notice, Name, Choose, Ask, Write, Pause, Look, or Consider. In detail, explicitly use the words resource and tension so both sides remain visible rather than collapsing into praise. Do not use the phrases trust your intuition, inner peace, beautifully aligned, or wonderful time. Astrology is a symbolic outlook, never a deterministic forecast.",
    transits:
      "This is the changing daily layer, distinct from Inner Weather. evidence must quote a Tight current-to-natal aspect, or the explicit fact that no such aspect or transit is available. temporalEvidence must copy the Ten-day transit arc exactly. now explains why the strongest supplied aspect matters in conditional language; never turn a daily sample into a guaranteed event or invent a calendar date. The server derives the next shift from temporalEvidence. Choose the most useful supplied transit, name both its pressure and its opening, and give one concrete action. In detail, explicitly use the words pressure and opening so the response neither catastrophizes nor becomes empty reassurance. spoken must include the current orientation. Do not use the phrases trust your intuition, inner peace, beautifully aligned, or wonderful time.",
    synastry:
      "This is Family Synastry, not romance with relabeled people. Treat the family as a reciprocal system: never rank, blame, diagnose, parentify a child, or make a compatibility verdict. headline must be one of Clear, Warm, Shifting, Inward, Tender, Changeable, Easy, Open, or Intense. evidence must quote the full identifying prefix plus content from The selected relationship, relationship between, or Tight major aspects; quoting only planet names is invalid. evidence supports only the durable pattern. perspectiveA and perspectiveB must each be under 48 characters, begin with the corresponding person's exact supplied name, use may, can, or tends to, and state meaningfully different sides of how the pattern meets. Never say one person causes, makes, forces, provokes, or triggers the other person's response. detail integrates both sides without deciding who is right. weatherEvidence must quote only a supplied live wellness, current relationship transit, or explicit no-live-signal fact. temporalEvidence must copy the Current relationship transit arc or explicit no-live-signal sentence exactly. now must name whose natal chart is touched and must not imply the transit automatically describes the whole relationship. The server derives the next shift from temporalEvidence. weather must not turn natal synastry into today's condition; when weatherEvidence says no signal, explicitly let lived experience lead. practice must be under 96 characters, begin with a direct imperative verb, and give one specific adult care or repair choice. In a parent-child relationship, keep regulation and repair responsibility with the adult. spoken must be a cohesive script under 260 characters with exactly three complete labeled sentences: Pattern: summarizes both perspectives; Today: summarizes weather; Practice: gives the action. Do not include Next, day offsets, or fragments in spoken.",
    tarot:
      "Offer one reflective daily draw, not a prediction. evidence must quote the Visible tarot card fact. Use that supplied deterministic card, name it in spoken, ask one concrete question, and give one observable action such as write, choose, or notice. Do not borrow astrology, transit, or sky evidence.",
    alethiometer:
      "Offer only an inviting, day-sensitive entry line. Do not pretend a question has already been answered.",
    sky:
      "Use only supplied observable sky, solar, weather, and timing facts. evidence must quote Local time or Current sky positions. A phase estimate does not prove the Moon is above the horizon; do not infer visibility, position, weather, or conditions. Prefer a supplied Current sky positions fact when one exists. Keep this observational and distinct from the Moon face; do not use the Lunar phase estimate as evidence. Connect one concrete supplied observation to a gentle invitation.",
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
    "Use short fields: headline <= 42 characters, display <= 80, spoken <= 150, action <= 96, and detail <= 180 unless the schema allows more.",
    "spoken must be natural, soft, ready for TTS, and no more than two complete sentences except on synastry. If spoken includes the action, copy action exactly rather than inventing a second practice. Do not mention JSON, prompts, models, or instructions.",
    params.id === "synastry"
      ? "practice is the structured action. Keep it under 96 characters and begin with Ask, Check, Choose, Give, Listen, Name, Notice, Offer, Pause, Say, Share, or Wait. The server preserves it as action in the final face."
      : LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
          params.id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
        )
      ? "action is required. Keep it under 96 characters and begin with Notice, Ask, Choose, Write, Pause, Look, Consider, Breathe, Compare, Name, Observe, or Wait. Make it specific enough to try today; the server composes it into spoken."
      : "",
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
  const evidence = (id: LunaSayDailyFaceId) =>
    lunaSayRequiredEvidence(id, params.facts);
  const transitTiming = lunaSayRequiredTemporalEvidence(
    "transits",
    params.facts,
  );
  const relationshipTiming = lunaSayRequiredTemporalEvidence(
    "synastry",
    params.facts,
  );
  const relationshipWeather = lunaSayRequiredWeatherEvidence(
    "synastry",
    params.facts,
  );
  const relationshipPeople = relationshipParticipants(params.facts);
  const fallbackPerspectiveA = relationshipPeople
    ? `${relationshipPeople[0]} may seek steadiness.`
    : "One person may seek steadiness.";
  const fallbackPerspectiveB = relationshipPeople
    ? `${relationshipPeople[1]} can take time to respond.`
    : "The other can take time to respond.";
  const relationshipNext = canonicalTemporalNext(
    relationshipTiming,
    "synastry",
  ) ?? "Let lived experience lead until a current signal is available.";
  const tarotEvidence = evidence("tarot");
  const tarotCard = tarotEvidence?.match(
    /visible tarot card(?:\s+is|:)\s*([^,.;\n]+)/i,
  )?.[1]?.trim() || lunaSayDailyTarotCardName(params.date);
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
      moon: {
        ...daily(
          "Moon",
          "moon",
          "Look again tonight",
          "Let the supplied lunar fact be enough until the detailed reading returns.",
          "The detailed Moon reading is resting. Look again tonight and notice what is actually visible.",
          "The supplied lunar fact remains available while the interpretive reading rests.",
        ),
        action: "Look again tonight and notice what is actually visible.",
        ...(evidence("moon") ? { evidence: evidence("moon") } : {}),
      },
      astrology: {
        mode: "daily",
        title: "Inner Weather",
        headline: "Shifting",
        display:
          "Stay flexible and notice what changes before choosing a direction.",
        spoken:
          "Your inner weather is shifting. Give yourself room to notice what changes, then choose one grounded next step.",
        detail:
          "The supplied natal fact remains available; this gentle fallback makes no additional astrological claim.",
        action: "Notice what changes, then choose one grounded next step.",
        ...(evidence("astrology") ? { evidence: evidence("astrology") } : {}),
        accent: "violet",
      },
      transits: {
        ...daily(
          "Transits",
          "amber",
          "No forecast loaded",
          "Keep the day open rather than filling the silence with a prediction.",
          "The detailed transit reading is unavailable. Pause before naming a pattern or acting on it.",
          "The supplied transit fact remains available while the interpretive reading rests.",
        ),
        action: "Pause before naming a pattern or acting on it.",
        now: "No verified timing is loaded.",
        next: canonicalTemporalNext(transitTiming, "transits") ??
          "Refresh later rather than filling the gap with a prediction.",
        temporalEvidence: transitTiming ??
          "Ten-day transit arc is unavailable.",
        ...(evidence("transits") ? { evidence: evidence("transits") } : {}),
      },
      synastry: {
        mode: "daily",
        title: "Relationship Weather",
        headline: "Tender",
        display: "Notice the pattern without making one person the problem.",
        perspectiveA: fallbackPerspectiveA,
        perspectiveB: fallbackPerspectiveB,
        spoken:
          `Pattern: ${fallbackPerspectiveA} ${fallbackPerspectiveB} Today: Let lived experience lead. Next: ${relationshipNext} Practice: Ask what support would feel useful, then listen and leave room for repair.`,
        action:
          "Ask what support would feel useful, then listen and leave room for repair.",
        detail:
          "Family Synastry is relationship weather and a prompt for care, never a verdict about any person.",
        now: "No verified relationship timing is loaded.",
        next: relationshipNext,
        temporalEvidence: relationshipTiming ??
          "No live relationship signal is available.",
        ...(relationshipWeather
          ? { weatherEvidence: relationshipWeather }
          : {}),
        ...(evidence("synastry") ? { evidence: evidence("synastry") } : {}),
        accent: "rose",
      },
      tarot: {
        ...daily(
          "Tarot",
          "amber",
          "Hold one clear question",
          "Use the card as a prompt, not a prediction.",
          "The detailed card reading is resting. Hold one clear question and notice your first honest response.",
          `The daily interpretation is unavailable; ${tarotCard} remains a reflective image rather than a prediction.`,
        ),
        action:
          "Hold one clear question and notice your first honest response.",
        ...(tarotEvidence ? { evidence: tarotEvidence } : {}),
        cardName: tarotCard,
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
      sky: {
        ...daily(
          "Sky",
          "blue",
          "Look outside",
          "The live sky is more trustworthy than an unavailable reading.",
          "The detailed sky reading is unavailable. Look outside and begin with what you can actually see.",
          "The supplied observation remains available without inferring any additional sky condition.",
        ),
        action: "Look outside and begin with what you can actually see.",
        ...(evidence("sky") ? { evidence: evidence("sky") } : {}),
      },
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
