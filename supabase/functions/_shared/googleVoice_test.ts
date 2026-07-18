import { assertEquals } from "jsr:@std/assert";
import { combineSpeechTranscripts } from "./googleVoice.ts";

Deno.test("combineSpeechTranscripts preserves every recognized segment", () => {
  assertEquals(
    combineSpeechTranscripts([
      { alternatives: [{ transcript: "Reply with exactly one word." }] },
      { alternatives: [{ transcript: "Alpha." }] },
    ]),
    "Reply with exactly one word. Alpha.",
  );
});

Deno.test("combineSpeechTranscripts ignores empty alternatives", () => {
  assertEquals(
    combineSpeechTranscripts([{}, { alternatives: [] }, {
      alternatives: [{ transcript: " Moon phases. " }],
    }]),
    "Moon phases.",
  );
});
