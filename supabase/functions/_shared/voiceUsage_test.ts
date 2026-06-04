import { assertAlmostEquals, assertEquals } from "jsr:@std/assert";

import {
  estimateGeminiUsd,
  estimateSttUsd,
  estimateTokensFromChars,
  estimateTtsUsd,
  ttsUsdPerChar,
} from "./voiceUsage.ts";

Deno.test("ttsUsdPerChar defaults for Chirp 3 HD", () => {
  assertEquals(ttsUsdPerChar("en-US-Chirp3-HD-Charon"), 0.00003);
});

Deno.test("estimateTtsUsd scales by character count", () => {
  const usd = estimateTtsUsd(1000, "en-US-Chirp3-HD-Charon");
  assertAlmostEquals(usd, 0.03, 1e-9);
});

Deno.test("estimateSttUsd scales by audio seconds", () => {
  assertAlmostEquals(estimateSttUsd(60), 0.024, 1e-9);
  assertAlmostEquals(estimateSttUsd(30), 0.012, 1e-9);
});

Deno.test("estimateTokensFromChars uses conservative four character tokens", () => {
  assertEquals(estimateTokensFromChars(0), 0);
  assertEquals(estimateTokensFromChars(1), 1);
  assertEquals(estimateTokensFromChars(8), 2);
  assertEquals(estimateTokensFromChars(9), 3);
});

Deno.test("estimateGeminiUsd combines input and output tokens", () => {
  assertAlmostEquals(estimateGeminiUsd(1000, 100), 0.00055, 1e-12);
});
