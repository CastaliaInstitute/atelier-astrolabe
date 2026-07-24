import {
  LUNASAY_GENERATED_DAILY_FACE_IDS,
  type LunaSayDailyFace,
  type LunaSayDailyFaceId,
  type LunaSayDailyPacket,
  lunaSayRequiredEvidence,
  lunaSayRequiredTemporalEvidence,
  lunaSayRequiredWeatherEvidence,
} from "./lunasayDailyPacket.ts";

export const LUNASAY_QUALITY_BENCHMARK_VERSION = 4;
export const LUNASAY_QUALITY_AVERAGE_TARGET = 72;
export const LUNASAY_QUALITY_FACE_FLOOR = 60;
export const LUNASAY_QUALITY_MAX_SIMILARITY = 0.42;

export type LunaSayQualityDimensions = {
  specificity: number;
  actionability: number;
  humility: number;
  distinctness: number;
  speech: number;
  nuance: number;
};

export type LunaSayFaceQuality = {
  face: LunaSayDailyFaceId;
  score: number;
  dimensions: LunaSayQualityDimensions;
  issues: string[];
  maxSimilarity: number;
  spokenBytes: number;
  spokenSentences: number;
};

export type LunaSayReadingQualityReport = {
  benchmarkVersion: typeof LUNASAY_QUALITY_BENCHMARK_VERSION;
  hardGatePassed: boolean;
  releaseGatePassed: boolean;
  averageScore: number;
  minimumFaceScore: number;
  maximumSimilarity: number;
  hardIssues: string[];
  faces: LunaSayFaceQuality[];
};

const STOPWORDS = new Set([
  "about",
  "after",
  "again",
  "also",
  "and",
  "are",
  "because",
  "before",
  "between",
  "but",
  "can",
  "could",
  "daily",
  "does",
  "for",
  "from",
  "has",
  "have",
  "into",
  "its",
  "let",
  "may",
  "more",
  "not",
  "one",
  "only",
  "rather",
  "than",
  "that",
  "the",
  "their",
  "there",
  "this",
  "through",
  "today",
  "until",
  "what",
  "when",
  "where",
  "which",
  "while",
  "with",
  "your",
]);
const ACTION_WORDS =
  /\b(acknowledge|act(?:ion)?s?|ask|breathe|check|choose|compare|connect|consider|create|direct|explore|find|focus|gaze|give|hold|honor|identify|listen|look|mark|name|notice|observe|offer|pause|place|protect|reach|reflect|release|rest|review|say|set|share|speak|step|take|tell|track|try|use|wait|watch|write)\b|be present/i;
const DIRECT_ACTION_OPENING_RE =
  /^(acknowledge|ask|breathe|check|choose|compare|connect|consider|create|explore|find|focus|gaze|give|hold|honor|identify|listen|look|mark|name|notice|observe|offer|pause|place|protect|reach|reflect|release|rest|review|say|set|share|speak|step|take|tell|track|try|use|wait|watch|write)\b/i;
const CONCRETE_MARKERS =
  /\b(one|before|after|today|tonight|next|question|need|boundary|conversation|body|sky|moon|card)\b/i;
const HUMILITY_WORDS =
  /\b(may|might|could|can|consider|invite|notice|question|reflection|symbolic|lived experience|if)\b/i;
const CERTAINTY_WORDS =
  /\b(always|certain(?:ly)?|destined|fated|guaranteed|inevitabl(?:e|y)|proves?|will definitely|the universe wants)\b/i;
const GENERIC_WORDS =
  /\b(trust your intuition|inner peace|beautifully aligned|wonderful time|embrace this|finding peace|gentle reminder)\b/i;
const RELATIONSHIP_BLAME_RE =
  /\b(?:causes?|forces?|makes?|provokes?|triggers?|at fault|to blame|is the problem|too (?:demanding|emotional|much|sensitive))\b/i;
const RESOURCE_WORDS =
  /\b(opening|resource|support|capacity|clarity|courage|curiosity|choice|connection|care|repair|steady|strength|useful)\b/i;
