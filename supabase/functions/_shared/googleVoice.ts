import { DEFAULT_CHIRP3_TTS, isChirp3VoiceName } from "./facultyTts.ts";
import { scheduleVoiceUsageLog, type VoiceUsageContext } from "./voiceUsage.ts";

export const corsHeaders: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
};

let vertexAccessToken: { value: string; expiresAtMs: number } | undefined;

function base64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_")
    .replace(/=+$/, "");
}

function pemToDer(pem: string): ArrayBuffer {
  const body = pem.replace(/-----(BEGIN|END) PRIVATE KEY-----/g, "")
    .replace(/\s+/g, "");
  const bytes = Uint8Array.from(atob(body), (c) => c.charCodeAt(0));
  return bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength,
  ) as ArrayBuffer;
}

/** Obtain a short-lived Google OAuth token for Cloud TTS Gemini models. */
async function vertexServiceAccessToken(): Promise<string> {
  if (
    vertexAccessToken && vertexAccessToken.expiresAtMs > Date.now() + 60_000
  ) {
    return vertexAccessToken.value;
  }
  const raw = Deno.env.get("VERTEX_SERVICE_ACCOUNT_JSON")?.trim();
  if (!raw) {
    throw new Error(
      "Gemini TTS requires VERTEX_SERVICE_ACCOUNT_JSON with aiplatform.endpoints.predict.",
    );
  }
  const service = JSON.parse(raw) as {
    client_email?: string;
    private_key?: string;
    token_uri?: string;
  };
  if (!service.client_email || !service.private_key) {
    throw new Error(
      "VERTEX_SERVICE_ACCOUNT_JSON is missing client_email or private_key.",
    );
  }
  const now = Math.floor(Date.now() / 1000);
  const claim = {
    iss: service.client_email,
    scope: "https://www.googleapis.com/auth/cloud-platform",
    aud: service.token_uri || "https://oauth2.googleapis.com/token",
    iat: now,
    exp: now + 3600,
  };
  const header = base64Url(new TextEncoder().encode(JSON.stringify({
    alg: "RS256",
    typ: "JWT",
  })));
  const payload = base64Url(new TextEncoder().encode(JSON.stringify(claim)));
  const unsigned = `${header}.${payload}`;
  const privateKey = await crypto.subtle.importKey(
    "pkcs8",
    pemToDer(service.private_key),
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const signature = await crypto.subtle.sign(
    "RSASSA-PKCS1-v1_5",
    privateKey,
    new TextEncoder().encode(unsigned),
  );
  const assertion = `${unsigned}.${base64Url(new Uint8Array(signature))}`;
  const tokenRes = await fetch(
    service.token_uri || "https://oauth2.googleapis.com/token",
    {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({
        grant_type: "urn:ietf:params:oauth:grant-type:jwt-bearer",
        assertion,
      }),
    },
  );
  const text = await tokenRes.text();
  if (!tokenRes.ok) {
    throw new Error(`Google OAuth failed: ${tokenRes.status} ${text}`);
  }
  const token = JSON.parse(text) as {
    access_token?: string;
    expires_in?: number;
  };
  if (!token.access_token) {
    throw new Error("Google OAuth returned no access token.");
  }
  vertexAccessToken = {
    value: token.access_token,
    expiresAtMs: Date.now() + Math.max(60, token.expires_in ?? 3600) * 1000,
  };
  return token.access_token;
}

export function jsonResponse(
  status: number,
  body: unknown,
  extraHeaders?: Record<string, string>,
): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json",
      ...extraHeaders,
    },
  });
}

export function envKeys(): {
  speech: string;
  gemini: string;
  tts: string;
} {
  const speech = Deno.env.get("GOOGLE_SPEECH_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_SPEECH_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    "";
  const gemini = Deno.env.get("GOOGLE_GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_AI_API_KEY")?.trim() ||
    Deno.env.get("GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    "";
  const tts = Deno.env.get("GOOGLE_TTS_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_TTS_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    Deno.env.get("GCP_API_KEY")?.trim() ||
    "";
  return { speech, gemini, tts };
}

export async function speechRecognize(
  apiKey: string,
  audioBase64: string,
  languageCode: string,
  sampleRateHertz: number,
  alternativeLanguageCodes?: string[],
): Promise<string> {
  const url = `https://speech.googleapis.com/v1/speech:recognize?key=${
    encodeURIComponent(apiKey)
  }`;
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      config: {
        encoding: "LINEAR16",
        sampleRateHertz,
        languageCode,
        ...(alternativeLanguageCodes && alternativeLanguageCodes.length
          ? { alternativeLanguageCodes }
          : {}),
        enableAutomaticPunctuation: true,
      },
      audio: { content: audioBase64 },
    }),
  });
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`Speech-to-Text failed: ${res.status} ${text}`);
  }
  const data = JSON.parse(text) as {
    results?: Array<{ alternatives?: Array<{ transcript?: string }> }>;
  };
  return combineSpeechTranscripts(data.results);
}

