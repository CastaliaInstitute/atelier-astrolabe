/** Google Cloud Chirp 3: HD voice + optional style prompt for Castalia faculty TTS. */

export type FacultyTtsConfig = {
  languageCode: string;
  name: string;
  /** Chirp 3 style / delivery instruction (SynthesisInput.prompt). */
  prompt?: string;
};

export const DEFAULT_CHIRP3_TTS: FacultyTtsConfig = {
  languageCode: "en-US",
  name: "en-US-Chirp3-HD-Charon",
  prompt:
    "Speak clearly and warmly, as a thoughtful conversational guide on a small wearable device. Natural pacing, one or two short paragraphs.",
};

/** Fallback when Supabase faculty row has no google_tts_* columns populated. */
export const FACULTY_CHIRP3_DEFAULTS: Record<string, FacultyTtsConfig> = {
  "a.einstein": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Charon",
    prompt:
      "Speak as Albert Einstein: warm, curious, slightly playful physicist. Measured pace with gentle wonder; plain words, not performance.",
  },
  "a.plato": {
    languageCode: "en-GB",
    name: "en-GB-Chirp3-HD-Orus",
    prompt:
      "Speak as Plato: calm Socratic teacher. Deliberate British cadence, reflective and inviting inquiry rather than lecturing.",
  },
  "a.curie": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Kore",
    prompt:
      "Speak as Marie Curie: quiet precision and steadfast warmth. Clear scientific diction, patient and grounded.",
  },
  "a.darwin": {
    languageCode: "en-GB",
    name: "en-GB-Chirp3-HD-Charon",
    prompt:
      "Speak as Charles Darwin: patient, observant, modest, and evidence-minded. Clear British naturalist diction, concrete examples, no grandstanding.",
  },
  "marie-curie": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Kore",
    prompt:
      "Speak as Marie Curie: quiet precision and steadfast warmth. Clear scientific diction, patient and grounded.",
  },
  "a.turing": {
    languageCode: "en-GB",
    name: "en-GB-Chirp3-HD-Fenrir",
    prompt:
      "Speak as Alan Turing: thoughtful, precise British intellect. Slightly reserved, logical, concise.",
  },
  "a.hesse": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Orus",
    prompt:
      "Speak as Hermann Hesse: contemplative literary mystic. Gentle, inward, warm cadence with quiet spiritual clarity.",
  },
  "hermann-hesse": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Orus",
    prompt:
      "Speak as Hermann Hesse: contemplative literary mystic. Gentle, inward, warm cadence with quiet spiritual clarity.",
  },
  "a.campbell": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Puck",
    prompt:
      "Speak as Joseph Campbell: engaging mythic storyteller. Rhythmic, inviting cadence that draws the listener in.",
  },
  "a.brucelee": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Iapetus",
    prompt:
      "Speak as Bruce Lee: direct, focused martial philosopher. Short pauses, energetic clarity, no fluff.",
  },
  "hypatia": {
    languageCode: "en-GB",
    name: "en-GB-Chirp3-HD-Aoede",
    prompt:
      "Speak as Hypatia: clear pedagogical voice. Calm authority in mathematics, philosophy, and civic ethics.",
  },
  "a.hypatia": {
    languageCode: "en-GB",
    name: "en-GB-Chirp3-HD-Aoede",
    prompt:
      "Speak as Hypatia: clear pedagogical voice. Calm authority in mathematics, philosophy, and civic ethics.",
  },
  "tom-robbins": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Leda",
    prompt:
      "Speak as Tom Robbins: witty, vivid, slightly mischievous literary voice. Colorful images, conversational flow.",
  },
  "a.tomrobbins": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Leda",
    prompt:
      "Speak as Tom Robbins: witty, vivid, slightly mischievous literary voice. Colorful images, conversational flow.",
  },
  "a-tomrobbins": {
    languageCode: "en-US",
    name: "en-US-Chirp3-HD-Leda",
    prompt:
      "Speak as Tom Robbins: witty, vivid, slightly mischievous literary voice. Colorful images, conversational flow.",
  },
};

export function isChirp3VoiceName(name: string): boolean {
  return /-Chirp3-HD-/i.test(name.trim());
}

/** Derive BCP-47 language tag from a Google voice name prefix (e.g. en-US-Chirp3-HD-Charon). */
export function languageFromVoiceName(name: string): string {
  const m = name.trim().match(/^([a-z]{2})-([A-Z]{2})\b/i);
  if (!m) return "en-US";
  return `${m[1].toLowerCase()}-${m[2].toUpperCase()}`;
}

export function facultyTtsFallback(
  slugRaw: string,
): FacultyTtsConfig | undefined {
  const slug = slugRaw.trim().toLowerCase();
  if (!slug) return undefined;
  if (FACULTY_CHIRP3_DEFAULTS[slug]) return FACULTY_CHIRP3_DEFAULTS[slug];
  const dotted = slug.replace(/-/g, ".");
  if (FACULTY_CHIRP3_DEFAULTS[dotted]) return FACULTY_CHIRP3_DEFAULTS[dotted];
  const dashed = slug.replace(/\./g, "-");
  if (FACULTY_CHIRP3_DEFAULTS[dashed]) return FACULTY_CHIRP3_DEFAULTS[dashed];
  return undefined;
}

function stringField(value: unknown): string {
  return typeof value === "string" ? value.trim() : "";
}

/** Merge DB row + env + static fallbacks into a Chirp-friendly TTS config. */
export function facultyTtsFromRow(
  row: Record<string, unknown> | null | undefined,
  slugRaw?: string,
): FacultyTtsConfig {
  const slug = stringField(row?.id) || stringField(row?.slug) ||
    stringField(slugRaw);
  const fallback = facultyTtsFallback(slug) ?? DEFAULT_CHIRP3_TTS;

  const name = stringField(row?.google_tts_voice_name) ||
    stringField(row?.tts_voice_name) ||
    fallback.name;
  let languageCode = stringField(row?.google_tts_language_code) ||
    stringField(row?.tts_language_code) ||
    fallback.languageCode;
  languageCode = languageFromVoiceName(name);
  const prompt = stringField(row?.google_tts_prompt) ||
    stringField(row?.tts_prompt) ||
    fallback.prompt;

  return { languageCode, name, ...(prompt ? { prompt } : {}) };
}
