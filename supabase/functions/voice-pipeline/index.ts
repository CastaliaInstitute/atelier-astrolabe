import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { createClient } from "npm:@supabase/supabase-js@2.49.8";

import { buildClockAgendaTranscript } from "../_shared/calciferClockBrief.ts";
import {
  buildDailyBriefingTranscript,
  SYSTEM_VOICE_FACE_DAILY_BRIEFING,
} from "../_shared/dailyBriefing.ts";
import {
  buildLunaSayDailyFaceInstruction,
  buildLunaSayDailyPacketInstruction,
  LUNASAY_DAILY_PACKET_FACE,
  LUNASAY_GENERATED_DAILY_FACE_IDS,
  type LunaSayDailyFaceId,
  lunaSayDailyFaceJsonSchema,
  lunaSayDailyPacketFallback,
  lunaSayDailyPacketJsonSchema,
  lunaSayDateForEpoch,
  lunaSayFocusedFacts,
  lunaSayRequiredEvidence,
  lunaSayRequiredTemporalEvidence,
  lunaSayRequiredWeatherEvidence,
  normalizeLunaSayTimezone,
  parseLunaSayDailyFace,
  parseLunaSayDailyPacket,
} from "../_shared/lunasayDailyPacket.ts";
import {
  capTextForWatchTts,
  corsHeaders,
  envKeys,
  geminiGenerate,
  jsonResponse,
  speechRecognize,
  ttsMp3Base64,
  ttsMp3Bytes,
  watchTtsMaxChars,
  watchTtsMaxCharsDailyBriefing,
  type WatchTtsVoiceSelection,
  watchTtsVoiceSelection,
} from "../_shared/googleVoice.ts";
import {
  SYSTEM_VOICE_FACE_CLOCK_AGENDA,
  VOICE_FACE_ALETHIOMETER,
  VOICE_FACE_ASTRO,
  VOICE_FACE_BABEL_FISH,
  VOICE_FACE_CLOCK_AGENDA,
  VOICE_FACE_CRYSTAL_BALL,
  VOICE_FACE_DAILY_BRIEFING,
  VOICE_FACE_SYNASTRY,
} from "../_shared/mynahVoiceFaces.ts";
import {
  type FacultySelection,
  isDedicatedFacultyFace,
  matchAskFacultyRoute,
  normalizeFacultySlug,
  siblingFunctionUrl,
} from "../_shared/askFacultyRoute.ts";
import {
  type FacultyTtsConfig,
  facultyTtsFallback,
  facultyTtsFromRow,
} from "../_shared/facultyTts.ts";
import {
  appendMynahCommonplaceEntry,
  scheduleMynahCommonplaceLog,
} from "../_shared/commonplaceDirectus.ts";
import { verifyAstrolabeDevice } from "../_shared/deviceAuth.ts";
import { scheduleLunaSayGithubLog } from "../_shared/lunasayGithubLog.ts";
import {
  checkVoiceUsageGate,
  estimateGeminiFlashTtsUsd,
  estimateGeminiUsd,
  estimateSttUsd,
  estimateTokensFromChars,
  estimateTtsUsd,
  resolveVoiceUsageUserId,
  scheduleVoiceUsageLog,
  voiceUsageGateResponse,
} from "../_shared/voiceUsage.ts";

type ReqBody = {
  audioBase64?: string;
  ttsText?: string;
  sampleRateHertz?: number;
  languageCode?: string;
  alternativeLanguageCodes?: string[];
  message?: string;
  systemInstruction?: string;
  skipLlm?: boolean;
  earlyRoute?: boolean;
  geminiModel?: string;
  /**
   * Pipeline profile (optional). Each face can imply server-side context + default prompts.
   * Example: `clock_agenda` loads Calcifer CalDAV; no `message` / `audioBase64` required.
   */
  face?: string;
  /** Unix seconds for time-aware faces (e.g. clock agenda); defaults to server now. */
  epochSeconds?: number;
  /** Device-built sky facts for `daily_briefing` (astrology, synastry, moon). */
  briefingFacts?: string;
  /** Device local civil hour, 0-23, used to soften evening/night TTS. */
  localHour?: number;
  /** IANA timezone used to make a cache packet unambiguous across midnight. */
  timezone?: string;
  /** Product profile, so delivery can remain LunaSay-specific despite a shared voice service. */
  deviceProfile?: string;
  /** Faculty id/slug whose Google TTS voice should be used for faculty-flavored replies. */
  facultySlug?: string;
  /** Optional display name for the faculty metadata headers / logging fallback. */
  facultyName?: string;
  /** Device-side turn history; forwarded to ask-faculty (does not replace faculty voice_prompt). */
  conversationHistory?: string;
  /**
   * `conversation` (default): STT -> LLM -> TTS/reply.
   * `transcribe` / `journal`: STT only, optionally log to Commonplace, no immediate reply.
   */
  interactionMode?: "conversation" | "transcribe" | "journal";
  /** Commonplace behavior. Defaults to `conversation` for conversational responses. */
  commonplaceMode?: "off" | "conversation" | "journal";
  /** Boolean shorthand for Commonplace logging; false disables, true uses the mode default. */
  logToCommonplace?: boolean;
  /** Test/verification mode: wait for the Directus write and report the real result. */
  commonplaceSync?: boolean;
  /** Direct Google Cloud TTS voice override for tour/device narration. */
  ttsVoiceName?: string;
  ttsVoice?: string | Record<string, unknown>;
  voiceName?: string;
  voice?: string | Record<string, unknown>;
  /**
   * `json` (default): `{ transcript, reply, audioBase64 }`.
   * `mp3`: raw MPEG body (~33% smaller download); text in `X-Voice-*` headers.
   */
  responseFormat?: "json" | "mp3";
  generateTts?: boolean;
  tts?: boolean;
};

type AskFacultyResponse = {
  transcript?: string;
  reply?: string;
  audioBase64?: string;
  route?: string;
  facultySlug?: string;
  facultyName?: string;
  facultyBustUrl?: string | null;
};

const LUNASAY_TTS_PROMPT =
  "Speak close, soft, and lucid, like a calm companion sharing a small piece of night-sky wisdom. Use an easy, natural conversational pace and clear diction for a small speaker. Let the mystery come from the words. Never sound slow, low, ominous, breathy, theatrical, or like a meditation recording.";

const LUNASAY_DAILY_TTS_FACES = new Set([
  "moon",
  "astrology",
  "transits",
  "synastry",
  "tarot",
  "sky",
]);

function isLunaSayProfile(value: unknown): boolean {
  return typeof value === "string" && value.trim().toLowerCase() === "lunasay";
}

function lunaSayDailyGeminiTtsVoice(face: string, profile: unknown) {
  if (!isLunaSayProfile(profile) || !LUNASAY_DAILY_TTS_FACES.has(face)) {
    return undefined;
  }
  return {
    languageCode: Deno.env.get("LUNASAY_TTS_LANGUAGE_CODE")?.trim() || "en-US",
    name: Deno.env.get("LUNASAY_GEMINI_TTS_VOICE")?.trim() || "Kore",
    modelName: Deno.env.get("LUNASAY_GEMINI_TTS_MODEL")?.trim() ||
      "gemini-2.5-flash-tts",
  };
}

type AlethiometerReply = {
  questionSymbols: number[];
  answerSymbol: number;
  spoken: string;
};

/** Faculty firmware streams VAD PCM: [u32 jsonLen LE][json metadata][raw LINEAR16 mono PCM]. */
const VOICE_STREAM_CT = "application/vnd.astrolabe.voice-stream";
const ALETHIOMETER_SYMBOLS = [
  "RIDER",
  "CLOVER",
  "SHIP",
  "HOUSE",
  "TREE",
  "CLOUDS",
  "SNAKE",
  "COFFIN",
  "BOUQUET",
  "SCYTHE",
  "WHIP",
  "BIRDS",
  "CHILD",
  "FOX",
  "BEAR",
  "STARS",
  "STORK",
  "DOG",
  "TOWER",
  "GARDEN",
  "MOUNTAIN",
  "ROADS",
  "MICE",
  "HEART",
  "RING",
  "BOOK",
  "LETTER",
  "MAN",
  "WOMAN",
  "LILY",
  "SUN",
  "MOON",
  "KEY",
  "FISH",
  "ANCHOR",
  "CROSS",
];

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

