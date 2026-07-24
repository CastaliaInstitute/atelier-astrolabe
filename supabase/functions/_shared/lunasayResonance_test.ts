import { assertEquals, assertStringIncludes } from "jsr:@std/assert@1.0.14";

import {
  lunaSayResonanceInstruction,
  parseLunaSayResonanceProfile,
} from "./lunasayResonance.ts";

Deno.test("resonance profile accepts only bounded generated-face counts", () => {
  assertEquals(
    parseLunaSayResonanceProfile({
      moon: { helpful: 3, mixed: 1, missed: 0, note: "must be ignored" },
      transits: { helpful: 0, mixed: 2, missed: 4 },
      conversation: { helpful: 9, mixed: 0, missed: 0 },
      tarot: { helpful: 25, mixed: 0, missed: 0 },
      unknown: { helpful: 4, mixed: 0, missed: 0 },
    }),
    {
      moon: { helpful: 3, mixed: 1, missed: 0 },
      transits: { helpful: 0, mixed: 2, missed: 4 },
    },
  );
});

Deno.test("resonance calibration is face-local and needs two samples", () => {
  const profile = parseLunaSayResonanceProfile({
    moon: { helpful: 0, mixed: 0, missed: 1 },
    transits: { helpful: 0, mixed: 1, missed: 3 },
  });
  assertEquals(lunaSayResonanceInstruction("moon", profile), "");
  assertEquals(lunaSayResonanceInstruction("sky", profile), "");
  assertStringIncludes(
    lunaSayResonanceInstruction("transits", profile),
    "especially concrete and modest",
  );
});

Deno.test("resonance calibration never treats ratings as evidence", () => {
  const guidance = lunaSayResonanceInstruction(
    "synastry",
    parseLunaSayResonanceProfile({
      synastry: { helpful: 5, mixed: 1, missed: 0 },
    }),
  );
  assertStringIncludes(guidance, "Never mention feedback");
  assertStringIncludes(guidance, "evidence contract");
  assertStringIncludes(guidance, "Helpful responses predominate");
});
