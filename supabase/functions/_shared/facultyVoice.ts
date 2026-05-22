import { createClient } from "npm:@supabase/supabase-js@2.49.8";

import type { WatchTtsVoiceSelection } from "./googleVoice.ts";

export type FacultyVoiceProfile = {
  facultySlug: string;
  ethnicity?: string;
  accent?: string;
  language?: string;
  prompt?: string;
  ttsVoice?: WatchTtsVoiceSelection;
};

function stringField(value: unknown): string {
  return typeof value === "string" ? value.trim() : "";
}

export function languageFromVoiceName(name: string): string {
  const parts = name.trim().split("-");
  return parts.length >= 2 && parts[0] && parts[1] ? `${parts[0]}-${parts[1]}` : "en-US";
}

export function normalizeFacultyVoiceSlug(raw: unknown): string {
  const input = typeof raw === "string" ? raw.trim().toLowerCase() : "";
  if (!input) return "";
  const compact = input.replace(/^faculty[:\s-]*/i, "").replace(/[^a-z0-9]+/g, "");
  const aliases: Record<string, string> = {
    einstein: "a.einstein",
    alberteinstein: "a.einstein",
    curie: "a.curie",
    mariecurie: "a.curie",
    madamecurie: "a.curie",
    plato: "a.plato",
    turing: "a.turing",
    alanturing: "a.turing",
    campbell: "a.campbell",
    josephcampbell: "a.campbell",
    brucelee: "a.brucelee",
    lee: "a.brucelee",
    hypatia: "a.hypatia",
    hypatiaofalexandria: "a.hypatia",
    shakespeare: "a.shakespeare",
    williamshakespeare: "a.shakespeare",
  };
  if (aliases[compact]) return aliases[compact];
  if (/^a[._-][a-z0-9._-]+$/.test(input)) {
    return input.replace(/[-_]/g, ".");
  }
  return input.replace(/_/g, "-");
}

export function voiceFromUnknown(value: unknown): WatchTtsVoiceSelection | undefined {
  if (!value) return undefined;
  if (typeof value === "string") {
    const name = value.trim();
    return name ? { languageCode: languageFromVoiceName(name), name } : undefined;
  }
  if (typeof value !== "object" || Array.isArray(value)) return undefined;
  const r = value as Record<string, unknown>;
  const name =
    stringField(r.name) ||
    stringField(r.voiceName) ||
    stringField(r.voice_name) ||
    stringField(r.googleVoiceName) ||
    stringField(r.google_voice_name) ||
    stringField(r.ttsVoiceName) ||
    stringField(r.tts_voice_name);
  if (!name) return undefined;
  const languageCode =
    stringField(r.languageCode) ||
    stringField(r.language_code) ||
    stringField(r.googleLanguageCode) ||
    stringField(r.google_language_code) ||
    stringField(r.ttsLanguageCode) ||
    stringField(r.tts_language_code) ||
    languageFromVoiceName(name);
  return { languageCode, name };
}

export function voiceFromFacultyRow(row: Record<string, unknown>): WatchTtsVoiceSelection | undefined {
  const directName =
    stringField(row.google_tts_voice_name) ||
    stringField(row.tts_voice_name) ||
    stringField(row.google_voice_name);
  if (directName) {
    return {
      languageCode:
        stringField(row.google_tts_language_code) ||
        stringField(row.tts_language_code) ||
        stringField(row.google_language_code) ||
        languageFromVoiceName(directName),
      name: directName,
    };
  }

  const voice = typeof row.voice === "object" && row.voice && !Array.isArray(row.voice)
    ? (row.voice as Record<string, unknown>)
    : {};
  const voiceCard = typeof row.voice_card === "object" && row.voice_card && !Array.isArray(row.voice_card)
    ? (row.voice_card as Record<string, unknown>)
    : {};

  return (
    voiceFromUnknown(row.google_tts_voice) ||
    voiceFromUnknown(row.tts_voice) ||
    voiceFromUnknown(voice.googleTts) ||
    voiceFromUnknown(voice.google_tts) ||
    voiceFromUnknown(voiceCard.googleTts) ||
    voiceFromUnknown(voiceCard.google_tts) ||
    voiceFromUnknown(voiceCard.ttsVoice) ||
    voiceFromUnknown(voiceCard.tts_voice)
  );
}

export function voiceFromEnv(slugRaw: unknown): WatchTtsVoiceSelection | undefined {
  const slug = normalizeFacultyVoiceSlug(slugRaw);
  if (!slug) return undefined;
  const keySlug = slug.toUpperCase().replace(/[^A-Z0-9]+/g, "_");
  const name =
    Deno.env.get(`FACULTY_TTS_VOICE_${keySlug}`)?.trim() ||
    Deno.env.get(`FACULTY_GOOGLE_TTS_VOICE_${keySlug}`)?.trim() ||
    "";
  if (!name) return undefined;
  const languageCode =
    Deno.env.get(`FACULTY_TTS_LANGUAGE_${keySlug}`)?.trim() ||
    Deno.env.get(`FACULTY_GOOGLE_TTS_LANGUAGE_${keySlug}`)?.trim() ||
    languageFromVoiceName(name);
  return { languageCode, name };
}