const TENSION_WORDS =
  /\b(pressure|tension|friction|restless|sensitive|strain|conflict|limit|uncertain|overwhelm|react|difficult|challenge)\b/i;
const FACE_WORDS: Record<LunaSayDailyFaceId, RegExp> = {
  moon: /\b(moon|lunar|phase|illuminat|wax|wane|night|visible)\w*/i,
  astrology: /\b(natal|inner weather|baseline|chart|resource|tension)\b/i,
  transits: /\b(transit|aspect|current|timing|arc|active|sample)\w*/i,
  synastry:
    /\b(relationship|family|mutual|reciprocal|repair|support|together|each)\b/i,
  tarot: /\b(card|draw|image|question|tarot|prompt)\b/i,
  sky: /\b(sky|sun|sunset|sunrise|visible|horizon|planet|star|observe)\w*/i,
  alethiometer: /\b(alethiometer|question|ask)\b/i,
  journal: /\b(journal|write|reflect)\b/i,
  conversation: /\b(conversation|speak|ask|listen)\b/i,
};

function normalized(value: string | undefined): string {
  return (value ?? "").toLowerCase().replace(/\s+/g, " ").trim();
}

function content(face: LunaSayDailyFace): string {
  return [
    face.headline,
    face.display,
    face.spoken,
    face.detail,
    face.action,
    face.now,
    face.next,
    face.perspectiveA,
    face.perspectiveB,
  ].filter(Boolean).join(" ");
}

