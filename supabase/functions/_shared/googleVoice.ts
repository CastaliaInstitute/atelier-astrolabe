export const corsHeaders: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
};

export function jsonResponse(
  status: number,
  body: unknown,
  extraHeaders?: Record<string, string>,
): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json", ...extraHeaders },
  });
}

export function envKeys(): {
  speech: string;
  gemini: string;
  tts: string;
} {
  const speech =
    Deno.env.get("GOOGLE_SPEECH_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_SPEECH_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    "";
  const gemini =
    Deno.env.get("GOOGLE_GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_AI_API_KEY")?.trim() ||
    Deno.env.get("GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    "";
  const tts =
    Deno.env.get("GOOGLE_TTS_API_KEY")?.trim() ||
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
  const url =
    `https://speech.googleapis.com/v1/speech:recognize?key=${encodeURIComponent(apiKey)}`;
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
  const first = data.results?.[0]?.alternatives?.[0]?.transcript?.trim() ?? "";
  return first;
}

/** Default ~90s spoken at Neural2 pace (~2.5 words/s, ~15 chars/word). */
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
  return undefined;
}

export async function geminiGenerate(params: {
  apiKey: string;
  model: string;
  systemInstruction: string;
  userText: string;
}): Promise<string> {
  const { apiKey, model, systemInstruction, userText } = params;
  const maxOutputTokens = geminiMaxOutputTokens(systemInstruction);
  const url =
    `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${encodeURIComponent(apiKey)}`;
  const generationConfig =
    maxOutputTokens != null ? { maxOutputTokens } : undefined;
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
  if (!out) {
    const reason = data.candidates?.[0]?.finishReason ?? "unknown";
    throw new Error(`Gemini returned no text (finishReason=${reason})`);
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
};

export type WatchTtsVoiceSelection = {
  languageCode: string;
  name: string;
};

export function watchTtsVoiceSelection(options?: WatchTtsOptions): WatchTtsVoiceSelection {
  if (options?.voice?.languageCode && options.voice.name) {
    return options.voice;
  }
  const languageCode =
    Deno.env.get("MYNAH_TTS_LANGUAGE_CODE")?.trim() || "en-GB";
  const name =
    Deno.env.get("MYNAH_TTS_VOICE_NAME")?.trim() || "en-GB-Neural2-A";
  return { languageCode, name };
}

function watchTtsAudioConfig(options?: WatchTtsOptions): Record<string, number | string> {
  const hour = Number.isFinite(options?.localHour) ? Number(options?.localHour) : -1;
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

async function ttsMp3BytesInner(
  apiKey: string,
  spoken: string,
  options?: WatchTtsOptions,
): Promise<Uint8Array> {
  if (!spoken) {
    throw new Error("Text-to-Speech: no speakable text after stripping stage directions.");
  }
  const url =
    `https://texttospeech.googleapis.com/v1/text:synthesize?key=${encodeURIComponent(apiKey)}`;
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      input: { text: spoken },
      voice: watchTtsVoiceSelection(options),
      audioConfig: watchTtsAudioConfig(options),
    }),
  });
  const raw = await res.text();
  if (!res.ok) {
    throw new Error(`Text-to-Speech failed: ${res.status} ${raw}`);
  }
  const data = JSON.parse(raw) as { audioContent?: string };
  const b64 = data.audioContent?.trim() ?? "";
  if (!b64) throw new Error("Text-to-Speech returned empty audio.");
  const bin = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  if (bin.length === 0) throw new Error("Text-to-Speech returned empty audio.");
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
