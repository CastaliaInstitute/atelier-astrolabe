import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { createClient } from "npm:@supabase/supabase-js@2.49.8";

import {
  corsHeaders,
  envKeys,
  geminiGenerate,
  jsonResponse,
  ttsMp3Base64,
  type WatchTtsVoiceSelection,
} from "../_shared/googleVoice.ts";
import {
  facultyBustPathIsPoseFallback,
  generateFacultyBustPoseIfMissing,
  inferFacultySlug,
  signedFacultyBustUrl,
} from "../_shared/facultyBust.ts";
import {
  buildFacultyVoicePrompt,
  normalizeFacultyVoiceSlug,
  resolveFacultyVoiceProfile,
  voiceFromUnknown,
} from "../_shared/facultyVoice.ts";
import { scheduleMynahCommonplaceLog } from "../_shared/commonplaceDirectus.ts";

type AskFacultyBody = {
  /** Prompt routed from voice-pipeline (text after `ask ...`). */
  message: string;
  languageCode?: string;
  geminiModel?: string;
  /** Full user utterance for client display (optional). */
  rawTranscript?: string;
  systemInstruction?: string;
  skipLlm?: boolean;
  localHour?: number;
  facultySlug?: string;
  facultyName?: string;
  facultyEthnicity?: string;
  facultyAccent?: string;
  facultyLanguage?: string;
  facultyVoicePrompt?: string;
  facultyTtsVoice?: WatchTtsVoiceSelection | Record<string, unknown> | string;
};

function requestLocalHour(body: AskFacultyBody): number | undefined {
  const hour = Number(body.localHour);
  if (!Number.isFinite(hour)) return undefined;
  if (hour < 0 || hour > 23) return undefined;
  return Math.floor(hour);
}

function cleanText(value: unknown): string {
  return typeof value === "string" ? value.trim() : "";
}

function facultyDisplayName(slug: string, requested: unknown): string | undefined {
  const direct = cleanText(requested);
  if (direct) return direct;
  const compact = slug.replace(/^a[._-]/i, "").replace(/[._-]+/g, " ").trim();
  if (!compact) return undefined;
  return compact.replace(/\b[a-z]/g, (m) => m.toUpperCase());
}

async function resolveAuthUserId(authHeader: string): Promise<string | null> {
  const token = authHeader.replace(/^Bearer\s+/i, "").trim();
  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const anon = Deno.env.get("SUPABASE_ANON_KEY")?.trim() ?? "";
  if (!token || !url || !anon || token === anon) return null;
  try {
    const res = await fetch(`${url.replace(/\/$/, "")}/auth/v1/user`, {
      headers: { Authorization: `Bearer ${token}`, apikey: anon },
    });
    if (!res.ok) return null;
    const json = await res.json() as { id?: unknown };
    return typeof json.id === "string" && json.id ? json.id : null;
  } catch {
    return null;
  }
}

