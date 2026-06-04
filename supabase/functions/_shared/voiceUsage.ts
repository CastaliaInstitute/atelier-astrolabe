/**
 * Per-user voice usage logging and budget gates.
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
  service?: string;
  model?: string;
  voiceName?: string;
  languageCode?: string;
  spokenChars?: number;
  promptChars?: number;
  billableChars?: number;
  audioBytes?: number;
  audioSeconds?: number;
  inputTokens?: number;
  outputTokens?: number;
  estimatedUsd?: number;
};

export type VoiceUsageGateResult = {
  allowed: boolean;
  status: number;
  reason: string;
  estimatedUsd: number;
  dailyUsd: number;
  monthlyUsd: number;
  dailyLimitUsd: number | null;
  monthlyLimitUsd: number | null;
  userId: string | null;
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

export function sttUsdPerMinute(): number {
  const raw = Deno.env.get("VOICE_STT_USD_PER_MINUTE")?.trim();
  if (raw) {
    const n = Number(raw);
    if (Number.isFinite(n) && n >= 0) return n;
  }
  return 0.024;
}

export function estimateSttUsd(audioSeconds: number): number {
  if (audioSeconds <= 0) return 0;
  return (audioSeconds / 60) * sttUsdPerMinute();
}

export function estimateTokensFromChars(chars: number): number {
  if (chars <= 0) return 0;
  return Math.ceil(chars / 4);
}

function geminiUsdPerInputToken(): number {
  const raw = Deno.env.get("VOICE_GEMINI_USD_PER_INPUT_TOKEN")?.trim();
  if (raw) {
    const n = Number(raw);
    if (Number.isFinite(n) && n >= 0) return n;
  }
  return 0.0000003;
}

function geminiUsdPerOutputToken(): number {
  const raw = Deno.env.get("VOICE_GEMINI_USD_PER_OUTPUT_TOKEN")?.trim();
  if (raw) {
    const n = Number(raw);
    if (Number.isFinite(n) && n >= 0) return n;
  }
  return 0.0000025;
}

export function estimateGeminiUsd(
  inputTokens: number,
  outputTokens: number,
): number {
  return Math.max(0, inputTokens) * geminiUsdPerInputToken() +
    Math.max(0, outputTokens) * geminiUsdPerOutputToken();
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

  const spokenChars = event.spokenChars ?? 0;
  const promptChars = event.promptChars ?? 0;
  const billableChars = event.billableChars ?? (spokenChars + promptChars);
  const service = event.service?.trim() || "google_tts";
  const estimatedUsd = event.estimatedUsd ??
    (service === "google_tts" && event.voiceName
      ? estimateTtsUsd(billableChars, event.voiceName)
      : 0);

  const supabase = createClient(url, key, {
    auth: { autoRefreshToken: false, persistSession: false },
  });

  const { error } = await supabase.from("voice_usage_events").insert({
    user_id: event.userId ?? null,
    service,
    route: event.route,
    source: event.source?.trim() || null,
    face: event.face?.trim() || null,
    faculty_slug: event.facultySlug?.trim() || null,
    model: event.model?.trim() || null,
    voice_name: event.voiceName?.trim() || null,
    language_code: event.languageCode?.trim() || null,
    spoken_chars: spokenChars,
    prompt_chars: promptChars,
    billable_chars: billableChars,
    audio_bytes: event.audioBytes ?? null,
    audio_seconds: event.audioSeconds ?? null,
    input_tokens: event.inputTokens ?? 0,
    output_tokens: event.outputTokens ?? 0,
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

function envUsd(name: string): number | null {
  const raw = Deno.env.get(name)?.trim();
  if (!raw) return null;
  const n = Number(raw);
  return Number.isFinite(n) && n > 0 ? n : null;
}

function usageGateEnabled(): boolean {
  return Deno.env.get("VOICE_USAGE_GATES_DISABLED") !== "true";
}

function requireMeteredUser(): boolean {
  return Deno.env.get("VOICE_USAGE_REQUIRE_USER_FOR_METERED") === "true";
}

async function usageSumSince(
  userId: string | null,
  sinceIso: string,
): Promise<number> {
  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key) return 0;

  const supabase = createClient(url, key, {
    auth: { autoRefreshToken: false, persistSession: false },
  });

  let query = supabase
    .from("voice_usage_events")
    .select("estimated_usd")
    .gte("created_at", sinceIso)
    .limit(10000);
  query = userId ? query.eq("user_id", userId) : query.is("user_id", null);
  const { data, error } = await query;
  if (error || !data) {
    console.warn("voice usage: budget lookup failed", error?.message);
    return 0;
  }
  return data.reduce((sum, row) => sum + Number(row.estimated_usd ?? 0), 0);
}

export async function checkVoiceUsageGate(
  req: Request,
  pendingUsd: number,
): Promise<VoiceUsageGateResult> {
  const userId = await resolveVoiceUsageUserId(req);
  const estimatedUsd = Math.max(0, pendingUsd);
  const dailyLimitUsd = envUsd(
    userId
      ? "VOICE_USAGE_DAILY_USD_LIMIT"
      : "VOICE_USAGE_UNAUTH_DAILY_USD_LIMIT",
  );
  const monthlyLimitUsd = envUsd(
    userId
      ? "VOICE_USAGE_MONTHLY_USD_LIMIT"
      : "VOICE_USAGE_UNAUTH_MONTHLY_USD_LIMIT",
  );

  if (!usageGateEnabled() || estimatedUsd <= 0) {
    return {
      allowed: true,
      status: 200,
      reason: "ok",
      estimatedUsd,
      dailyUsd: 0,
      monthlyUsd: 0,
      dailyLimitUsd,
      monthlyLimitUsd,
      userId,
    };
  }

  if (!userId && requireMeteredUser()) {
    return {
      allowed: false,
      status: 402,
      reason: "metered_voice_requires_authenticated_user",
      estimatedUsd,
      dailyUsd: 0,
      monthlyUsd: 0,
      dailyLimitUsd,
      monthlyLimitUsd,
      userId,
    };
  }

  const now = new Date();
  const dayStart = new Date(
    Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate()),
  );
  const monthStart = new Date(
    Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1),
  );
  const [dailyUsd, monthlyUsd] = await Promise.all([
    dailyLimitUsd
      ? usageSumSince(userId, dayStart.toISOString())
      : Promise.resolve(0),
    monthlyLimitUsd
      ? usageSumSince(userId, monthStart.toISOString())
      : Promise.resolve(0),
  ]);

  if (dailyLimitUsd && dailyUsd + estimatedUsd > dailyLimitUsd) {
    return {
      allowed: false,
      status: 402,
      reason: "daily_voice_budget_exceeded",
      estimatedUsd,
      dailyUsd,
      monthlyUsd,
      dailyLimitUsd,
      monthlyLimitUsd,
      userId,
    };
  }
  if (monthlyLimitUsd && monthlyUsd + estimatedUsd > monthlyLimitUsd) {
    return {
      allowed: false,
      status: 402,
      reason: "monthly_voice_budget_exceeded",
      estimatedUsd,
      dailyUsd,
      monthlyUsd,
      dailyLimitUsd,
      monthlyLimitUsd,
      userId,
    };
  }

  return {
    allowed: true,
    status: 200,
    reason: "ok",
    estimatedUsd,
    dailyUsd,
    monthlyUsd,
    dailyLimitUsd,
    monthlyLimitUsd,
    userId,
  };
}

export function voiceUsageGateResponse(gate: VoiceUsageGateResult): Response {
  return new Response(
    JSON.stringify({
      error: gate.reason,
      estimatedUsd: gate.estimatedUsd,
      dailyUsd: gate.dailyUsd,
      monthlyUsd: gate.monthlyUsd,
      dailyLimitUsd: gate.dailyLimitUsd,
      monthlyLimitUsd: gate.monthlyLimitUsd,
    }),
    {
      status: gate.status,
      headers: { "Content-Type": "application/json" },
    },
  );
}
