import { assertEquals } from "jsr:@std/assert";

import {
  facultyTtsFallback,
  facultyTtsFromRow,
  isChirp3VoiceName,
} from "./facultyTts.ts";

Deno.test("isChirp3VoiceName detects Chirp 3 HD voices", () => {
  assertEquals(isChirp3VoiceName("en-US-Chirp3-HD-Charon"), true);
  assertEquals(isChirp3VoiceName("en-GB-Neural2-A"), false);
});

Deno.test("facultyTtsFallback normalizes slug aliases", () => {
  const cfg = facultyTtsFallback("marie-curie");
  assertEquals(cfg?.name, "en-US-Chirp3-HD-Kore");
  assertEquals(cfg?.prompt?.includes("Marie Curie"), true);
});

Deno.test("facultyTtsFromRow prefers database columns", () => {
  const cfg = facultyTtsFromRow({
    id: "a.einstein",
    google_tts_voice_name: "en-US-Chirp3-HD-Puck",
    google_tts_language_code: "en-US",
    google_tts_prompt: "Custom Einstein delivery.",
  });
  assertEquals(cfg.name, "en-US-Chirp3-HD-Puck");
  assertEquals(cfg.prompt, "Custom Einstein delivery.");
});
