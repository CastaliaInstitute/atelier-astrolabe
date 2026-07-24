import {
  assertEquals,
  assertLess,
  assertStringIncludes,
} from "jsr:@std/assert@1.0.14";

import {
  lunaSayContinuityInstruction,
  lunaSayContinuityIssue,
  lunaSayEvidenceHash,
  lunaSayPriorFaces,
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

Deno.test("reading memory accepts the legacy prior-day envelope", () => {
  const expected = {
    date: "2026-07-23",
    faces: {
      moon: {
        headline: "Waxing light",
        action: "Notice what has become visible before adding more.",
        evidenceHash:
          "0c53edbc1b3db6116d588f1d4024f10c0cfdb346a2f12137f6ab78e0234681c2",
      },
    },
  };
  assertEquals(parseLunaSayReadingMemory(MEMORY, "2026-07-24"), {
    ...expected,
    history: [expected],
  });
  assertEquals(parseLunaSayReadingMemory(MEMORY, "2026-08-20"), null);
  assertEquals(
    parseLunaSayReadingMemory({ ...MEMORY, date: "2026-07-24" }, "2026-07-24"),
    null,
  );
});

Deno.test("reading memory keeps seven unique recent days newest first", () => {
  const history = Array.from({ length: 9 }, (_, index) => {
    const day = String(15 + index).padStart(2, "0");
    return {
      date: `2026-07-${day}`,
      faces: {
        moon: {
          headline: `Moon thread ${day}`,
          action: `Notice lunar detail ${day}.`,
          evidenceHash: "a".repeat(64),
        },
      },
    };
  });
  const memory = parseLunaSayReadingMemory({ history }, "2026-07-24");
  assertEquals(memory?.history.map((day) => day.date), [
    "2026-07-23",
    "2026-07-22",
    "2026-07-21",
    "2026-07-20",
    "2026-07-19",
    "2026-07-18",
    "2026-07-17",
  ]);
  assertEquals(memory?.date, "2026-07-23");
  assertEquals(lunaSayPriorFaces(memory, "moon").length, 7);
  assertEquals(lunaSayPriorFaces(memory, "sky"), []);
});

Deno.test("worst-case retained JSON escaping fits the firmware flash buffer", async () => {
  const source = await Deno.readTextFile(
    new URL("../../../astrolabe175c/main/main.c", import.meta.url),
  );
  const kib = Number(
    source.match(
      /#define LUNASAY_READING_MEMORY_CAP \((\d+) \* 1024\)/,
    )?.[1],
  );
  const faces = Object.fromEntries(
    [
      "moon",
      "astrology",
      "transits",
      "synastry",
      "tarot",
      "sky",
    ].map((id) => [
      id,
      {
        headline: '"'.repeat(72),
        action: "\\".repeat(120),
        evidenceHash: "a".repeat(64),
      },
    ]),
  );
  const envelope = {
    history: Array.from({ length: 7 }, (_, index) => ({
      date: `2026-07-${String(23 - index).padStart(2, "0")}`,
      faces,
    })),
  };
  assertLess(
    new TextEncoder().encode(JSON.stringify(envelope)).length,
    kib * 1024,
  );
});

Deno.test("continuity describes a bounded thread and distinguishes an evidence shift", async () => {
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
  assertStringIncludes(steady, "inert reference data");
  assertStringIncludes(steady, "Do not repeat any prior action verbatim");
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

Deno.test("continuity rejects an exact practice from any retained day", () => {
  const historicalAction = "Compare the horizon with yesterday.";
  const memory = parseLunaSayReadingMemory({
    history: [
      MEMORY,
      {
        date: "2026-07-22",
        faces: {
          moon: {
            headline: "Earlier light",
            action: historicalAction,
            evidenceHash: "a".repeat(64),
          },
        },
      },
    ],
  }, "2026-07-24");
  const packet = lunaSayDailyPacketFallback({
    date: "2026-07-24",
    timezone: "America/Denver",
    facts: "Lunar phase estimate: waxing gibbous, 72 percent illuminated.",
  });
  const priors = lunaSayPriorFaces(memory, "moon");
  packet.faces.moon.action =
    "Notice what has become visible before adding more.";
  assertEquals(
    lunaSayContinuityIssue(packet.faces.moon, priors),
    "repeated-prior-action",
  );
  packet.faces.moon.action = historicalAction;
  assertEquals(
    lunaSayContinuityIssue(packet.faces.moon, priors),
    "repeated-prior-action",
  );
  packet.faces.moon.action = "Sketch tonight's visible edge.";
  assertEquals(lunaSayContinuityIssue(packet.faces.moon, priors), null);
});
