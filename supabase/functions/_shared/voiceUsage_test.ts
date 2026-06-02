import { assertAlmostEquals, assertEquals } from "jsr:@std/assert";

import { estimateTtsUsd, ttsUsdPerChar } from "./voiceUsage.ts";

Deno.test("ttsUsdPerChar defaults for Chirp 3 HD", () => {
  assertEquals(ttsUsdPerChar("en-US-Chirp3-HD-Charon"), 0.00003);
});

Deno.test("estimateTtsUsd scales by character count", () => {
  const usd = estimateTtsUsd(1000, "en-US-Chirp3-HD-Charon");
  assertAlmostEquals(usd, 0.03, 1e-9);
});