async function parseVoiceRequestBody(req: Request): Promise<ReqBody> {
  const ct = (req.headers.get("content-type") ?? "").toLowerCase();
  if (!ct.includes(VOICE_STREAM_CT)) {
    return (await req.json()) as ReqBody;
  }

  const raw = new Uint8Array(await req.arrayBuffer());
  if (raw.byteLength < 4) {
    throw new Error("voice stream too short");
  }
  const jsonLen = new DataView(raw.buffer, raw.byteOffset, raw.byteLength)
    .getUint32(0, true);
  if (
    !Number.isFinite(jsonLen) || jsonLen <= 0 || jsonLen > 16384 ||
    4 + jsonLen > raw.byteLength
  ) {
    throw new Error("invalid voice stream header");
  }
  const jsonText = new TextDecoder().decode(raw.subarray(4, 4 + jsonLen));
  const body = JSON.parse(jsonText) as ReqBody;
  const pcm = raw.subarray(4 + jsonLen);
  if (pcm.byteLength === 0) {
    throw new Error("empty PCM in voice stream");
  }
  body.audioBase64 = bytesToBase64(pcm);
  body.sampleRateHertz = body.sampleRateHertz ?? 16000;
  return body;
}

const ASK_FACULTY_SELECTIONS: Array<FacultySelection & { hints: string }> = [
  {
    slug: "a.darwin",
    name: "Charles Darwin",
    hints:
      "biology, evolution, natural selection, variation, adaptation, natural history, geology, botany, species",
  },
  {
    slug: "a.einstein",
    name: "Einstein",
    hints: "physics, time, relativity, pattern, wonder, imagination, systems",
  },
  {
    slug: "marie-curie",
    name: "Marie Curie",
    hints: "experiment, care, materials, persistence, evidence, patience",
  },
  {
    slug: "hypatia",
    name: "Hypatia",
    hints:
      "mathematics, philosophy, civic clarity, teaching, ethics, astronomy",
  },
  {
    slug: "a.hesse",
    name: "Hermann Hesse",
    hints:
      "literature, mysticism, spirituality, Catholic church, monasteries, discipline, shadow, Jung, individuation, Glass Bead Game",
  },
];

function commonplaceRoute(face: string, fallback: string): string {
  switch (face.trim().toLowerCase()) {
    case VOICE_FACE_SYNASTRY:
      return "synastry";
    case VOICE_FACE_ASTRO:
    case "astrology":
      return "astrology";
    case VOICE_FACE_CLOCK_AGENDA:
      return "clock_agenda";
    case VOICE_FACE_DAILY_BRIEFING:
      return "daily_briefing";
    case "question_of_day":
    case "question-day":
    case "qotd":
      return "question_of_day";
    case VOICE_FACE_BABEL_FISH:
    case "babel-fish":
    case "babel":
      return VOICE_FACE_BABEL_FISH;
    case VOICE_FACE_ALETHIOMETER:
      return VOICE_FACE_ALETHIOMETER;
    case VOICE_FACE_CRYSTAL_BALL:
    case "crystal_ball":
      return VOICE_FACE_CRYSTAL_BALL;
    default:
      return fallback;
  }
}

function babelFishAlternativeLanguageCodes(body: ReqBody): string[] {
  const explicit = Array.isArray(body.alternativeLanguageCodes)
    ? body.alternativeLanguageCodes
      .map((v) => (typeof v === "string" ? v.trim() : ""))
      .filter(Boolean)
    : [];
  if (explicit.length) {
    return explicit.slice(0, 3);
  }
  return ["es-US", "fr-FR", "de-DE"];
}

function wantsMp3Response(req: Request, body: ReqBody): boolean {
  const fmt = (body.responseFormat ?? "").trim().toLowerCase();
  if (fmt === "mp3") return true;
  const accept = (req.headers.get("Accept") ?? "").toLowerCase();
  return accept.includes("audio/mpeg") || accept.includes("audio/mp3");
}

function shouldGenerateTts(req: Request, body: ReqBody): boolean {
  if (typeof body.generateTts === "boolean") return body.generateTts;
  if (typeof body.tts === "boolean") return body.tts;
  return true;
}

function parseQuestionOfDayReply(reply: string): {
  question?: string;
  facultySlug?: string;
  facultyName?: string;
} {
  const field = (name: string): string | undefined => {
    const match = reply.match(new RegExp(`^${name}:\\s*(.+)$`, "im"));
    const value = match?.[1]?.trim();
    return value || undefined;
  };
  return {
    facultySlug: field("FACULTY_SLUG"),
    facultyName: field("FACULTY_NAME"),
    question: field("QUESTION"),
  };
}

function parseAlethiometerReply(text: string): AlethiometerReply {
  const trimmed = text.trim()
    .replace(/^```(?:json)?/i, "")
    .replace(/```$/i, "")
    .trim();
  const parsed = JSON.parse(trimmed) as Partial<AlethiometerReply>;
  const question = Array.isArray(parsed.questionSymbols)
    ? parsed.questionSymbols.map((v) => Number(v))
    : [];
  const answer = Number(parsed.answerSymbol);
  const unique = new Set([...question, answer]);
  const valid = question.length === 3 &&
    question.every((v) =>
      Number.isInteger(v) && v >= 0 && v < ALETHIOMETER_SYMBOLS.length
    ) &&
    Number.isInteger(answer) && answer >= 0 &&
    answer < ALETHIOMETER_SYMBOLS.length &&
    unique.size === 4;
  const spoken = typeof parsed.spoken === "string" ? parsed.spoken.trim() : "";
  if (!valid || !spoken) {
    throw new Error("Invalid alethiometer JSON from Gemini");
  }
  return { questionSymbols: question, answerSymbol: answer, spoken };
}

function parseAlethiometerQuestionSymbols(text: string): number[] {
  const trimmed = text.trim()
    .replace(/^```(?:json)?/i, "")
    .replace(/```$/i, "")
    .trim();
  const parsed = JSON.parse(trimmed) as { questionSymbols?: unknown[] };
  const question = Array.isArray(parsed.questionSymbols)
    ? parsed.questionSymbols.map((v) => Number(v))
    : [];
  if (
    question.length !== 3 || new Set(question).size !== 3 ||
    !question.every((v) =>
      Number.isInteger(v) && v >= 0 && v < ALETHIOMETER_SYMBOLS.length
    )
  ) {
    throw new Error("Invalid alethiometer question symbols from Gemini");
  }
  return question;
}

function parseAlethiometerAnswer(
  text: string,
  questionSymbols: number[],
): AlethiometerReply {
  const trimmed = text.trim()
    .replace(/^```(?:json)?/i, "")
    .replace(/```$/i, "")
    .trim();
  const parsed = JSON.parse(trimmed) as {
    answerSymbol?: unknown;
    spoken?: unknown;
  };
  const answer = Number(parsed.answerSymbol);
  const spoken = typeof parsed.spoken === "string" ? parsed.spoken.trim() : "";
  if (
    !Number.isInteger(answer) || answer < 0 ||
    answer >= ALETHIOMETER_SYMBOLS.length ||
    questionSymbols.includes(answer) || !spoken
  ) {
    throw new Error("Invalid alethiometer answer from Gemini");
  }
  return { questionSymbols, answerSymbol: answer, spoken };
}

function alethiometerFallback(question: string): AlethiometerReply {
  let hash = 2166136261;
  for (const ch of question) {
    hash ^= ch.charCodeAt(0);
    hash = Math.imul(hash, 16777619);
  }
  const picked: number[] = [];
  while (picked.length < 4) {
    hash ^= hash << 13;
    hash ^= hash >>> 17;
    hash ^= hash << 5;
    const idx = Math.abs(hash) % ALETHIOMETER_SYMBOLS.length;
    if (!picked.includes(idx)) picked.push(idx);
  }
  const names = picked.map((idx) => ALETHIOMETER_SYMBOLS[idx].toLowerCase());
  return {
    questionSymbols: picked.slice(0, 3),
    answerSymbol: picked[3],
    spoken: `The aleithiometer sets ${names[0]}, ${names[1]}, and ${
      names[2]
    } around your question; ${names[3]} answers: move carefully, but move.`,
  };
}

function fallbackFacultySelection(text: string): FacultySelection {
  const t = text.toLowerCase();
  if (
    /(hesse|hermann|glass bead|magister ludi|steppenwolf|siddhartha|catholic|church|monastery|monastic)/
      .test(
        t,
      )
  ) {
    return { slug: "a.hesse", name: "Hermann Hesse" };
  }
  if (
    /(experiment|evidence|material|chem|lab|patient|persist|care|radi|measure)/
      .test(t)
  ) {
    return { slug: "marie-curie", name: "Marie Curie" };
  }
  if (
    /(math|philosoph|ethic|teach|civic|city|clarity|geometry|astronom)/.test(t)
  ) {
    return { slug: "hypatia", name: "Hypatia" };
  }
  if (
    /(biology|evolution|species|selection|adapt|variation|naturalist|geology|botany|beagle|origin)/
      .test(t)
  ) {
    return { slug: "a.darwin", name: "Charles Darwin" };
  }
  return { slug: "a.darwin", name: "Charles Darwin" };
}