export function combineSpeechTranscripts(
  results?: Array<{ alternatives?: Array<{ transcript?: string }> }>,
): string {
  return (results ?? [])
    .map((result) => result.alternatives?.[0]?.transcript?.trim() ?? "")
    .filter(Boolean)
    .join(" ");
}

/** Default ~90s spoken at conversational pace (~2.5 words/s, ~15 chars/word). */
export function watchTtsMaxChars(): number {
  const raw = Deno.env.get("MYNAH_TTS_MAX_CHARS")?.trim();
  const n = raw ? parseInt(raw, 10) : 1400;
  return Number.isFinite(n) && n > 200 ? n : 1400;
}

/** Longer cap for daily briefing face (~4–5 min spoken). */
export function watchTtsMaxCharsDailyBriefing(): number {
  const raw = Deno.env.get("MYNAH_TTS_DAILY_MAX_CHARS")?.trim();
  const n = raw ? parseInt(raw, 10) : 5200;
  return Number.isFinite(n) && n > 400 ? n : 5200;
}

/** Trim LLM output for TTS while keeping full text in JSON `reply`. */
export function capTextForWatchTts(
  text: string,
  maxChars: number = watchTtsMaxChars(),
): string {
  const spoken = stripAsteriskEmotes(text);
  if (!spoken || spoken.length <= maxChars) {
    return spoken;
  }
  const slice = spoken.slice(0, maxChars);
  const lastEnd = Math.max(
    slice.lastIndexOf(". "),
    slice.lastIndexOf("! "),
    slice.lastIndexOf("? "),
    slice.lastIndexOf(".\n"),
  );
  if (lastEnd > maxChars * 0.45) {
    return slice.slice(0, lastEnd + 1).trim();
  }
  return slice.trim();
}

function geminiMaxOutputTokens(systemInstruction: string): number | undefined {
  const raw = Deno.env.get("GEMINI_MAX_OUTPUT_TOKENS")?.trim();
  if (raw) {
    const n = parseInt(raw, 10);
    if (Number.isFinite(n) && n > 64) return n;
  }
  const s = systemInstruction.toLowerCase();
  if (
    s.includes("daily briefing") || s.includes("three to five minutes") ||
    s.includes("three to five minute")
  ) {
    return 2048;
  }
  if (
    s.includes("90 second") || s.includes("mini-reading") ||
    s.includes("under 90") || s.includes("tiny round watch")
  ) {
    return 512;
  }
  if (
    s.includes("under 25 seconds") || s.includes("under ~25 seconds") ||
    s.includes("never exceed 35 spoken words")
  ) {
    /* Gemini 2.5 may spend part of maxOutputTokens on internal reasoning.
     * A 128-token ceiling can therefore surface only the first few words of
     * an otherwise simple watch answer. Spoken duration is bounded later by
     * capTextForWatchTts, so retain enough generation headroom here. */
    return 512;
  }
  return undefined;
}

export async function geminiGenerate(params: {
  apiKey: string;
  model: string;
  systemInstruction: string;
  userText: string;
  /** Force a machine-readable response for device cache packets. */
  responseMimeType?: "application/json";
  /** JSON Schema used by Gemini structured output. */
  responseJsonSchema?: Record<string, unknown>;
  /** Explicit ceiling for structured responses that do not match legacy face prompts. */
  maxOutputTokens?: number;
  /** Gemini 3.x effort level; use minimal for deterministic format transforms. */
  thinkingLevel?: "minimal" | "low" | "medium" | "high";
  /** Gemini 2.5 thinking-token budget; zero is appropriate for strict transforms. */
  thinkingBudget?: number;
}): Promise<string> {
  const { apiKey, model, systemInstruction, userText } = params;
  const maxOutputTokens = params.maxOutputTokens ??
    geminiMaxOutputTokens(systemInstruction);
  const url =
    `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${
      encodeURIComponent(apiKey)
    }`;
  const thinkingConfig = params.thinkingLevel
    ? { thinkingLevel: params.thinkingLevel.toUpperCase() }
    : params.thinkingBudget != null
    ? { thinkingBudget: params.thinkingBudget }
    : undefined;
  const generationConfig = maxOutputTokens != null || params.responseMimeType ||
      params.responseJsonSchema || thinkingConfig
    ? {
      ...(maxOutputTokens != null ? { maxOutputTokens } : {}),
      ...(params.responseMimeType
        ? { responseMimeType: params.responseMimeType }
        : {}),
      ...(params.responseJsonSchema
        ? { responseJsonSchema: params.responseJsonSchema }
        : {}),
      ...(thinkingConfig ? { thinkingConfig } : {}),
    }
    : undefined;
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      systemInstruction: { parts: [{ text: systemInstruction }] },
      contents: [{ role: "user", parts: [{ text: userText }] }],
      ...(generationConfig ? { generationConfig } : {}),
    }),
  });
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`Gemini failed: ${res.status} ${text}`);
  }
  const data = JSON.parse(text) as {
    candidates?: Array<{
      content?: { parts?: Array<{ text?: string }> };
      finishReason?: string;
    }>;
    error?: { message?: string };
  };
  if (data.error?.message) {
    throw new Error(data.error.message);
  }
  const parts = data.candidates?.[0]?.content?.parts;
  const out = parts?.map((p) => p.text ?? "").join("")?.trim() ?? "";
  const finishReason = data.candidates?.[0]?.finishReason ?? "unknown";
  if (!out) {
    throw new Error(`Gemini returned no text (finishReason=${finishReason})`);
  }
  if (finishReason !== "STOP") {
    throw new Error(
      `Gemini output incomplete (finishReason=${finishReason}, chars=${out.length})`,
    );
  }
  return out;
}

