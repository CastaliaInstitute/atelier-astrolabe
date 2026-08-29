import { assertEquals } from "jsr:@std/assert";

import {
  mergeGeminiLiveFinal,
  normalizeGeminiLiveTranscript,
  parseGeminiLiveTranscriptMessage,
  splitPcmForGeminiLive,
} from "./geminiLiveTranscriber.ts";

Deno.test("merges segmented Gemini Live finals", () => {
  assertEquals(
    mergeGeminiLiveFinal(
      "Daniel and Camille first met in Paris",
      "in the autumn of 2012.",
    ),
    "Daniel and Camille first met in Paris in the autumn of 2012.",
  );
  assertEquals(
    mergeGeminiLiveFinal("Daniel met", "Daniel met Camille."),
    "Daniel met Camille.",
  );
  assertEquals(
    mergeGeminiLiveFinal("Daniel met Camille.", "Camille."),
    "Daniel met Camille.",
  );
});

Deno.test("splits PCM into Gemini Live 100 ms messages without losing bytes", () => {
  const pcm = Uint8Array.from({ length: 32768 }, (_, index) => index % 251);
  const chunks = splitPcmForGeminiLive(pcm, 16000);

  assertEquals(chunks.length, 11);
  assertEquals(
    chunks.slice(0, -1).map((chunk) => chunk.byteLength),
    Array(10).fill(3200),
  );
  assertEquals(chunks.at(-1)?.byteLength, 768);

  const reconstructed = new Uint8Array(
    chunks.reduce((total, chunk) => total + chunk.byteLength, 0),
  );
  let offset = 0;
  for (const chunk of chunks) {
    reconstructed.set(chunk, offset);
    offset += chunk.byteLength;
  }
  assertEquals(reconstructed, pcm);
});

Deno.test("parses Gemini Live setup acknowledgement", () => {
  assertEquals(
    parseGeminiLiveTranscriptMessage('{"setupComplete":{}}'),
    { kind: "setup" },
  );
});

Deno.test("parses Gemini Live interim transcription", () => {
  assertEquals(
    parseGeminiLiveTranscriptMessage(JSON.stringify({
      serverContent: { interimInputTranscription: { text: "When did Daniel" } },
    })),
    { kind: "interim", text: "When did Daniel" },
  );
});

Deno.test("parses Gemini Live final transcription", () => {
  assertEquals(
    parseGeminiLiveTranscriptMessage(JSON.stringify({
      serverContent: { inputTranscription: { text: "Daniel met Camille." } },
    })),
    { kind: "final", text: "Daniel met Camille." },
  );
});

Deno.test("ignores malformed Gemini Live messages", () => {
  assertEquals(parseGeminiLiveTranscriptMessage("not-json"), { kind: "none" });
});

Deno.test("surfaces Gemini Live server errors", () => {
  assertEquals(
    parseGeminiLiveTranscriptMessage(JSON.stringify({
      error: { code: 400, message: "Invalid audio stream" },
    })),
    { kind: "error", message: "Gemini Live error 400: Invalid audio stream" },
  );
});

Deno.test("rejects Gemini no-speech placeholders", () => {
  assertEquals(normalizeGeminiLiveTranscript("None"), "");
  assertEquals(normalizeGeminiLiveTranscript("[No transcript]"), "");
  assertEquals(
    parseGeminiLiveTranscriptMessage(JSON.stringify({
      serverContent: { inputTranscription: { text: "None" } },
    })),
    { kind: "none" },
  );
});