async function selectFacultyForAsk(
  apiKey: string | undefined,
  model: string,
  message: string,
): Promise<FacultySelection> {
  if (!apiKey) return fallbackFacultySelection(message);
  const allowed = ASK_FACULTY_SELECTIONS
    .map((f) => `- ${f.slug} / ${f.name}: ${f.hints}`)
    .join("\n");
  try {
    const reply = await geminiGenerate({
      apiKey,
      model,
      systemInstruction:
        "Select the Castalia faculty member most relevant to the user's request. Return exactly two lines: " +
        "FACULTY_SLUG: <slug> and FACULTY_NAME: <name>. Use only the allowed faculty list.",
      userText: `Allowed faculty:\n${allowed}\n\nUser request:\n${message}`,
    });
    const parsed = parseQuestionOfDayReply(`${reply}\nQUESTION: placeholder`);
    if (parsed.facultySlug && parsed.facultyName) {
      const found = ASK_FACULTY_SELECTIONS.find((f) =>
        f.slug === parsed.facultySlug
      );
      if (found) return { slug: found.slug, name: found.name };
    }
  } catch (e) {
    console.warn("voice-pipeline: faculty selection failed", e);
  }
  return fallbackFacultySelection(message);
}

function headerMetaValue(s: string, maxLen: number): string {
  const t = s.trim();
  if (t.length <= maxLen) return encodeURIComponent(t);
  return encodeURIComponent(t.slice(0, maxLen));
}

function requestLocalHour(body: ReqBody): number | undefined {
  const hour = Number(body.localHour);
  if (!Number.isFinite(hour)) return undefined;
  if (hour < 0 || hour > 23) return undefined;
  return Math.floor(hour);
}

function normalizedInteractionMode(
  body: ReqBody,
): "conversation" | "transcribe" | "journal" {
  const mode = String(body.interactionMode ?? "").trim().toLowerCase();
  if (
    mode === "transcribe" || mode === "transcription" || mode === "dictation"
  ) return "transcribe";
  if (
    mode === "journal" || mode === "commonplace" || mode === "capture" ||
    mode === "note"
  ) return "journal";
  return "conversation";
}

function normalizedCommonplaceMode(
  body: ReqBody,
  defaultMode: "off" | "conversation" | "journal",
): "off" | "conversation" | "journal" {
  if (body.logToCommonplace === false) return "off";
  const raw = String(body.commonplaceMode ?? "").trim().toLowerCase();
  if (
    raw === "off" || raw === "none" || raw === "false" || raw === "disabled"
  ) return "off";
  if (
    raw === "journal" || raw === "note" || raw === "capture" ||
    raw === "commonplace"
  ) return "journal";
  if (raw === "conversation" || raw === "chat" || raw === "turn") {
    return "conversation";
  }
  if (body.logToCommonplace === true) {
    return defaultMode === "off" ? "journal" : defaultMode;
  }
  return defaultMode;
}

function earlyRouteFallbackSelection(
  transcript: string,
): FacultySelection | undefined {
  const t = transcript.toLowerCase();
  if (
    /(hesse|hermann|catholic|church|monaster|monastic|siddhartha|steppenwolf|glass bead)/
      .test(t)
  ) {
    return fallbackFacultySelection(transcript);
  }
  return undefined;
}

async function scheduleCommonplaceForVoice(
  req: Request,
  body: ReqBody,
  payload: {
    defaultMode: "off" | "conversation" | "journal";
    route: string;
    transcript: string;
    reply?: string;
    facultySlug?: string | null;
    deviceLabel?: string;
    face?: string;
  },
): Promise<boolean> {
  const mode = normalizedCommonplaceMode(body, payload.defaultMode);
  if (mode === "off") return false;
  const face = (payload.face ?? body.face ?? "").trim().toLowerCase();
  if (face === "journal" || face === "conversation") {
    scheduleLunaSayGithubLog({
      mode: face,
      transcript: payload.transcript,
      reply: payload.reply,
      face,
      route: payload.route,
    });
  }
  const authHdr = req.headers.get("Authorization") ?? "";
  const commonplacePayload = mode === "journal"
    ? {
      kind: "journal" as const,
      transcript: payload.transcript,
      deviceLabel: payload.deviceLabel ?? "Astrolabe",
    }
    : {
      kind: "conversation" as const,
      route: payload.route,
      transcript: payload.transcript,
      reply: payload.reply ?? "",
      facultySlug: payload.facultySlug,
    };
  if (body.commonplaceSync) {
    return await appendMynahCommonplaceEntry(authHdr, commonplacePayload);
  }
  if (mode === "journal") {
    scheduleMynahCommonplaceLog(authHdr, commonplacePayload);
    return true;
  }
  scheduleMynahCommonplaceLog(authHdr, commonplacePayload);
  return true;
}

function cleanFacultySlug(raw: unknown): string {
  return normalizeFacultySlug(raw);
}

function voiceFromUnknown(value: unknown): WatchTtsVoiceSelection | undefined {
  if (!value) return undefined;
  if (typeof value === "string") {
    const name = value.trim();
    return name
      ? { languageCode: languageFromVoiceName(name), name }
      : undefined;
  }
  if (typeof value !== "object" || Array.isArray(value)) return undefined;
  const r = value as Record<string, unknown>;
  const name = stringField(r.name) ||
    stringField(r.voiceName) ||
    stringField(r.voice_name) ||
    stringField(r.googleVoiceName) ||
    stringField(r.google_voice_name) ||
    stringField(r.ttsVoiceName) ||
    stringField(r.tts_voice_name);
  if (!name) return undefined;
  const languageCode = stringField(r.languageCode) ||
    stringField(r.language_code) ||
    stringField(r.googleLanguageCode) ||
    stringField(r.google_language_code) ||
    stringField(r.ttsLanguageCode) ||
    stringField(r.tts_language_code) ||
    languageFromVoiceName(name);
  return { languageCode, name };
}

function stringField(value: unknown): string {
  return typeof value === "string" ? value.trim() : "";
}

function languageFromVoiceName(name: string): string {
  const parts = name.trim().split("-");
  return parts.length >= 2 && parts[0] && parts[1]
    ? `${parts[0]}-${parts[1]}`
    : "en-US";
}

function voiceFromEnv(slug: string): WatchTtsVoiceSelection | undefined {
  if (!slug) return undefined;
  const keySlug = slug.toUpperCase().replace(/[^A-Z0-9]+/g, "_");
  const name = Deno.env.get(`FACULTY_TTS_VOICE_${keySlug}`)?.trim() ||
    Deno.env.get(`FACULTY_GOOGLE_TTS_VOICE_${keySlug}`)?.trim() ||
    "";
  if (!name) return undefined;
  const languageCode =
    Deno.env.get(`FACULTY_TTS_LANGUAGE_${keySlug}`)?.trim() ||
    Deno.env.get(`FACULTY_GOOGLE_TTS_LANGUAGE_${keySlug}`)?.trim() ||
    languageFromVoiceName(name);
  return { languageCode, name };
}

function voiceFromFacultyRow(
  row: Record<string, unknown>,
): WatchTtsVoiceSelection | undefined {
  const cfg = facultyTtsFromRow(row);
  if (!cfg.name) return undefined;
  return { languageCode: cfg.languageCode, name: cfg.name };
}

function promptFromEnv(slug: string): string | undefined {
  if (!slug) return undefined;
  const keySlug = slug.toUpperCase().replace(/[^A-Z0-9]+/g, "_");
  return (
    Deno.env.get(`FACULTY_TTS_PROMPT_${keySlug}`)?.trim() ||
    Deno.env.get(`FACULTY_GOOGLE_TTS_PROMPT_${keySlug}`)?.trim() ||
    undefined
  );
}

function facultyTtsConfigFromEnv(slug: string): FacultyTtsConfig | undefined {
  const voice = voiceFromEnv(slug);
  if (!voice) return undefined;
  const prompt = promptFromEnv(slug);
  return { ...voice, ...(prompt ? { prompt } : {}) };
}

async function resolveFacultyTtsConfig(
  slugRaw: unknown,
): Promise<FacultyTtsConfig | undefined> {
  const slug = cleanFacultySlug(slugRaw);
  if (!slug) return undefined;

  const envCfg = facultyTtsConfigFromEnv(slug);
  if (envCfg) return envCfg;

  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key) return facultyTtsFallback(slug);

  try {
    const supabase = createClient(url, key);
    const select =
      "id,slug,google_tts_voice_name,google_tts_language_code,google_tts_prompt,voice_prompt";
    let row: Record<string, unknown> | null = null;
    const byId = await supabase.from("faculty").select(select).eq("id", slug)
      .maybeSingle();
    if (!byId.error && byId.data) {
      row = byId.data as Record<string, unknown>;
    }
    if (!row) {
      const bySlug = await supabase.from("faculty").select(select).eq(
        "slug",
        slug,
      ).maybeSingle();
      if (!bySlug.error && bySlug.data) {
        row = bySlug.data as Record<string, unknown>;
      }
    }
    if (!row) return facultyTtsFallback(slug);
    return facultyTtsFromRow(row, slug);
  } catch (e) {
    console.warn("voice-pipeline: faculty TTS lookup failed", e);
    return facultyTtsFallback(slug);
  }
}