/** Remove *stage directions* / emotes before TTS (watch should not speak them). */
export function stripAsteriskEmotes(text: string): string {
  let s = text;
  let prev = "";
  while (s !== prev) {
    prev = s;
    s = s.replace(/\*[^*\n]{1,160}\*/g, " ");
  }
  return s.replace(/\s+/g, " ").trim();
}

export type WatchTtsOptions = {
  localHour?: number;
  voice?: WatchTtsVoiceSelection;
  /** Chirp 3 style / delivery instruction (Google TTS SynthesisInput.prompt). */
  prompt?: string;
  /** When set, logs billable characters + estimated USD to voice_usage_events. */
  usage?: VoiceUsageContext;
};

export type WatchTtsVoiceSelection = {
  languageCode: string;
  name: string;
  /** Cloud TTS Gemini model. Omit for the existing voice-family path. */
  modelName?: string;
};

export function watchTtsVoiceSelection(
  options?: WatchTtsOptions,
): WatchTtsVoiceSelection {
  if (options?.voice?.languageCode && options.voice.name) {
    return options.voice;
  }
  const languageCode = Deno.env.get("MYNAH_TTS_LANGUAGE_CODE")?.trim() ||
    DEFAULT_CHIRP3_TTS.languageCode;
  const name = Deno.env.get("MYNAH_TTS_VOICE_NAME")?.trim() ||
    DEFAULT_CHIRP3_TTS.name;
  return { languageCode, name };
}

export function watchTtsAudioConfig(
  options?: WatchTtsOptions,
  voiceName?: string,
  modelName?: string,
): Record<string, number | string> {
  const chirp3 = isChirp3VoiceName(
    voiceName ?? watchTtsVoiceSelection(options).name,
  );
  const geminiTts = /^gemini-[\w.-]+-tts(?:-preview)?$/i.test(
    modelName ?? watchTtsVoiceSelection(options).modelName ?? "",
  );
  const hour = Number.isFinite(options?.localHour)
    ? Number(options?.localHour)
    : -1;
  if (chirp3 || geminiTts) {
    /* Chirp 3 HD controls pacing in the model and does not support the legacy
     * speaking-rate / pitch controls.  Let the delivery prompt supply the
     * softer LunaSay character rather than mechanically slowing it down. */
    return {
      audioEncoding: "MP3",
      ...(hour >= 22 || (hour >= 0 && hour < 6) ? { volumeGainDb: -2.0 } : {}),
    };
  }
  if (hour < 0 || hour > 23) {
    return {
      audioEncoding: "MP3",
      speakingRate: 1.0,
      pitch: 0.0,
    };
  }
  if (hour >= 22 || (hour >= 0 && hour < 6)) {
    return {
      audioEncoding: "MP3",
      speakingRate: 0.86,
      pitch: -2.0,
      volumeGainDb: -5.0,
    };
  }
  if (hour >= 18 || hour < 8) {
    return {
      audioEncoding: "MP3",
      speakingRate: 0.92,
      pitch: -1.2,
      volumeGainDb: -3.0,
    };
  }
  return {
    audioEncoding: "MP3",
    speakingRate: 1.0,
    pitch: 0.0,
  };
}

export function ttsSynthesisInput(
  spoken: string,
  options?: WatchTtsOptions,
  voiceName?: string,
  modelName?: string,
): Record<string, string> {
  const prompt = options?.prompt?.trim();
  /* SynthesisInput.prompt is supported by Google's controllable / promptable
   * models.  Chirp 3 HD is the controllable voice family we use; do not send
   * this field to legacy voices. */
  if (
    prompt &&
    (isChirp3VoiceName(voiceName ?? watchTtsVoiceSelection(options).name) ||
      /^gemini-[\w.-]+-tts(?:-preview)?$/i.test(
        modelName ?? watchTtsVoiceSelection(options).modelName ?? "",
      ))
  ) {
    return { text: spoken, prompt };
  }
  return { text: spoken };
}

