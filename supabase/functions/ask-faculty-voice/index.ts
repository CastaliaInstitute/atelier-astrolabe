import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { createClient } from "npm:@supabase/supabase-js@2.49.8";

import {
  capTextForWatchTts,
  corsHeaders,
  envKeys,
  geminiGenerate,
  jsonResponse,
  ttsMp3Base64,
  watchTtsVoiceSelection,
} from "../_shared/googleVoice.ts";
import {
  inferFacultySlug,
  signedFacultyBustUrl,
} from "../_shared/facultyBust.ts";
import { normalizeFacultySlug } from "../_shared/askFacultyRoute.ts";
import {
  type FacultyTtsConfig,
  facultyTtsFallback,
  facultyTtsFromRow,
} from "../_shared/facultyTts.ts";
import {
  fetchFacultyMemoryContext,
  scheduleMynahCommonplaceLog,
} from "../_shared/commonplaceDirectus.ts";
import {
  checkVoiceUsageGate,
  estimateGeminiUsd,
  estimateTokensFromChars,
  estimateTtsUsd,
  resolveVoiceUsageUserId,
  scheduleVoiceUsageLog,
  voiceUsageGateResponse,
} from "../_shared/voiceUsage.ts";

type AskFacultyBody = {
  message: string;
  languageCode?: string;
  geminiModel?: string;
  responseFormat?: "json" | "text" | "mp3";
  generateTts?: boolean;
  tts?: boolean;
  commonplaceMode?: "off" | "conversation" | "journal";
  logToCommonplace?: boolean;
  rawTranscript?: string;
  systemInstruction?: string;
  skipLlm?: boolean;
  facultySlug?: string;
  facultyName?: string;
  localHour?: number;
  /** Prior turns from the device; appended to the LLM user message, not the faculty system prompt. */
  conversationHistory?: string;
};

function stringField(value: unknown): string {
  return typeof value === "string" ? value.trim() : "";
}

function defaultFacultySystem(name: string): string {
  return (
    `You are ${name}, a Castalia faculty member speaking on Astrolabe or Mynah. ` +
    "Answer in your authentic voice and historical frame. Keep replies concise and " +
    "spoken-friendly (under ~25 seconds). Do not invent device actions."
  );
}

async function loadFacultyRow(
  slug: string,
): Promise<Record<string, unknown> | null> {
  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key || !slug) return null;

  const supabase = createClient(url, key);
  const select =
    "id,slug,name,google_tts_voice_name,google_tts_language_code,google_tts_prompt,voice_prompt,agent_persona";
  const byId = await supabase.from("faculty").select(select).eq("id", slug)
    .maybeSingle();
  if (!byId.error && byId.data) return byId.data as Record<string, unknown>;
  const bySlug = await supabase.from("faculty").select(select).eq("slug", slug)
    .maybeSingle();
  if (!bySlug.error && bySlug.data) {
    return bySlug.data as Record<string, unknown>;
  }
  return null;
}

function facultySystemInstruction(
  row: Record<string, unknown> | null,
  facultyName: string,
  override?: string,
): string {
  const client = override?.trim();
  if (client) return client;
  const voicePrompt = stringField(row?.voice_prompt);
  if (voicePrompt) return voicePrompt;
  const persona = stringField(row?.agent_persona);
  if (persona) return persona;
  return defaultFacultySystem(facultyName);
}

function resolveFacultyIdentity(
  body: AskFacultyBody,
  row: Record<string, unknown> | null,
  inferredSlug: string,
): { slug: string; name: string } {
  const slug = normalizeFacultySlug(body.facultySlug) ||
    stringField(row?.id) ||
    stringField(row?.slug) ||
    inferredSlug;
  const name = stringField(body.facultyName) ||
    stringField(row?.name) ||
    slug.replace(/^a\./, "").replace(/[.-]/g, " ") ||
    "Faculty";
  return { slug, name };
}

function shouldGenerateTts(req: Request, body: AskFacultyBody): boolean {
  if (typeof body.generateTts === "boolean") return body.generateTts;
  if (typeof body.tts === "boolean") return body.tts;
  const fmt = String(body.responseFormat ?? "").trim().toLowerCase();
  if (fmt === "text") return false;
  if (fmt === "mp3") return true;
  const accept = (req.headers.get("Accept") ?? "").toLowerCase();
  if (accept.includes("text/plain")) return false;
  return true;
}

async function ensureVoiceBudget(
  req: Request,
  pendingUsd: number,
): Promise<Response | null> {
  const gate = await checkVoiceUsageGate(req, pendingUsd);
  return gate.allowed ? null : voiceUsageGateResponse(gate);
}

