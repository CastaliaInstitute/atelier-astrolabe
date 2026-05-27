/**
 * Named `voice-pipeline` profiles: each face may set defaults for LLM system text.
 * Clients send `face` + optional `systemInstruction` override.
 */

export const VOICE_FACE_CLOCK_AGENDA = "clock_agenda";
export const VOICE_FACE_DAILY_BRIEFING = "daily_briefing";
export const VOICE_FACE_SYNASTRY = "synastry";
export const VOICE_FACE_ASTRO = "astro";
export const VOICE_FACE_BABEL_FISH = "babelfish";

/** When `skipLlm` is false, Gemini rewrites schedule facts using this unless the client sends `systemInstruction`. */
export const SYSTEM_VOICE_FACE_CLOCK_AGENDA =
  "You speak aloud for a tiny round watch. The user message is SCHEDULE FACTS (time, current block, next event). " +
  "Rewrite into clear, natural spoken English in one or two short sentences, with the light touch of a gentle " +
  "fortune teller offering a small omen and practical counsel. " +
  "Do not invent events or times; only restate what appears in the facts. " +
  "If the facts say the calendar is not configured or could not be read, say that briefly and kindly.";
