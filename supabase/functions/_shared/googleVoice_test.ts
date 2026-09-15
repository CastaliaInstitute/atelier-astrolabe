import { assertEquals } from "jsr:@std/assert";
import {
  combineSpeechTranscripts,
  ttsSynthesisInput,
  watchTtsAudioConfig,
} from "./googleVoice.ts";

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

Deno.test("Chirp 3 receives the LunaSay delivery prompt without mechanical slowing", () => {
  const voice = "en-US-Chirp3-HD-Charon";
  assertEquals(
    ttsSynthesisInput("The Moon is rising.", { prompt: "Soft and lucid." }, voice),
    { text: "The Moon is rising.", prompt: "Soft and lucid." },
  );
  assertEquals(
    watchTtsAudioConfig({ localHour: 23 }, voice),
    { audioEncoding: "MP3", volumeGainDb: -2.0 },
  );
});

Deno.test("Gemini Flash TTS receives the LunaSay delivery prompt without legacy rate controls", () => {
  const model = "gemini-2.5-flash-tts";
  assertEquals(
    ttsSynthesisInput(
      "The Moon is rising.",
      { prompt: "Soft and lucid." },
      "Kore",
      model,
    ),
    { text: "The Moon is rising.", prompt: "Soft and lucid." },
  );
  assertEquals(
    watchTtsAudioConfig({ localHour: 23 }, "Kore", model),
    { audioEncoding: "MP3", volumeGainDb: -2.0 },
  );
});