function shouldLogCommonplace(body: AskFacultyBody): boolean {
  if (body.logToCommonplace === false) return false;
  const mode = String(body.commonplaceMode ?? "").trim().toLowerCase();
  if (
    mode === "off" || mode === "none" || mode === "false" || mode === "disabled"
  ) return false;
  return true;
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  let body: AskFacultyBody;
  try {
    body = (await req.json()) as AskFacultyBody;
  } catch {
    return jsonResponse(400, { error: "Invalid JSON body" });
  }

  const message = (body.message ?? "").trim();
  if (!message) {
    return jsonResponse(400, { error: "message is required" });
  }

  const { gemini, tts } = envKeys();
  const geminiModel =
    (body.geminiModel ?? Deno.env.get("GEMINI_MODEL") ?? "gemini-2.5-flash")
      .trim();

  const inferredSlug = inferFacultySlug(message);
  const preSlug = normalizeFacultySlug(body.facultySlug) || inferredSlug;
  const row = preSlug ? await loadFacultyRow(preSlug) : null;
  const { slug: facultySlug, name: facultyName } = resolveFacultyIdentity(
    body,
    row,
    inferredSlug,
  );

  const systemInstruction = facultySystemInstruction(
    row,
    facultyName,
    body.systemInstruction,
  );

  const rawTranscript = (body.rawTranscript ?? "").trim();
  const transcript = rawTranscript || message;

  try {
    let reply: string;
    if (body.skipLlm) {
      reply = message;
    } else {
      if (!gemini) {
        return jsonResponse(500, {
          error:
            "Server missing GOOGLE_GEMINI_API_KEY, GOOGLE_AI_API_KEY, or GOOGLE_CLOUD_API_KEY",
        });
      }
      const history = stringField(body.conversationHistory);
      const authHdr = req.headers.get("Authorization") ?? "";
      const memoryContext = await fetchFacultyMemoryContext(
        authHdr,
        facultySlug,
        rawTranscript || message,
      );
      let userText = rawTranscript && rawTranscript !== message
        ? `User said (full utterance): ${rawTranscript}\n\nFaculty-focused request: ${message}`
        : message;
      if (history) {
        userText =
          `Recent conversation on this device:\n${history}\n\nCurrent user request:\n${userText}`;
      }
      if (memoryContext) {
        userText =
          `Relevant prior Commonplace memories for this user and faculty:\n${memoryContext}\n\n` +
          `Current exchange:\n${userText}`;
      }
      const inputTokensEstimate = estimateTokensFromChars(
        systemInstruction.length + userText.length,
      );
      const geminiGate = await ensureVoiceBudget(
        req,
        estimateGeminiUsd(inputTokensEstimate, 1024),
      );
      if (geminiGate) return geminiGate;
      reply = await geminiGenerate({
        apiKey: gemini,
        model: geminiModel,
        systemInstruction,
        userText,
      });
      const outputTokens = estimateTokensFromChars(reply.length);
      scheduleVoiceUsageLog({
        service: "google_gemini",
        route: "ask-faculty",
        userId: await resolveVoiceUsageUserId(req),
        facultySlug,
        source: "ask-faculty-voice",
        model: geminiModel,
        inputTokens: inputTokensEstimate,
        outputTokens,
        estimatedUsd: estimateGeminiUsd(inputTokensEstimate, outputTokens),
      });
    }

    const wantsTts = shouldGenerateTts(req, body);
    let audioBase64 = "";
    let voiceHeaders: Record<string, string> = {};
    if (wantsTts) {
      if (!tts) {
        return jsonResponse(500, {
          error: "Server missing GOOGLE_TTS_API_KEY or GOOGLE_CLOUD_API_KEY",
        });
      }

      const ttsCfg: FacultyTtsConfig = row
        ? facultyTtsFromRow(row, facultySlug)
        : (facultyTtsFallback(facultySlug) ??
          facultyTtsFromRow(null, facultySlug));

      const localHour = Number(body.localHour);
      const hour =
        Number.isFinite(localHour) && localHour >= 0 && localHour <= 23
          ? Math.floor(localHour)
          : undefined;

      const usageUserId = await resolveVoiceUsageUserId(req);
      const spoken = capTextForWatchTts(reply);
      const ttsGate = await ensureVoiceBudget(
        req,
        estimateTtsUsd(
          spoken.length + (ttsCfg.prompt?.length ?? 0),
          ttsCfg.name,
        ),
      );
      if (ttsGate) return ttsGate;
      audioBase64 = await ttsMp3Base64(tts, spoken, {
        localHour: hour,
        voice: { languageCode: ttsCfg.languageCode, name: ttsCfg.name },
        ...(ttsCfg.prompt ? { prompt: ttsCfg.prompt } : {}),
        usage: {
          route: "ask-faculty",
          userId: usageUserId,
          facultySlug,
          source: "ask-faculty-voice",
        },
      });

      const voice = watchTtsVoiceSelection({
        voice: { languageCode: ttsCfg.languageCode, name: ttsCfg.name },
      });
      voiceHeaders = {
        "x-voice-language": voice.languageCode,
        "x-voice-name": voice.name,
      };
    }

    let facultyBustUrl: string | null = null;
    try {
      const bust = await signedFacultyBustUrl(facultySlug);
      facultyBustUrl = bust?.url ?? null;
    } catch (e) {
      console.warn("ask-faculty bust URL:", e);
    }

    const authHdr = req.headers.get("Authorization") ?? "";
    if (shouldLogCommonplace(body)) {
      scheduleMynahCommonplaceLog(authHdr, {
        kind: "conversation",
        route: "ask-faculty",
        transcript,
        reply,
        facultySlug,
        facultyName,
      });
    }

    if (String(body.responseFormat ?? "").trim().toLowerCase() === "text") {
      return new Response(reply, {
        status: 200,
        headers: {
          ...corsHeaders,
          "Content-Type": "text/plain; charset=utf-8",
          "x-mynah-route": "ask-faculty",
          "x-faculty-slug": facultySlug,
          "x-faculty-name": facultyName,
          "x-voice-tts-generated": String(wantsTts),
          ...voiceHeaders,
        },
      });
    }

    return jsonResponse(
      200,
      {
        transcript,
        reply,
        ...(wantsTts ? { audioBase64 } : {}),
        route: "ask-faculty",
        facultySlug,
        facultyName,
        facultyBustUrl,
        ttsGenerated: wantsTts,
      },
      {
        "x-mynah-route": "ask-faculty",
        "x-voice-tts-generated": String(wantsTts),
        ...voiceHeaders,
        "x-faculty-slug": facultySlug,
        "x-faculty-name": facultyName,
      },
    );
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.error("ask-faculty error:", msg);
    return jsonResponse(500, { error: msg });
  }
});