async function saveFacultyMemberHistory(params: {
  authHeader: string;
  facultySlug: string;
  facultyName?: string;
  transcript: string;
  reply: string;
  facultyVoice: Record<string, unknown>;
  facultyBustUrl: string | null;
}): Promise<void> {
  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key) return;
  const supabase = createClient(url, key);
  const authUserId = await resolveAuthUserId(params.authHeader);
  const { error } = await supabase.from("faculty_member_history").insert({
    auth_user_id: authUserId,
    faculty_id: params.facultySlug,
    faculty_slug: params.facultySlug,
    faculty_name: params.facultyName,
    route: "ask-faculty",
    transcript: params.transcript,
    reply: params.reply,
    faculty_voice: params.facultyVoice,
    faculty_bust_url: params.facultyBustUrl,
  });
  if (error) {
    console.warn("ask-faculty history save:", error.message);
  }
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

  const message = cleanText(body.message);
  if (!message) {
    return jsonResponse(400, { error: "message is required" });
  }

  const { gemini, tts } = envKeys();
  const geminiModel =
    (body.geminiModel ?? Deno.env.get("GEMINI_MODEL") ?? "gemini-2.5-flash")
      .trim();

  const facultySlug =
    normalizeFacultyVoiceSlug(body.facultySlug) ||
    normalizeFacultyVoiceSlug(inferFacultySlug(message));
  const displayName = facultyDisplayName(facultySlug, body.facultyName);
  const resolvedVoice = await resolveFacultyVoiceProfile(facultySlug);
  const requestVoice = voiceFromUnknown(body.facultyTtsVoice);
  const facultyVoice = resolvedVoice
    ? {
      ...resolvedVoice,
      ethnicity: cleanText(body.facultyEthnicity) || resolvedVoice.ethnicity,
      accent: cleanText(body.facultyAccent) || resolvedVoice.accent,
      language: cleanText(body.facultyLanguage) || resolvedVoice.language,
      prompt: cleanText(body.facultyVoicePrompt) || resolvedVoice.prompt,
      ttsVoice: requestVoice || resolvedVoice.ttsVoice,
    }
    : {
      facultySlug,
      ethnicity: cleanText(body.facultyEthnicity) || undefined,
      accent: cleanText(body.facultyAccent) || undefined,
      language: cleanText(body.facultyLanguage) || undefined,
      prompt: cleanText(body.facultyVoicePrompt) || undefined,
      ttsVoice: requestVoice,
    };
  const facultyVoicePrompt = buildFacultyVoicePrompt(facultyVoice);

  const defaultSystem =
    Deno.env.get("FACULTY_GEMINI_SYSTEM_INSTRUCTION")?.trim() ||
    Deno.env.get("FACULTY_SYSTEM_INSTRUCTION")?.trim() ||
    "You are Mynah's Castalia faculty specialist. The user is asking about institute faculty, their published views, or course-related guidance. " +
      "Answer accurately and cite uncertainty when you are not sure. Prefer concise, spoken-friendly replies (short paragraphs). " +
      "Do not pretend to perform device actions.";

  const systemInstruction = [
    cleanText(body.systemInstruction) || defaultSystem,
    facultyVoicePrompt,
  ].filter(Boolean).join("\n\n");

  const rawTranscript = cleanText(body.rawTranscript);
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
      const userText =
        rawTranscript && rawTranscript !== message
          ? `User said (full utterance): ${rawTranscript}\n\nFaculty-focused request: ${message}`
          : message;
      reply = await geminiGenerate({
        apiKey: gemini,
        model: geminiModel,
        systemInstruction,
        userText,
      });
    }

    if (!tts) {
      return jsonResponse(500, {
        error: "Server missing GOOGLE_TTS_API_KEY or GOOGLE_CLOUD_API_KEY",
      });
    }

    const audioBase64 = await ttsMp3Base64(tts, reply, {
      localHour: requestLocalHour(body),
      voice: facultyVoice.ttsVoice,
    });

    let facultyBustUrl: string | null = null;
    try {
      let bust = await signedFacultyBustUrl(facultySlug, undefined, "right");
      if (bust && facultyBustPathIsPoseFallback(bust.path, "right")) {
        const generated = await generateFacultyBustPoseIfMissing(facultySlug, "right");
        if (generated.generated) {
          bust = await signedFacultyBustUrl(facultySlug, undefined, "right") ?? bust;
        }
      }
      facultyBustUrl = bust?.url ?? null;
    } catch (e) {
      console.warn("ask-faculty bust URL:", e);
    }

    const authHdr = req.headers.get("Authorization") ?? "";
    const facultyVoiceJson = {
      ethnicity: facultyVoice.ethnicity,
      accent: facultyVoice.accent,
      language: facultyVoice.language,
      prompt: facultyVoicePrompt,
      ttsVoice: facultyVoice.ttsVoice,
    };
    await saveFacultyMemberHistory({
      authHeader: authHdr,
      facultySlug,
      facultyName: displayName,
      transcript,
      reply,
      facultyVoice: facultyVoiceJson,
      facultyBustUrl,
    });
    scheduleMynahCommonplaceLog(authHdr, {
      kind: "conversation",
      route: "ask-faculty",
      transcript,
      reply,
      facultySlug,
    });

    return jsonResponse(200, {
      transcript,
      reply,
      audioBase64,
      route: "ask-faculty",
      facultySlug,
      facultyName: displayName,
      facultyBustUrl,
      facultyVoice: facultyVoiceJson,
    });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.error("ask-faculty error:", msg);
    return jsonResponse(500, { error: msg });
  }
});