function ttsSynthesizeUrl(
  options?: WatchTtsOptions,
  voiceName?: string,
): string {
  void options;
  void voiceName;
  return "https://texttospeech.googleapis.com/v1/text:synthesize";
}

async function ttsMp3BytesInner(
  apiKey: string,
  spoken: string,
  options?: WatchTtsOptions,
): Promise<Uint8Array> {
  if (!spoken) {
    throw new Error(
      "Text-to-Speech: no speakable text after stripping stage directions.",
    );
  }
  const voice = watchTtsVoiceSelection(options);
  const geminiTts = !!voice.modelName;
  const url = geminiTts
    ? ttsSynthesizeUrl(options, voice.name)
    : `${ttsSynthesizeUrl(options, voice.name)}?key=${
      encodeURIComponent(apiKey)
    }`;
  const authHeaders: Record<string, string> = geminiTts
    ? { Authorization: `Bearer ${await vertexServiceAccessToken()}` }
    : {};
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json", ...authHeaders },
    body: JSON.stringify({
      input: ttsSynthesisInput(spoken, options, voice.name, voice.modelName),
      voice,
      audioConfig: watchTtsAudioConfig(options, voice.name, voice.modelName),
    }),
  });
  const raw = await res.text();
  if (!res.ok) {
    /* Gemini TTS is an opt-in Cloud API with a separate Vertex permission.
     * Keep a deployed LunaSay device speaking while that one-time project
     * enablement propagates; setting this flag makes integration failures
     * fail closed for operational diagnostics instead. */
    if (
      geminiTts && Deno.env.get("LUNASAY_GEMINI_TTS_STRICT") !== "true" &&
      (res.status === 403 || res.status === 404)
    ) {
      console.warn(
        `Gemini TTS unavailable (${res.status}); falling back to Chirp 3 HD.`,
      );
      return await ttsMp3BytesInner(apiKey, spoken, {
        ...options,
        prompt: undefined,
        voice: {
          languageCode: DEFAULT_CHIRP3_TTS.languageCode,
          name: DEFAULT_CHIRP3_TTS.name,
        },
      });
    }
    /* Some projects have the Cloud TTS API enabled without access to Chirp 3
     * HD (or Vertex Gemini TTS).  Keep LunaSay speaking with a broadly
     * available Neural2 voice instead of turning a provider 403 into a silent
     * device.  The explicit voice also prevents this branch from recursing. */
    if (
      !geminiTts && isChirp3VoiceName(voice.name) &&
      Deno.env.get("LUNASAY_TTS_LEGACY_FALLBACK") !== "false" &&
      (res.status === 403 || res.status === 404)
    ) {
      console.warn(
        `Chirp 3 TTS unavailable (${res.status}); falling back to Neural2.`,
      );
      return await ttsMp3BytesInner(apiKey, spoken, {
        ...options,
        prompt: undefined,
        voice: {
          languageCode: "en-US",
          name: "en-US-Neural2-F",
        },
      });
    }
    throw new Error(`Text-to-Speech failed: ${res.status} ${raw}`);
  }
  const data = JSON.parse(raw) as { audioContent?: string };
  const b64 = data.audioContent?.trim() ?? "";
  if (!b64) throw new Error("Text-to-Speech returned empty audio.");
  const bin = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  if (bin.length === 0) throw new Error("Text-to-Speech returned empty audio.");

  const usage = options?.usage;
  if (usage?.route) {
    const promptChars = options?.prompt?.trim().length ?? 0;
    scheduleVoiceUsageLog({
      ...usage,
      ...(voice.modelName
        ? { service: "google_gemini_tts", model: voice.modelName }
        : {}),
      voiceName: voice.name,
      languageCode: voice.languageCode,
      spokenChars: spoken.length,
      promptChars,
      audioBytes: bin.length,
    });
  }

  return bin;
}

/** MP3 bytes for watch playback (reply may be truncated for length). */
export async function ttsMp3Bytes(
  apiKey: string,
  text: string,
  options?: WatchTtsOptions,
): Promise<Uint8Array> {
  const spoken = capTextForWatchTts(text);
  return await ttsMp3BytesInner(apiKey, spoken, options);
}

export async function ttsMp3Base64(
  apiKey: string,
  text: string,
  options?: WatchTtsOptions,
): Promise<string> {
  const bin = await ttsMp3Bytes(apiKey, text, options);
  let s = "";
  for (let i = 0; i < bin.length; i++) {
    s += String.fromCharCode(bin[i]);
  }
  return btoa(s);
}