async function voicePipelineOk(
  req: Request,
  body: ReqBody,
  payload: {
    transcript: string;
    reply: string;
    route: string;
    face?: string;
    facultySlug?: string;
    facultyName?: string;
    extraHeaders?: Record<string, string>;
    extraJson?: Record<string, unknown>;
    ttsMaxChars?: number;
    spokenReply?: string;
  },
): Promise<Response> {
  await scheduleCommonplaceForVoice(req, body, {
    defaultMode: "conversation",
    route: payload.route,
    transcript: payload.transcript,
    reply: payload.reply,
    facultySlug: payload.facultySlug,
    face: payload.face,
  });

  const ttsOverride = voiceFromUnknown(body.ttsVoice) ||
    voiceFromUnknown(body.voice) ||
    voiceFromUnknown(body.ttsVoiceName) ||
    voiceFromUnknown(body.voiceName);

  const facultyTts = ttsOverride
    ? undefined
    : await resolveFacultyTtsConfig(payload.facultySlug);
  const ttsVoice = ttsOverride ?? lunaSayDailyGeminiTtsVoice(
    payload.face ?? "",
    body.deviceProfile,
  ) ??
    (facultyTts
      ? { languageCode: facultyTts.languageCode, name: facultyTts.name }
      : undefined);
  /* LunaSay has one product voice across all its faces.  It must not inherit
   * an incidental historical-faculty delivery prompt from a shared device
   * configuration. */
  const ttsPrompt = isLunaSayProfile(body.deviceProfile)
    ? LUNASAY_TTS_PROMPT
    : facultyTts?.prompt;
  const generateTts = shouldGenerateTts(req, body);

  const usageUserId = await resolveVoiceUsageUserId(req);
  const usageBase = {
    route: payload.route,
    userId: usageUserId,
    face: payload.face,
    facultySlug: payload.facultySlug,
    source: "voice-pipeline",
  };

  const ttsOptions = (localHour?: number) => ({
    localHour,
    voice: ttsVoice,
    ...(ttsPrompt ? { prompt: ttsPrompt } : {}),
    usage: usageBase,
  });

  if (!generateTts) {
    return jsonResponse(
      200,
      {
        transcript: payload.transcript,
        reply: payload.reply,
        route: payload.route,
        ...payload.extraJson,
        ...(payload.face ? { face: payload.face } : {}),
        ...(payload.facultySlug ? { facultySlug: payload.facultySlug } : {}),
        ...(payload.facultyName ? { facultyName: payload.facultyName } : {}),
      },
      {
        "x-mynah-route": payload.route,
        ...(payload.face ? { "x-mynah-face": payload.face } : {}),
        ...(payload.facultySlug
          ? { "x-faculty-slug": headerMetaValue(payload.facultySlug, 80) }
          : {}),
        ...(payload.facultyName
          ? { "x-faculty-name": headerMetaValue(payload.facultyName, 120) }
          : {}),
        ...payload.extraHeaders,
      },
    );
  }

  const { tts } = envKeys();
  if (!tts) {
    return jsonResponse(500, {
      error: "Server missing GOOGLE_TTS_API_KEY or GOOGLE_CLOUD_API_KEY",
    });
  }

  const ttsSource = payload.spokenReply ?? payload.reply;
  const spoken = capTextForWatchTts(
    ttsSource,
    payload.ttsMaxChars ?? watchTtsMaxChars(),
  );
  if (spoken.length < ttsSource.trim().length) {
    console.log(
      `voice-pipeline: TTS capped ${ttsSource.length} -> ${spoken.length} chars`,
    );
  }

  if (wantsMp3Response(req, body)) {
    const localHour = requestLocalHour(body);
    const voice = watchTtsVoiceSelection({ voice: ttsVoice });
    const ttsGate = await ensureVoiceBudget(
      req,
      voice.modelName
        ? estimateGeminiFlashTtsUsd(spoken.length, ttsPrompt?.length ?? 0)
        : estimateTtsUsd(spoken.length + (ttsPrompt?.length ?? 0), voice.name),
    );
    if (ttsGate) return ttsGate;
    const mp3 = await ttsMp3Bytes(tts, spoken, ttsOptions(localHour));
    const headers: Record<string, string> = {
      ...corsHeaders,
      "Content-Type": "audio/mpeg",
      "X-Voice-Route": payload.route,
      "X-Voice-Language": voice.languageCode,
      "X-Voice-Name": voice.name,
      "X-Voice-Transcript": headerMetaValue(payload.transcript, 300),
      "X-Voice-Reply": headerMetaValue(payload.reply, 700),
      "X-Voice-Tts-Chars": String(spoken.length),
      ...(payload.facultySlug
        ? { "X-Faculty-Slug": headerMetaValue(payload.facultySlug, 80) }
        : {}),
      ...(payload.facultyName
        ? { "X-Faculty-Name": headerMetaValue(payload.facultyName, 120) }
        : {}),
      ...payload.extraHeaders,
    };
    if (localHour !== undefined) {
      headers["X-Voice-Local-Hour"] = String(localHour);
    }
    if (payload.face) {
      headers["X-Voice-Face"] = payload.face;
    }
    const mp3Body = new Uint8Array(mp3).buffer;
    return new Response(mp3Body, { status: 200, headers });
  }

  const localHour = requestLocalHour(body);
  const voice = watchTtsVoiceSelection({ voice: ttsVoice });
  const ttsGate = await ensureVoiceBudget(
    req,
    estimateTtsUsd(spoken.length + (ttsPrompt?.length ?? 0), voice.name),
  );
  if (ttsGate) return ttsGate;
  const audioBase64 = await ttsMp3Base64(tts, spoken, ttsOptions(localHour));
  return jsonResponse(
    200,
    {
      transcript: payload.transcript,
      reply: payload.reply,
      audioBase64,
      route: payload.route,
      ...payload.extraJson,
      ...(payload.face ? { face: payload.face } : {}),
      ...(payload.facultySlug ? { facultySlug: payload.facultySlug } : {}),
      ...(payload.facultyName ? { facultyName: payload.facultyName } : {}),
    },
    {
      "x-mynah-route": payload.route,
      "x-voice-language": voice.languageCode,
      "x-voice-name": voice.name,
      ...(payload.face ? { "x-mynah-face": payload.face } : {}),
      ...(payload.facultySlug
        ? { "x-faculty-slug": headerMetaValue(payload.facultySlug, 80) }
        : {}),
      ...(payload.facultyName
        ? { "x-faculty-name": headerMetaValue(payload.facultyName, 120) }
        : {}),
      ...payload.extraHeaders,
    },
  );
}

async function forwardToAskFaculty(
  req: Request,
  payload: {
    message: string;
    languageCode: string;
    geminiModel: string;
    rawTranscript: string;
    skipLlm: boolean;
    localHour?: number;
    facultySlug?: string;
    facultyName?: string;
    conversationHistory?: string;
    /** Only forwarded when the client set `systemInstruction`; otherwise ask-faculty uses its faculty default. */
    overrideSystemInstruction?: string;
  },
): Promise<Response> {
  const url = siblingFunctionUrl("ask-faculty-voice");
  const auth = req.headers.get("Authorization") ?? "";
  const apikey = req.headers.get("apikey") ?? "";
  const body: Record<string, unknown> = {
    message: payload.message,
    languageCode: payload.languageCode,
    geminiModel: payload.geminiModel,
    rawTranscript: payload.rawTranscript,
    skipLlm: payload.skipLlm,
  };
  if (payload.localHour !== undefined) {
    body.localHour = payload.localHour;
  }
  if (payload.facultySlug) body.facultySlug = payload.facultySlug;
  if (payload.facultyName) body.facultyName = payload.facultyName;
  if (payload.conversationHistory) {
    body.conversationHistory = payload.conversationHistory;
  }
  const sys = payload.overrideSystemInstruction?.trim();
  if (sys) body.systemInstruction = sys;

  return await fetch(url, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...(auth ? { Authorization: auth } : {}),
      ...(apikey ? { apikey } : {}),
    },
    body: JSON.stringify(body),
  });
}

function decodeBase64Audio(audioBase64: string): Uint8Array {
  const b64 = audioBase64.trim();
  if (!b64) return new Uint8Array();
  return Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
}