function fallbackProfile(slugRaw: unknown): FacultyVoiceProfile | undefined {
  const facultySlug = normalizeFacultyVoiceSlug(slugRaw);
  if (!facultySlug) return undefined;
  const profiles: Record<string, Omit<FacultyVoiceProfile, "facultySlug">> = {
    "a.einstein": {
      ethnicity: "Ashkenazi Jewish, German-born Swiss-American",
      accent: "light German-influenced English",
      language: "German and English",
      ttsVoice: { languageCode: "en-US", name: "en-US-Neural2-D" },
    },
    "a.curie": {
      ethnicity: "Polish-born French",
      accent: "light Polish/French-influenced English",
      language: "Polish, French, and English",
      ttsVoice: { languageCode: "en-US", name: "en-US-Neural2-F" },
    },
    "a.plato": {
      ethnicity: "Ancient Greek",
      accent: "Greek-influenced English",
      language: "Ancient Greek",
      ttsVoice: { languageCode: "en-GB", name: "en-GB-Neural2-B" },
    },
    "a.turing": {
      ethnicity: "British",
      accent: "educated British English",
      language: "English",
      ttsVoice: { languageCode: "en-GB", name: "en-GB-Neural2-D" },
    },
    "a.campbell": {
      ethnicity: "Irish-American",
      accent: "American English, measured lecture cadence",
      language: "English",
      ttsVoice: { languageCode: "en-US", name: "en-US-Neural2-J" },
    },
    "a.brucelee": {
      ethnicity: "Chinese, Hong Kong American",
      accent: "Hong Kong Cantonese-influenced English",
      language: "Cantonese and English",
      ttsVoice: { languageCode: "en-US", name: "en-US-Neural2-I" },
    },
    "a.hypatia": {
      ethnicity: "Alexandrian Greek",
      accent: "Greek-influenced English",
      language: "Greek",
      ttsVoice: { languageCode: "en-GB", name: "en-GB-Neural2-C" },
    },
  };
  const p = profiles[facultySlug];
  return p ? { facultySlug, ...p } : { facultySlug };
}

export function buildFacultyVoicePrompt(profile?: FacultyVoiceProfile): string {
  if (!profile) return "";
  const parts = [
    profile.ethnicity ? `cultural/ethnic background: ${profile.ethnicity}` : "",
    profile.accent ? `accent: ${profile.accent}` : "",
    profile.language ? `language background: ${profile.language}` : "",
  ].filter(Boolean);
  const base = parts.length
    ? `Voice casting for TTS: ${parts.join("; ")}.`
    : "Voice casting for TTS: use the selected faculty voice profile.";
  const custom = profile.prompt?.trim();
  return [
    base,
    custom,
    "Use this only to guide respectful voice, vocabulary, pronunciation, and cadence. Do not caricature or overstate accent. Keep the reply understandable to an English-speaking listener unless the user explicitly asks for another language.",
  ].filter(Boolean).join(" ");
}

export async function resolveFacultyVoiceProfile(slugRaw: unknown): Promise<FacultyVoiceProfile | undefined> {
  const facultySlug = normalizeFacultyVoiceSlug(slugRaw);
  if (!facultySlug) return undefined;

  const fallback = fallbackProfile(facultySlug);
  const envVoice = voiceFromEnv(facultySlug);

  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key) {
    return envVoice || fallback
      ? { ...(fallback ?? { facultySlug }), facultySlug, ...(envVoice ? { ttsVoice: envVoice } : {}) }
      : undefined;
  }

  try {
    const supabase = createClient(url, key);
    const select =
      "id,slug,google_tts_voice_name,google_tts_language_code,voice_accent,voice_language,voice_prompt,voice_card";
    let row: Record<string, unknown> | null = null;
    const byId = await supabase.from("faculty").select(select).eq("id", facultySlug).maybeSingle();
    if (!byId.error && byId.data) {
      row = byId.data as Record<string, unknown>;
    }
    if (!row) {
      const bySlug = await supabase.from("faculty").select(select).eq("slug", facultySlug).maybeSingle();
      if (!bySlug.error && bySlug.data) {
        row = bySlug.data as Record<string, unknown>;
      }
    }
    if (!row) {
      return envVoice || fallback
        ? { ...(fallback ?? { facultySlug }), facultySlug, ...(envVoice ? { ttsVoice: envVoice } : {}) }
        : undefined;
    }
    const dbVoice = voiceFromFacultyRow(row);
    const voiceCard = typeof row.voice_card === "object" && row.voice_card && !Array.isArray(row.voice_card)
      ? (row.voice_card as Record<string, unknown>)
      : {};
    return {
      facultySlug,
      ethnicity:
        stringField(voiceCard.ethnicity) ||
        stringField(voiceCard.culturalBackground) ||
        stringField(voiceCard.cultural_background) ||
        fallback?.ethnicity,
      accent: stringField(row.voice_accent) || stringField(voiceCard.accent) || fallback?.accent,
      language: stringField(row.voice_language) || stringField(voiceCard.language) ||
        stringField(voiceCard.languageBackground) || stringField(voiceCard.language_background) || fallback?.language,
      prompt: stringField(row.voice_prompt) || stringField(voiceCard.prompt) || fallback?.prompt,
      ttsVoice: envVoice || dbVoice || fallback?.ttsVoice,
    };
  } catch (e) {
    console.warn("faculty voice lookup failed", e);
    return envVoice || fallback
      ? { ...(fallback ?? { facultySlug }), facultySlug, ...(envVoice ? { ttsVoice: envVoice } : {}) }
      : undefined;
  }
}
