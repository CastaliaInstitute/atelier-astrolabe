import { assertEquals, assertStringIncludes } from "jsr:@std/assert@1.0.14";

import {
  lunaSayContinuityInstruction,
  lunaSayContinuityIssue,
  lunaSayEvidenceHash,
  parseLunaSayReadingMemory,
} from "./lunasayContinuity.ts";
import { lunaSayDailyPacketFallback } from "./lunasayDailyPacket.ts";

const MEMORY = {
  date: "2026-07-23",
  faces: {
    moon: {
      headline: "Waxing light",
      action: "Notice what has become visible before adding more.",
      evidenceHash:
        "0c53edbc1b3db6116d588f1d4024f10c0cfdb346a2f12137f6ab78e0234681c2",
      note: "user prose must not be accepted",
    },
    conversation: {
      headline: "Private transcript",
      action: "Repeat a private conversation.",
      evidenceHash: "not-a-valid-fingerprint",
    },
  },
};

Deno.test("reading memory accepts only recent generated-face summaries", () => {
  assertEquals(parseLunaSayReadingMemory(MEMORY, "2026-07-24"), {
    date: "2026-07-23",
    faces: {
      moon: {
        headline: "Waxing light",
        action: "Notice what has become visible before adding more.",
        evidenceHash:
          "0c53edbc1b3db6116d588f1d4024f10c0cfdb346a2f12137f6ab78e0234681c2",
      },
    },
  });
  assertEquals(parseLunaSayReadingMemory(MEMORY, "2026-08-20"), null);
  assertEquals(
    parseLunaSayReadingMemory({ ...MEMORY, date: "2026-07-24" }, "2026-07-24"),
    null,
  );
});

Deno.test("continuity distinguishes steady evidence from an evidence shift", async () => {
  const evidence =
    "Lunar phase estimate: waxing gibbous, 70 percent illuminated.";
  assertEquals(
    await lunaSayEvidenceHash(evidence),
    MEMORY.faces.moon.evidenceHash,
  );
  const memory = parseLunaSayReadingMemory(MEMORY, "2026-07-24");
  const steady = await lunaSayContinuityInstruction(
    "moon",
    memory,
    evidence,
  );
  assertStringIncludes(steady, "device-owned continuity");
  assertStringIncludes(steady, "unchanged");
  assertStringIncludes(steady, "Do not repeat the prior action verbatim");
  const shifting = await lunaSayContinuityInstruction(
    "moon",
    memory,
    "Lunar phase estimate: waxing gibbous, 76 percent illuminated.",
  );
  assertStringIncludes(shifting, "evidence differs");
  assertStringIncludes(shifting, "do not invent a lived event");
  assertEquals(
    await lunaSayContinuityInstruction("sky", memory, "Sky fact"),
    "",
  );
});

Deno.test("continuity rejects only an exact repeated practice", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2026-07-24",
    timezone: "America/Denver",
    facts: "Lunar phase estimate: waxing gibbous, 72 percent illuminated.",
  });
  const prior = parseLunaSayReadingMemory(MEMORY, "2026-07-24")?.faces.moon;
  packet.faces.moon.action =
    "Notice what has become visible before adding more.";
  assertEquals(
    lunaSayContinuityIssue(packet.faces.moon, prior),
    "repeated-prior-action",
  );
  packet.faces.moon.action =
    "Compare tonight's visible edge with yesterday's observation.";
  assertEquals(lunaSayContinuityIssue(packet.faces.moon, prior), null);
});