function pcm16AudioSeconds(
  audioBase64: string,
  sampleRateHertz: number,
): number {
  const bytes = decodeBase64Audio(audioBase64).byteLength;
  return sampleRateHertz > 0 ? bytes / 2 / sampleRateHertz : 0;
}

async function ensureVoiceBudget(
  req: Request,
  pendingUsd: number,
): Promise<Response | null> {
  const gate = await checkVoiceUsageGate(req, pendingUsd);
  return gate.allowed ? null : voiceUsageGateResponse(gate);
}

async function meteredSpeechRecognize(
  req: Request,
  params: {
    apiKey: string;
    audioBase64: string;
    languageCode: string;
    sampleRateHertz: number;
    alternativeLanguageCodes?: string[];
    route: string;
    face?: string;
    facultySlug?: string;
  },
): Promise<string> {
  const audioSeconds = pcm16AudioSeconds(
    params.audioBase64,
    params.sampleRateHertz,
  );
  const transcript = await speechRecognize(
    params.apiKey,
    params.audioBase64,
    params.languageCode,
    params.sampleRateHertz,
    params.alternativeLanguageCodes,
  );
  scheduleVoiceUsageLog({
    service: "google_stt",
    route: params.route,
    userId: await resolveVoiceUsageUserId(req),
    face: params.face,
    facultySlug: params.facultySlug,
    source: "voice-pipeline",
    languageCode: params.languageCode,
    audioSeconds,
    audioBytes: Math.round(audioSeconds * params.sampleRateHertz * 2),
    estimatedUsd: estimateSttUsd(audioSeconds),
  });
  return transcript;
}

async function meteredGeminiGenerate(
  req: Request,
  params: {
    apiKey: string;
    model: string;
    systemInstruction: string;
    userText: string;
    route: string;
    face?: string;
    facultySlug?: string;
    responseMimeType?: "application/json";
    responseJsonSchema?: Record<string, unknown>;
    maxOutputTokens?: number;
    thinkingLevel?: "minimal" | "low" | "medium" | "high";
  },
): Promise<string> {
  const reply = await geminiGenerate({
    apiKey: params.apiKey,
    model: params.model,
    systemInstruction: params.systemInstruction,
    userText: params.userText,
    responseMimeType: params.responseMimeType,
    responseJsonSchema: params.responseJsonSchema,
    maxOutputTokens: params.maxOutputTokens,
    thinkingLevel: params.thinkingLevel,
  });
  const inputTokens = estimateTokensFromChars(
    params.systemInstruction.length + params.userText.length,
  );
  const outputTokens = estimateTokensFromChars(reply.length);
  scheduleVoiceUsageLog({
    service: "google_gemini",
    route: params.route,
    userId: await resolveVoiceUsageUserId(req),
    face: params.face,
    facultySlug: params.facultySlug,
    source: "voice-pipeline",
    model: params.model,
    inputTokens,
    outputTokens,
    estimatedUsd: estimateGeminiUsd(inputTokens, outputTokens),
  });
  return reply;
}

function headerMetaFromUnknown(
  value: unknown,
  maxLen: number,
): string | undefined {
  if (typeof value !== "string") return undefined;
  const trimmed = value.trim();
  return trimmed ? headerMetaValue(trimmed, maxLen) : undefined;
}