function actionKey(value: string | undefined): string {
  return normalized(value).replace(/[^a-z0-9']+/g, " ").trim();
}

function tokens(value: string): Set<string> {
  return new Set(
    normalized(value).match(/[a-z][a-z'-]{2,}/g)
      ?.filter((token) => !STOPWORDS.has(token)) ?? [],
  );
}

function similarity(a: string, b: string): number {
  const left = tokens(a);
  const right = tokens(b);
  if (left.size === 0 || right.size === 0) return 0;
  let intersection = 0;
  for (const token of left) {
    if (right.has(token)) intersection++;
  }
  return intersection / (left.size + right.size - intersection);
}

function evidenceOverlap(face: LunaSayDailyFace): number {
  const evidence = tokens(face.evidence ?? "");
  const reading = tokens(content(face));
  let overlap = 0;
  for (const token of evidence) {
    if (reading.has(token)) overlap++;
  }
  return overlap;
}

function sentenceCount(value: string): number {
  return value.split(/[.!?]+(?:\s|$)/).filter((part) => part.trim()).length;
}

function relationshipParticipants(
  value: string,
): [string, string] | undefined {
  const match = value.match(
    /\b(?:the selected relationship|relationship)\s+between\s+([^,.;:\n]+?)\s+and\s+([^,.;:\n]+?)\s+(?:is|has|shows|includes|:)/i,
  );
  if (!match) return undefined;
  const first = match[1].replace(/\s+/g, " ").trim();
  const second = match[2].replace(/\s+/g, " ").trim();
  return first && second ? [first, second] : undefined;
}

function perspectiveTerms(value: string, participant: string): Set<string> {
  const participantTerms = new Set(
    normalized(participant).match(/[a-z][a-z'-]{1,}/g) ?? [],
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
    normalized(value).match(/[a-z][a-z'-]{2,}/g)
      ?.filter((term) =>
        !participantTerms.has(term) && !structural.has(term)
      ) ?? [],
  );
}

function perspectivesMeaningfullyDistinct(
  perspectiveA: string,
  perspectiveB: string,
  participants: [string, string] | undefined,
): boolean {
  if (normalized(perspectiveA) === normalized(perspectiveB)) return false;
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
  facts: string,
  participants: [string, string] | undefined,
): boolean {
  if (!practice || !participants) return false;
  const escaped = participants.map((name) =>
    name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
  );
  const parentChild = new RegExp(
    `relationship\\s+between\\s+${escaped[0]}\\s+and\\s+${
      escaped[1]
    }\\s+is\\s+parent\\s+and\\s+child`,
    "i",
  );
  if (!parentChild.test(facts)) return false;
  return new RegExp(
    `\\b(?:ask|expect|have|tell)\\s+${
      escaped[1]
    }\\s+to\\s+(?:calm|comfort|fix|manage|regulate|reassure|soothe|support)\\b`,
    "i",
  ).test(practice);
}

function nuanceScore(id: LunaSayDailyFaceId, face: LunaSayDailyFace): number {
  const value = content(face);
  if (id === "synastry") {
    let score = 0;
    if (
      face.perspectiveA && face.perspectiveB &&
      normalized(face.perspectiveA) !== normalized(face.perspectiveB)
    ) {
      score += 5;
    }
    if (/\b(today|lived experience|current|weather)\b/i.test(value)) score += 5;
    if (/\b(practice|ask|listen|repair|support|need|space)\b/i.test(value)) {
      score += 5;
    }
    return score;
  }
  if (id === "tarot") {
    return Math.min(
      15,
      (face.cardName ? 5 : 0) +
        (/\b(question|ask|consider|notice|write)\b/i.test(value) ? 5 : 0) +
        (!/\b(predict|will happen|foretell)\b/i.test(value) ? 5 : 0),
    );
  }
  if (id === "moon" || id === "sky") {
    return Math.min(
      15,
      (FACE_WORDS[id].test(value) ? 5 : 0) +
        (/\b(visible|observe|look|notice|tonight|sunset|sunrise|horizon)\b/i
            .test(value)
          ? 5
          : 0) +
        (ACTION_WORDS.test(value) ? 5 : 0),
    );
  }
  return Math.min(
    15,
    (RESOURCE_WORDS.test(value) ? 7 : 0) +
      (TENSION_WORDS.test(value) ? 8 : 0),
  );
}

function hardIssuesForFace(
  id: LunaSayDailyFaceId,
  face: LunaSayDailyFace,
  facts: string,
): string[] {
  const issues: string[] = [];
  const reading = content(face);
  if (!face.action) issues.push("missing structured action");
  if (face.action && !DIRECT_ACTION_OPENING_RE.test(face.action)) {
    issues.push("structured action does not begin with a direct verb");
  }
  if (
    face.action &&
    !actionKey(face.spoken).includes(actionKey(face.action))
  ) {
    issues.push("structured action is not present in spoken delivery");
  }
  if (face.action && face.action.length > 120) {
    issues.push("structured action exceeds device boundary");
  }
  if (!face.evidence) issues.push("missing evidence");
  if (
    face.evidence &&
    facts &&
    !normalized(facts).includes(normalized(face.evidence))
  ) {
    issues.push("evidence is not verbatim in supplied facts");
  }
  const requiredEvidence = facts
    ? lunaSayRequiredEvidence(id, facts)
    : undefined;
  if (
    requiredEvidence &&
    normalized(face.evidence) !== normalized(requiredEvidence)
  ) {
    issues.push("evidence is not the server-selected face fact");
  }
  if (CERTAINTY_WORDS.test(reading)) issues.push("deterministic language");
  if (GENERIC_WORDS.test(reading)) issues.push("stock horoscope language");
  if (new TextEncoder().encode(face.spoken).byteLength >= 384) {
    issues.push("spoken field exceeds device boundary");
  }
  if ((id === "transits" || id === "synastry") && !face.temporalEvidence) {
    issues.push("missing temporal evidence");
  }
  if (
    face.temporalEvidence &&
    facts &&
    !normalized(facts).includes(normalized(face.temporalEvidence))
  ) {
    issues.push("temporal evidence is not verbatim in supplied facts");
  }
  const requiredTemporalEvidence = facts
    ? lunaSayRequiredTemporalEvidence(id, facts)
    : undefined;
  if (
    requiredTemporalEvidence &&
    normalized(face.temporalEvidence) !== normalized(requiredTemporalEvidence)
  ) {
    issues.push("temporal evidence is not the server-selected timing fact");
  }
  if (id === "synastry") {
    const participants = relationshipParticipants(facts);
    if (!face.perspectiveA || !face.perspectiveB) {
      issues.push("missing two-sided relationship perspectives");
    } else {
      if (face.perspectiveA.length > 48 || face.perspectiveB.length > 48) {
        issues.push("relationship perspective exceeds spoken boundary");
      }
      if (
        !perspectivesMeaningfullyDistinct(
          face.perspectiveA,
          face.perspectiveB,
          participants,
        )
      ) {
        issues.push("relationship perspectives are not meaningfully distinct");
      }
      if (
        !/\b(?:may|can|tends to)\b/i.test(face.perspectiveA) ||
        !/\b(?:may|can|tends to)\b/i.test(face.perspectiveB)
      ) {
        issues.push("relationship perspectives are stated as fixed traits");
      }
      if (
        RELATIONSHIP_BLAME_RE.test(face.perspectiveA) ||
        RELATIONSHIP_BLAME_RE.test(face.perspectiveB)
      ) {
        issues.push("relationship perspective makes one person the problem");
      }
      if (
        participants &&
        (
          !normalized(face.perspectiveA).startsWith(
            normalized(participants[0]),
          ) ||
          !normalized(face.perspectiveB).startsWith(
            normalized(participants[1]),
          )
        )
      ) {
        issues.push("relationship perspectives do not identify both people");
      }
      if (
        !actionKey(face.spoken).includes(actionKey(face.perspectiveA)) ||
        !actionKey(face.spoken).includes(actionKey(face.perspectiveB))
      ) {
        issues.push("spoken Pattern does not include both perspectives");
      }
    }
    if (practiceAssignsChildEmotionalLabor(face.action, facts, participants)) {
      issues.push("relationship practice assigns emotional labor to a child");
    }
    const requiredWeatherEvidence = facts
      ? lunaSayRequiredWeatherEvidence(id, facts)
      : undefined;
    if (!face.weatherEvidence) {
      issues.push("missing relationship weather evidence");
    }
    if (
      requiredWeatherEvidence &&
      normalized(face.weatherEvidence) !== normalized(requiredWeatherEvidence)
    ) {
      issues.push(
        "relationship weather evidence is not the server-selected live fact",
      );
    }
  }
  if ((id === "transits" || id === "synastry") && !face.next) {
    issues.push("missing server-owned next timing");
  }
  if (
    id === "synastry" &&
    !(
      /^Pattern:/i.test(face.spoken) &&
      /\bToday:/i.test(face.spoken) &&
      /\bNext:/i.test(face.spoken) &&
      /\bPractice:/i.test(face.spoken)
    )
  ) {
    issues.push("synastry speech is not a complete four-beat narrative");
  }
  if (
    id === "sky" &&
    /\bmoon\b/i.test(reading) &&
    !/(?:current sky positions:[^\n]*\bmoon\b|moon[^\n]*(?:visible|above|altitude|horizon))/i
      .test(facts)
  ) {
    issues.push("sky infers Moon visibility without an observational fact");
  }
  return issues;
}

export function scoreLunaSayReadingQuality(
  packet: LunaSayDailyPacket,
  facts = "",
): LunaSayReadingQualityReport {
  const generated = LUNASAY_GENERATED_DAILY_FACE_IDS.map((id) => ({
    id,
    face: packet.faces[id],
    text: content(packet.faces[id]),
  }));
  const faces = generated.map(({ id, face, text }) => {
    const peerSimilarities = generated
      .filter((peer) => peer.id !== id)
      .map((peer) => similarity(text, peer.text));
    const maxSimilarity = Math.max(0, ...peerSimilarities);
    const overlap = evidenceOverlap(face);
    const detail = normalized(face.detail);
    const display = normalized(face.display);
    const actionIsDirect = Boolean(
      face.action && DIRECT_ACTION_OPENING_RE.test(face.action),
    );
    const actionIsSpoken = Boolean(
      face.action &&
        actionKey(face.spoken).includes(actionKey(face.action)),
    );
    const specificity = Math.min(
      20,
      (face.evidence ? 8 : 0) +
        (overlap >= 2 ? 6 : overlap === 1 ? 3 : 0) +
        (detail.length >= 70 && detail !== display ? 6 : 0),
    );
    const actionability = Math.min(
      20,
      (actionIsDirect ? 10 : 0) +
        (actionIsSpoken ? 5 : 0) +
        (CONCRETE_MARKERS.test(text) ? 5 : 0),
    );
    const humility = Math.max(
      0,
      15 -
        (CERTAINTY_WORDS.test(text) ? 15 : 0) -
        (GENERIC_WORDS.test(text) ? 5 : 0) +
        (HUMILITY_WORDS.test(text) ? 0 : -5),
    );
    const distinctness = Math.min(
      15,
      (FACE_WORDS[id].test(text) ||
          (id === "tarot" &&
            Boolean(
              face.cardName &&
                normalized(text).includes(normalized(face.cardName)),
            ))
        ? 10
        : 0) +
        (maxSimilarity <= LUNASAY_QUALITY_MAX_SIMILARITY ? 5 : 0),
    );
    const spokenBytes = new TextEncoder().encode(face.spoken).byteLength;
    const sentences = sentenceCount(face.spoken);
    // The server may append the validated action as a fourth short sentence.
    // Synastry retains its five-part Pattern/Today/Next/Practice delivery.
    const maxSpokenSentences = id === "synastry" ? 5 : 4;
    const speech = Math.min(
      15,
      (spokenBytes >= 45 && spokenBytes < 384 ? 5 : 0) +
        (/[.!?]$/.test(face.spoken.trim()) ? 5 : 0) +
        (sentences >= 1 && sentences <= maxSpokenSentences ? 5 : 0),
    );
    const nuance = nuanceScore(id, face);
    const dimensions = {
      specificity,
      actionability,
      humility,
      distinctness,
      speech,
      nuance,
    };
    const issues: string[] = [];
    if (specificity < 14) issues.push("weak evidence-to-reading specificity");
    if (actionability < 15) issues.push("not concretely actionable");
    if (humility < 10) issues.push("insufficient epistemic humility");
    if (distinctness < 15) issues.push("face identity or distinctness is weak");
    if (speech < 15) {
      issues.push("spoken delivery is not fully bounded/coherent");
    }
    if (nuance < 10) issues.push("face-specific nuance is thin");
    return {
      face: id,
      score: Object.values(dimensions).reduce((sum, value) => sum + value, 0),
      dimensions,
      issues,
      maxSimilarity: Number(maxSimilarity.toFixed(3)),
      spokenBytes,
      spokenSentences: sentences,
    };
  });
  const hardIssues = generated.flatMap(({ id, face }) =>
    hardIssuesForFace(id, face, facts).map((issue) => `${id}: ${issue}`)
  );
  const scores = faces.map((face) => face.score);
  const averageScore = scores.reduce((sum, score) => sum + score, 0) /
    Math.max(1, scores.length);
  const minimumFaceScore = Math.min(...scores);
  const maximumSimilarity = Math.max(
    ...faces.map((face) => face.maxSimilarity),
  );
  const hardGatePassed = hardIssues.length === 0;
  const releaseGatePassed = hardGatePassed &&
    averageScore >= LUNASAY_QUALITY_AVERAGE_TARGET &&
    minimumFaceScore >= LUNASAY_QUALITY_FACE_FLOOR &&
    maximumSimilarity <= LUNASAY_QUALITY_MAX_SIMILARITY &&
    faces.every((face) => face.issues.length === 0);
  return {
    benchmarkVersion: LUNASAY_QUALITY_BENCHMARK_VERSION,
    hardGatePassed,
    releaseGatePassed,
    averageScore: Number(averageScore.toFixed(1)),
    minimumFaceScore,
    maximumSimilarity: Number(maximumSimilarity.toFixed(3)),
    hardIssues,
    faces,
  };
}
