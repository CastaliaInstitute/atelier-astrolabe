/**
 * Per-user Google TTS usage logging for cost attribution.
 * Inserts into public.voice_usage_events (castalia.institute migration).
 */

import { createClient } from "npm:@supabase/supabase-js@2.49.8";

import { isChirp3VoiceName } from "./facultyTts.ts";

type EdgeRt = { waitUntil: (promise: Promise<unknown>) => void };

function edgeWaitUntil(promise: Promise<unknown>): void {
  const er = (globalThis as unknown as { EdgeRuntime?: EdgeRt }).EdgeRuntime;
  if (er?.waitUntil) {
    er.waitUntil(promise);
  } else {
    void promise;
  }
}

export type VoiceUsageContext = {
  route: string;
  userId?: string | null;
  face?: string;
  facultySlug?: string;
  source?: string;
};

export type VoiceUsageEvent = VoiceUsageContext & {
  voiceName: string;
  languageCode: string;
  spokenChars: number;
  promptChars: number;
  audioBytes: number;
};

/** Default Chirp 3 HD list price (USD per character) after free tier. */
export function ttsUsdPerChar(voiceName: string): number {
  const raw = Deno.env.get("VOICE_TTS_USD_PER_CHAR")?.trim();
  if (raw) {
    const n = Number(raw);
    if (Number.isFinite(n) && n >= 0) return n;
  }
  if (isChirp3VoiceName(voiceName)) return 0.00003;
  if (/Neural2/i.test(voiceName)) return 0.000016;
  return 0.000004;
}

export function estimateTtsUsd(
  billableChars: number,
  voiceName: string,
): number {
  if (billableChars <= 0) return 0;
  return billableChars * ttsUsdPerChar(voiceName);
}

export async function resolveVoiceUsageUserId(
  req: Request,
): Promise<string | null> {
  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const anonKey = Deno.env.get("SUPABASE_ANON_KEY")?.trim() ?? "";
  const authHeader = req.headers.get("Authorization") ?? "";
  const token = authHeader.replace(/^Bearer\s+/i, "").trim();
  if (!token || !url || !anonKey || token === anonKey) return null;

  try {
    const client = createClient(url, anonKey, {
      global: { headers: { Authorization: `Bearer ${token}` } },
      auth: { autoRefreshToken: false, persistSession: false },
    });
    const { data: { user }, error } = await client.auth.getUser(token);
    if (error || !user?.id) return null;
    return user.id;
  } catch {
    return null;
  }
}

async function insertVoiceUsageEvent(event: VoiceUsageEvent): Promise<void> {
  if (Deno.env.get("VOICE_USAGE_DISABLED") === "true") return;

  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key) {
    console.warn("voice usage: SUPABASE_URL or service role missing; skip log");
    return;
  }

  const billableChars = event.spokenChars + event.promptChars;
  const estimatedUsd = estimateTtsUsd(billableChars, event.voiceName);

  const supabase = createClient(url, key, {
    auth: { autoRefreshToken: false, persistSession: false },
  });

  const { error } = await supabase.from("voice_usage_events").insert({
    user_id: event.userId ?? null,
    service: "google_tts",
    route: event.route,
    source: event.source?.trim() || null,
    face: event.face?.trim() || null,
    faculty_slug: event.facultySlug?.trim() || null,
    voice_name: event.voiceName,
    language_code: event.languageCode,
    spoken_chars: event.spokenChars,
    prompt_chars: event.promptChars,
    billable_chars: billableChars,
    audio_bytes: event.audioBytes,
    estimated_usd: estimatedUsd,
  });

  if (error) {
    console.warn("voice usage: insert failed", error.message);
  }
}

export function scheduleVoiceUsageLog(event: VoiceUsageEvent): void {
  edgeWaitUntil(
    insertVoiceUsageEvent(event).catch((e) =>
      console.warn("voice usage:", e instanceof Error ? e.message : e)
    ),
  );
}