async function askFacultyPipelineResponse(
  req: Request,
  body: ReqBody,
  fr: Response,
  face?: string,
  fallbackFaculty?: FacultySelection,
): Promise<Response> {
  const text = await fr.text();
  const routeHeaders: Record<string, string> = {
    "x-mynah-route": "ask-faculty",
    ...(face ? { "x-mynah-face": face } : {}),
  };

  let faculty: AskFacultyResponse | undefined;
  try {
    faculty = JSON.parse(text) as AskFacultyResponse;
  } catch {
    // Preserve the upstream body on non-JSON error responses.
  }

  if (!fr.ok || !faculty) {
    return new Response(text, {
      status: fr.status,
      headers: {
        ...corsHeaders,
        "Content-Type": fr.headers.get("Content-Type") ?? "application/json",
        ...routeHeaders,
      },
    });
  }

  const transcript = (faculty.transcript ?? "").trim();
  const reply = (faculty.reply ?? "").trim();
  const audioBase64 = (faculty.audioBase64 ?? "").trim();
  const facultySlug = (faculty.facultySlug ?? fallbackFaculty?.slug ?? "")
    .trim();
  const facultyName = (faculty.facultyName ?? fallbackFaculty?.name ?? "")
    .trim();

  if (wantsMp3Response(req, body) && audioBase64) {
    const mp3 = decodeBase64Audio(audioBase64);
    if (mp3.length > 0) {
      const headers: Record<string, string> = {
        ...corsHeaders,
        "Content-Type": "audio/mpeg",
        "X-Voice-Route": "ask-faculty",
        "X-Voice-Tts-Source": "ask-faculty",
        "X-Voice-Tts-Chars": String(reply.length),
        ...routeHeaders,
      };
      const localHour = requestLocalHour(body);
      if (localHour !== undefined) {
        headers["X-Voice-Local-Hour"] = String(localHour);
      }
      const transcriptHeader = headerMetaFromUnknown(transcript, 300);
      if (transcriptHeader) headers["X-Voice-Transcript"] = transcriptHeader;
      const replyHeader = headerMetaFromUnknown(reply, 700);
      if (replyHeader) headers["X-Voice-Reply"] = replyHeader;
      const slugHeader = headerMetaFromUnknown(facultySlug, 80);
      if (slugHeader) headers["X-Faculty-Slug"] = slugHeader;
      const nameHeader = headerMetaFromUnknown(facultyName, 120);
      if (nameHeader) headers["X-Faculty-Name"] = nameHeader;
      if (face) headers["X-Voice-Face"] = face;

      const mp3Body = new Uint8Array(mp3).buffer;
      return new Response(mp3Body, { status: 200, headers });
    }
  }

  if (wantsMp3Response(req, body) && reply) {
    return await voicePipelineOk(req, body, {
      transcript,
      reply,
      route: "ask-faculty",
      face,
      facultySlug,
      facultyName,
      extraHeaders: {
        ...routeHeaders,
        "X-Voice-Tts-Source": "voice-pipeline-fallback",
      },
    });
  }

  if (facultySlug && !faculty.facultySlug) faculty.facultySlug = facultySlug;
  if (facultyName && !faculty.facultyName) faculty.facultyName = facultyName;

  return new Response(JSON.stringify(faculty), {
    status: fr.status,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json",
      ...routeHeaders,
    },
  });
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  const deviceAuthError = await verifyAstrolabeDevice(req);
  if (deviceAuthError) return deviceAuthError;

  let body: ReqBody;
  try {
    body = await parseVoiceRequestBody(req);
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    return jsonResponse(400, { error: msg || "Invalid request body" });
  }

  const { speech, gemini } = envKeys();
  const languageCode = (body.languageCode ?? "en-US").trim() || "en-US";
  const sampleRateHertz = body.sampleRateHertz ?? 16000;
  const geminiModel =
    (body.geminiModel ?? Deno.env.get("GEMINI_MODEL") ?? "gemini-2.5-flash")
      .trim();

  const clientSystem = (body.systemInstruction ?? "").trim();
  const defaultSystem = Deno.env.get("GEMINI_SYSTEM_INSTRUCTION")?.trim() ||
    "You are Mynah, a concise, friendly bedside assistant. Answer clearly in one or two short paragraphs unless the user asks for detail. Do not invent device actions you cannot perform.";

  const systemInstruction = clientSystem || defaultSystem;

  const face = (body.face ?? "").trim().toLowerCase();

  const audio = (body.audioBase64 ?? "").trim();
  const message = (body.message ?? "").trim();
  const ttsText = (body.ttsText ?? "").trim();

  try {
    if (
      face === LUNASAY_DAILY_PACKET_FACE ||
      (ttsText && isLunaSayProfile(body.deviceProfile))
    ) {
      const authError = await verifyAstrolabeDevice(req, true);
      if (authError) return authError;
    }
    if (ttsText) {
      return await voicePipelineOk(req, body, {
        transcript: (message || ttsText).trim(),
        reply: ttsText,
        spokenReply: ttsText,
        route: commonplaceRoute(face, "voice-pipeline"),
        face: face || undefined,
        facultySlug: body.facultySlug,
        facultyName: body.facultyName,
        extraHeaders: {
          "x-mynah-route": commonplaceRoute(face, "voice-pipeline"),
          ...(face ? { "x-mynah-face": face } : {}),
        },
      });
    }

    if (face === VOICE_FACE_CLOCK_AGENDA) {
      const epochSeconds = typeof body.epochSeconds === "number" &&
          Number.isFinite(body.epochSeconds)
        ? Math.floor(body.epochSeconds)
        : Math.floor(Date.now() / 1000);

      const transcript = await buildClockAgendaTranscript(epochSeconds);

      const route = matchAskFacultyRoute(transcript);
      if (route.kind === "ask-faculty") {
        const selection = route.selectFaculty
          ? await selectFacultyForAsk(gemini, geminiModel, route.facultyMessage)
          : undefined;
        const fr = await forwardToAskFaculty(req, {
          message: selection
            ? `${selection.name}: ${route.facultyMessage}`
            : route.facultyMessage,
          languageCode,
          geminiModel,
          rawTranscript: transcript,
          skipLlm: body.skipLlm ?? false,
          localHour: requestLocalHour(body),
          facultySlug: selection?.slug,
          facultyName: selection?.name,
          overrideSystemInstruction: clientSystem || undefined,
        });
        return await askFacultyPipelineResponse(
          req,
          body,
          fr,
          VOICE_FACE_CLOCK_AGENDA,
          selection,
        );
      }

      const skipLlm = body.skipLlm !== false;
      let reply: string;
      if (skipLlm || !gemini) {
        reply = transcript;
      } else {
        const sys = clientSystem || SYSTEM_VOICE_FACE_CLOCK_AGENDA;
        const inputTokens = estimateTokensFromChars(
          sys.length + transcript.length,
        );
        const geminiGate = await ensureVoiceBudget(
          req,
          estimateGeminiUsd(inputTokens, 1024),
        );
        if (geminiGate) return geminiGate;
        reply = await meteredGeminiGenerate(req, {
          apiKey: gemini,
          model: geminiModel,
          systemInstruction: sys,
          userText: transcript,
          route: "voice-pipeline",
          face: VOICE_FACE_CLOCK_AGENDA,
        });
      }

      return await voicePipelineOk(req, body, {
        transcript,
        reply,
        route: "voice-pipeline",
        face: VOICE_FACE_CLOCK_AGENDA,
        extraHeaders: {
          "x-mynah-route": "voice-pipeline",
          "x-mynah-face": VOICE_FACE_CLOCK_AGENDA,
        },
      });
    }

    if (face === VOICE_FACE_DAILY_BRIEFING) {
      const epochSeconds = typeof body.epochSeconds === "number" &&
          Number.isFinite(body.epochSeconds)
        ? Math.floor(body.epochSeconds)
        : Math.floor(Date.now() / 1000);
      const deviceFacts = (body.briefingFacts ?? "").trim();
      const transcript = await buildDailyBriefingTranscript({
        epochSeconds,
        deviceFacts,
      });
      const userText = (message ||
        "Deliver today's Mynah Astrolabe daily briefing now from the facts. " +
          "Speak for about three to five minutes in one flowing narrative.")
        .trim();
      const sys = clientSystem || SYSTEM_VOICE_FACE_DAILY_BRIEFING;
      let reply = transcript;
      if (gemini) {
        const input = `${userText}\n\n${transcript}`;
        const inputTokens = estimateTokensFromChars(sys.length + input.length);
        const geminiGate = await ensureVoiceBudget(
          req,
          estimateGeminiUsd(inputTokens, 2048),
        );
        if (geminiGate) return geminiGate;
        reply = await meteredGeminiGenerate(req, {
          apiKey: gemini,
          model: geminiModel,
          systemInstruction: sys,
          userText: input,
          route: "voice-pipeline",
          face: VOICE_FACE_DAILY_BRIEFING,
        });
      }
      return await voicePipelineOk(req, body, {
        transcript,
        reply,
        route: "voice-pipeline",
        face: VOICE_FACE_DAILY_BRIEFING,
        ttsMaxChars: watchTtsMaxCharsDailyBriefing(),
        extraHeaders: {
          "x-mynah-route": "voice-pipeline",
          "x-mynah-face": VOICE_FACE_DAILY_BRIEFING,
        },
      });
    }

    if (face === LUNASAY_DAILY_PACKET_FACE) {
      if (!gemini) {
        return jsonResponse(500, {
          error:
            "Server missing GOOGLE_GEMINI_API_KEY, GOOGLE_AI_API_KEY, or GOOGLE_CLOUD_API_KEY",
        });
      }
      const epochSeconds = typeof body.epochSeconds === "number" &&
          Number.isFinite(body.epochSeconds)
        ? Math.floor(body.epochSeconds)
        : Math.floor(Date.now() / 1000);
      const timezone = normalizeLunaSayTimezone(body.timezone);
      const dailyPacketModel = (body.geminiModel ??
        Deno.env.get("GEMINI_LUNASAY_DAILY_MODEL") ?? "gemini-2.5-flash")
        .trim();
      const date = lunaSayDateForEpoch(epochSeconds, timezone);
      const facts = await buildDailyBriefingTranscript({
        epochSeconds,
        deviceFacts: (body.briefingFacts ?? "").trim(),
      });
      const generationMode =
        Deno.env.get("LUNASAY_DAILY_GENERATION_MODE")?.trim().toLowerCase() ===
            "packet"
          ? "packet"
          : "per_face";
      if (generationMode === "per_face") {
        const perFaceModel =
          Deno.env.get("GEMINI_LUNASAY_FACE_MODEL")?.trim() ||
          "gemini-2.5-flash";
        const generatedFaceIds = LUNASAY_GENERATED_DAILY_FACE_IDS;
        const prompts = generatedFaceIds.map((id) => {
          let systemInstruction = buildLunaSayDailyFaceInstruction({
            id,
            date,
            timezone,
          });
          const requiredEvidence = lunaSayRequiredEvidence(id, facts);
          const requiredWeatherEvidence = lunaSayRequiredWeatherEvidence(
            id,
            facts,
          );
          const requiredTemporalEvidence = lunaSayRequiredTemporalEvidence(
            id,
            facts,
          );
          if (requiredEvidence) {
            systemInstruction += ` For this request, set evidence exactly to ${
              JSON.stringify(requiredEvidence)
            }.`;
          }
          if (requiredWeatherEvidence) {
            systemInstruction += ` Set weatherEvidence exactly to ${
              JSON.stringify(requiredWeatherEvidence)
            }.`;
          }
          if (requiredTemporalEvidence) {
            systemInstruction += ` Set temporalEvidence exactly to ${
              JSON.stringify(requiredTemporalEvidence)
            }. Preserve every day offset in now and next; do not invent a calendar date or event.`;
          }
          const focusedFacts = lunaSayFocusedFacts(id, facts);
          const userText =
            `DATE CONTEXT:\n${date} (${timezone})\n\nDAILY FACTS:\n${
              focusedFacts || facts
            }`;
          return {
            id,
            systemInstruction,
            userText,
            requiredEvidence,
            requiredWeatherEvidence,
            requiredTemporalEvidence,
          };
        });
        const inputTokens = prompts.reduce(
          (total, prompt) =>
            total +
            estimateTokensFromChars(
              prompt.systemInstruction.length + prompt.userText.length,
            ),
          0,
        );
        const geminiGate = await ensureVoiceBudget(
          req,
          estimateGeminiUsd(
            inputTokens * 2,
            generatedFaceIds.length * 2 * 1024,
          ),
        );
        if (geminiGate) return geminiGate;

        const fallbackPacket = lunaSayDailyPacketFallback({
          date,
          timezone,
          facts,
        });
        const faces = { ...fallbackPacket.faces };
        const faceFallbacks: LunaSayDailyFaceId[] = [];
        const faceFallbackReasons: Partial<
          Record<LunaSayDailyFaceId, string>
        > = {};
        let llmCalls = 0;
        const results = await Promise.all(prompts.map(async (prompt) => {
          let lastReason = "unknown validation failure";
          for (let attempt = 0; attempt < 2; attempt++) {
            try {
              llmCalls++;
              const raw = await meteredGeminiGenerate(req, {
                apiKey: gemini,
                model: perFaceModel,
                systemInstruction: attempt === 0
                  ? prompt.systemInstruction
                  : `${prompt.systemInstruction} RETRY: The prior response failed server validation: ${lastReason}. Correct that exact defect; do not relax or reinterpret the evidence contract.`,
                userText: prompt.userText,
                route: LUNASAY_DAILY_PACKET_FACE,
                face: prompt.id,
                responseMimeType: "application/json",
                responseJsonSchema: lunaSayDailyFaceJsonSchema(prompt.id),
                /* Gemini 2.5 may consume substantial generation headroom even
                 * with thinking disabled. The JSON schema—not this ceiling—
                 * keeps the billable visible response short. */
                maxOutputTokens: 4096,
                ...(perFaceModel.startsWith("gemini-2.5")
                  ? { thinkingBudget: 0 }
                  : {}),
                ...(perFaceModel.startsWith("gemini-3")
                  ? { thinkingLevel: "minimal" as const }
                  : {}),
              });
              const parsedFace = parseLunaSayDailyFace(
                raw,
                prompt.id,
                date,
                facts,
              );
              if (
                prompt.requiredEvidence &&
                parsedFace.evidence !== prompt.requiredEvidence
              ) {
                throw new Error("wrong-required-evidence");
              }
              if (
                prompt.requiredWeatherEvidence &&
                parsedFace.weatherEvidence !== prompt.requiredWeatherEvidence
              ) {
                throw new Error("wrong-required-weather-evidence");
              }
              if (
                prompt.requiredTemporalEvidence &&
                parsedFace.temporalEvidence !==
                  prompt.requiredTemporalEvidence
              ) {
                throw new Error("wrong-required-temporal-evidence");
              }
              return { id: prompt.id, face: parsedFace };
            } catch (error) {
              lastReason =
                (error instanceof Error ? error.message : String(error))
                  .replace(/\s+/g, " ")
                  .slice(0, 180);
            }
          }
          console.error(
            `voice-pipeline: invalid LunaSay daily face ${prompt.id}`,
            lastReason,
          );
          return { id: prompt.id, error: lastReason };
        }));
        for (const result of results) {
          if ("face" in result && result.face) {
            faces[result.id] = result.face;
          } else {
            faceFallbacks.push(result.id);
            faceFallbackReasons[result.id] = result.error;
          }
        }
        const packet = {
          ...fallbackPacket,
          generatedAt: new Date().toISOString(),
          faces,
        };
        return jsonResponse(200, {
          packet,
          route: LUNASAY_DAILY_PACKET_FACE,
          cacheKey: `lunasay:${date}:${timezone}:v2`,
          tts: "on_demand",
          generationMode,
          model: perFaceModel,
          llmCalls,
          ...(faceFallbacks.length
            ? { fallback: true, faceFallbacks, faceFallbackReasons }
            : {}),
        }, {
          "x-mynah-route": LUNASAY_DAILY_PACKET_FACE,
          "x-mynah-face": LUNASAY_DAILY_PACKET_FACE,
          "x-lunasay-date": date,
          "x-lunasay-timezone": timezone,
          "x-lunasay-llm-calls": String(llmCalls),
          "x-lunasay-generation-mode": generationMode,
        });
      }
      const sys = buildLunaSayDailyPacketInstruction({ date, timezone });
      const input =
        `DATE CONTEXT:\n${date} (${timezone})\n\nDAILY FACTS:\n${facts}`;
      const inputTokens = estimateTokensFromChars(sys.length + input.length);
      const geminiGate = await ensureVoiceBudget(
        req,
        estimateGeminiUsd(inputTokens, 8192),
      );
      if (geminiGate) return geminiGate;
      try {
        const raw = await meteredGeminiGenerate(req, {
          apiKey: gemini,
          model: dailyPacketModel,
          systemInstruction: sys,
          userText: input,
          route: LUNASAY_DAILY_PACKET_FACE,
          face,
          responseMimeType: "application/json",
          responseJsonSchema: lunaSayDailyPacketJsonSchema(),
          maxOutputTokens: 8192,
          ...(dailyPacketModel.startsWith("gemini-3")
            ? { thinkingLevel: "minimal" as const }
            : {}),
        });
        const packet = parseLunaSayDailyPacket(raw, {
          date,
          timezone,
          facts,
        });
        return jsonResponse(200, {
          packet,
          route: LUNASAY_DAILY_PACKET_FACE,
          cacheKey: `lunasay:${date}:${timezone}:v1`,
          tts: "on_demand",
        }, {
          "x-mynah-route": LUNASAY_DAILY_PACKET_FACE,
          "x-mynah-face": LUNASAY_DAILY_PACKET_FACE,
          "x-lunasay-date": date,
          "x-lunasay-timezone": timezone,
          "x-lunasay-llm-calls": "1",
        });
      } catch (e) {
        const fallbackReason = (e instanceof Error ? e.message : String(e))
          .replace(/\s+/g, " ")
          .slice(0, 180);
        console.error(
          "voice-pipeline: invalid LunaSay daily packet",
          fallbackReason,
        );
        const packet = lunaSayDailyPacketFallback({
          date,
          timezone,
          facts,
        });
        return jsonResponse(200, {
          packet,
          route: LUNASAY_DAILY_PACKET_FACE,
          cacheKey: `lunasay:${date}:${timezone}:v1`,
          tts: "on_demand",
          fallback: true,
          fallbackReason,
        });
      }
    }

    let transcript = "";
    const interactionMode = normalizedInteractionMode(body);

    if (audio) {
      if (!speech) {
        return jsonResponse(500, {
          error: "Server missing GOOGLE_SPEECH_API_KEY or GOOGLE_CLOUD_API_KEY",
        });
      }
      const sttRoute = interactionMode === "journal"
        ? "voice-journal"
        : interactionMode === "transcribe"
        ? "voice-transcribe"
        : "voice-pipeline";
      const audioSeconds = pcm16AudioSeconds(audio, sampleRateHertz);
      const sttGate = await ensureVoiceBudget(
        req,
        estimateSttUsd(audioSeconds),
      );
      if (sttGate) return sttGate;
      transcript = await meteredSpeechRecognize(req, {
        apiKey: speech,
        audioBase64: audio,
        languageCode,
        sampleRateHertz,
        alternativeLanguageCodes: face === VOICE_FACE_BABEL_FISH
          ? babelFishAlternativeLanguageCodes(body)
          : undefined,
        route: sttRoute,
        face: face || undefined,
        facultySlug: body.facultySlug,
      });
      if (!transcript) {
        return jsonResponse(422, {
          error: "No speech detected",
          transcript: "",
          reply: "",
          audioBase64: "",
        });
      }
    } else if (message) {
      transcript = message;
    } else {
      return jsonResponse(400, {
        error:
          "Provide audioBase64, message, or face (e.g. clock_agenda, daily_briefing, or lunasay_daily_packet with optional epochSeconds)",
      });
    }

    if (interactionMode !== "conversation") {
      const route = interactionMode === "journal"
        ? "voice-journal"
        : "voice-transcribe";
      const askRoute = matchAskFacultyRoute(transcript);
      const selection =
        askRoute.kind === "ask-faculty" && askRoute.selectFaculty
          ? await selectFacultyForAsk(
            gemini,
            geminiModel,
            askRoute.facultyMessage,
          )
          : body.earlyRoute === true
          ? earlyRouteFallbackSelection(transcript)
          : undefined;
      const facultySlug = selection?.slug ?? body.facultySlug;
      const facultyName = selection?.name ?? body.facultyName;
      const commonplaceLogged = await scheduleCommonplaceForVoice(req, body, {
        defaultMode: interactionMode === "journal" ? "journal" : "off",
        route,
        transcript,
        reply: "",
        facultySlug,
        deviceLabel: "Astrolabe FacultyAtom",
        face,
      });
      return jsonResponse(
        200,
        {
          transcript,
          reply: "",
          audioBase64: "",
          route,
          interactionMode,
          commonplaceLogged,
          ...(askRoute.kind === "ask-faculty"
            ? { askFacultyRoute: true, earlyRoute: body.earlyRoute === true }
            : {}),
          ...(facultySlug ? { facultySlug } : {}),
          ...(facultyName ? { facultyName } : {}),
        },
        {
          "x-mynah-route": route,
          "x-voice-interaction-mode": interactionMode,
          "x-voice-commonplace-logged": String(commonplaceLogged),
          ...(face ? { "x-mynah-face": face } : {}),
        },
      );
    }

    const route = matchAskFacultyRoute(transcript);
    if (route.kind === "ask-faculty") {
      const selection = route.selectFaculty
        ? await selectFacultyForAsk(gemini, geminiModel, route.facultyMessage)
        : undefined;
      const fr = await forwardToAskFaculty(req, {
        message: selection
          ? `${selection.name}: ${route.facultyMessage}`
          : route.facultyMessage,
        languageCode,
        geminiModel,
        rawTranscript: transcript,
        skipLlm: body.skipLlm ?? false,
        localHour: requestLocalHour(body),
        facultySlug: selection?.slug,
        facultyName: selection?.name,
        overrideSystemInstruction: clientSystem || undefined,
      });
      return await askFacultyPipelineResponse(
        req,
        body,
        fr,
        face || undefined,
        selection,
      );
    }

    const activeSlug = cleanFacultySlug(body.facultySlug);
    const activeName = (body.facultyName ?? "").trim();
    if (isDedicatedFacultyFace(face) && activeSlug) {
      const fallback: FacultySelection = {
        slug: activeSlug,
        name: activeName || activeSlug,
      };
      const history = (body.conversationHistory ?? "").trim();
      const fr = await forwardToAskFaculty(req, {
        message: transcript,
        languageCode,
        geminiModel,
        rawTranscript: transcript,
        skipLlm: body.skipLlm ?? false,
        localHour: requestLocalHour(body),
        facultySlug: activeSlug,
        facultyName: activeName || undefined,
        conversationHistory: history || undefined,
      });
      return await askFacultyPipelineResponse(
        req,
        body,
        fr,
        face || undefined,
        fallback,
      );
    }

    if (face === VOICE_FACE_ALETHIOMETER || face === VOICE_FACE_CRYSTAL_BALL) {
      if (!gemini) {
        return jsonResponse(500, {
          error:
            "Server missing GOOGLE_GEMINI_API_KEY, GOOGLE_AI_API_KEY, or GOOGLE_CLOUD_API_KEY",
        });
      }
      const alethPrompt = clientSystem || systemInstruction;
      if (face === VOICE_FACE_ALETHIOMETER) {
        const symbolTable = ALETHIOMETER_SYMBOLS.map((name, idx) =>
          `${idx} ${name}`
        ).join(", ");
        const questionPrompt =
          "You are the first stage of an alethiometer reading. Interpret the user's exact question and choose exactly three distinct symbols that encode the question, not its answer. " +
          `Valid symbols are: ${symbolTable}. ` +
          'Return only strict minified JSON using {"questionSymbols":[number,number,number]}.';
        const questionInputTokens = estimateTokensFromChars(
          questionPrompt.length + transcript.length,
        );
        const questionGate = await ensureVoiceBudget(
          req,
          estimateGeminiUsd(questionInputTokens, 192),
        );
        if (questionGate) return questionGate;

        let questionSymbols: number[];
        try {
          const questionRaw = await meteredGeminiGenerate(req, {
            apiKey: gemini,
            model: geminiModel,
            systemInstruction: questionPrompt,
            userText: transcript,
            route: `${face}:question`,
            face,
            facultySlug: body.facultySlug,
          });
          questionSymbols = parseAlethiometerQuestionSymbols(questionRaw);
        } catch (e) {
          console.warn(
            "voice-pipeline: alethiometer question-symbol call failed; using deterministic symbols",
            e instanceof Error ? e.message : String(e),
          );
          questionSymbols = alethiometerFallback(transcript).questionSymbols;
        }

        const fixedSymbols = questionSymbols.map((idx) =>
          `${idx} ${ALETHIOMETER_SYMBOLS[idx]}`
        ).join(", ");
        const answerPrompt =
          "You are the second stage of an alethiometer reading. The three question hands are already fixed and must not change. Choose one distinct answer symbol and interpret it as a concise answer to the exact question. " +
          `The fixed question symbols are ${fixedSymbols}. Valid symbols are: ${symbolTable}. ` +
          'Return only strict minified JSON using {"answerSymbol":number,"spoken":"one or two concise spoken sentences"}.';
        const answerUserText = `Exact question: ${transcript}`;
        const answerInputTokens = estimateTokensFromChars(
          answerPrompt.length + answerUserText.length,
        );
        const answerGate = await ensureVoiceBudget(
          req,
          estimateGeminiUsd(answerInputTokens, 384),
        );
        if (answerGate) return answerGate;

        let reading: AlethiometerReply;
        try {
          const answerRaw = await meteredGeminiGenerate(req, {
            apiKey: gemini,
            model: geminiModel,
            systemInstruction: answerPrompt,
            userText: answerUserText,
            route: `${face}:answer`,
            face,
            facultySlug: body.facultySlug,
          });
          reading = parseAlethiometerAnswer(answerRaw, questionSymbols);
        } catch (e) {
          console.warn(
            "voice-pipeline: alethiometer answer call failed; using deterministic answer",
            e instanceof Error ? e.message : String(e),
          );
          const fallback = alethiometerFallback(transcript);
          let answer = fallback.answerSymbol;
          while (questionSymbols.includes(answer)) {
            answer = (answer + 1) % ALETHIOMETER_SYMBOLS.length;
          }
          reading = {
            questionSymbols,
            answerSymbol: answer,
            spoken:
              `With ${fixedSymbols.toLowerCase()} set around your question, ${
                ALETHIOMETER_SYMBOLS[answer].toLowerCase()
              } answers: move carefully, but move.`,
          };
        }
        const reply = JSON.stringify(reading);
        return await voicePipelineOk(req, body, {
          transcript,
          reply,
          spokenReply: reading.spoken,
          route: VOICE_FACE_ALETHIOMETER,
          face,
          extraJson: {
            alethiometer: {
              questionSymbols: reading.questionSymbols,
              questionSymbolNames: reading.questionSymbols.map((idx) =>
                ALETHIOMETER_SYMBOLS[idx]
              ),
              answerSymbol: reading.answerSymbol,
              answerSymbolName: ALETHIOMETER_SYMBOLS[reading.answerSymbol],
              spoken: reading.spoken,
              llmCalls: 2,
            },
          },
          extraHeaders: {
            "x-mynah-route": VOICE_FACE_ALETHIOMETER,
            "x-alethiometer-question-symbols": reading.questionSymbols.join(
              ",",
            ),
            "x-alethiometer-answer-symbol": String(reading.answerSymbol),
            "x-alethiometer-llm-calls": "2",
          },
        });
      }
      const inputTokens = estimateTokensFromChars(
        alethPrompt.length + transcript.length,
      );
      const geminiGate = await ensureVoiceBudget(
        req,
        estimateGeminiUsd(inputTokens, 384),
      );
      if (geminiGate) return geminiGate;
      let reading: AlethiometerReply;
      try {
        const raw = await meteredGeminiGenerate(req, {
          apiKey: gemini,
          model: geminiModel,
          systemInstruction: alethPrompt,
          userText: transcript,
          route: face,
          face,
          facultySlug: body.facultySlug,
        });
        reading = parseAlethiometerReply(raw);
      } catch (e) {
        console.warn(
          "voice-pipeline: alethiometer JSON failed; using deterministic fallback",
          e instanceof Error ? e.message : String(e),
        );
        reading = alethiometerFallback(transcript);
      }
      const reply = JSON.stringify(reading);
      const oracleRoute = face === VOICE_FACE_CRYSTAL_BALL
        ? VOICE_FACE_CRYSTAL_BALL
        : VOICE_FACE_ALETHIOMETER;
      return await voicePipelineOk(req, body, {
        transcript,
        reply,
        spokenReply: reading.spoken,
        route: oracleRoute,
        face,
        extraJson: {
          alethiometer: {
            questionSymbols: reading.questionSymbols,
            questionSymbolNames: reading.questionSymbols.map((idx) =>
              ALETHIOMETER_SYMBOLS[idx]
            ),
            answerSymbol: reading.answerSymbol,
            answerSymbolName: ALETHIOMETER_SYMBOLS[reading.answerSymbol],
            spoken: reading.spoken,
          },
        },
        extraHeaders: {
          "x-mynah-route": oracleRoute,
          "x-alethiometer-question-symbols": reading.questionSymbols.join(","),
          "x-alethiometer-answer-symbol": String(reading.answerSymbol),
        },
      });
    }

    let reply: string;
    if (body.skipLlm) {
      reply = transcript;
    } else {
      if (!gemini) {
        return jsonResponse(500, {
          error:
            "Server missing GOOGLE_GEMINI_API_KEY, GOOGLE_AI_API_KEY, or GOOGLE_CLOUD_API_KEY",
        });
      }
      const inputTokens = estimateTokensFromChars(
        systemInstruction.length + transcript.length,
      );
      const geminiGate = await ensureVoiceBudget(
        req,
        estimateGeminiUsd(inputTokens, 1024),
      );
      if (geminiGate) return geminiGate;
      reply = await meteredGeminiGenerate(req, {
        apiKey: gemini,
        model: geminiModel,
        systemInstruction,
        userText: transcript,
        route: commonplaceRoute(face, "voice-pipeline"),
        face: face || undefined,
        facultySlug: body.facultySlug,
      });
    }

    const qotd = commonplaceRoute(face, "voice-pipeline") === "question_of_day"
      ? parseQuestionOfDayReply(reply)
      : {};

    return await voicePipelineOk(req, body, {
      transcript,
      reply,
      spokenReply: qotd.question,
      route: commonplaceRoute(face, "voice-pipeline"),
      face: face || undefined,
      facultySlug: qotd.facultySlug ?? body.facultySlug,
      facultyName: qotd.facultyName ?? body.facultyName,
      extraHeaders: {
        "x-mynah-route": commonplaceRoute(face, "voice-pipeline"),
        ...(face ? { "x-mynah-face": face } : {}),
      },
    });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.error("voice-pipeline error:", msg);
    return jsonResponse(500, { error: msg });
  }
});
