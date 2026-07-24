import {
  buildLunaSayDailyPacketInstruction,
  LUNASAY_DAILY_FACE_IDS,
  lunaSayDailyPacketFallback,
  lunaSayDailyTarotCardName,
  lunaSayDateForEpoch,
  normalizeLunaSayTimezone,
  parseLunaSayDailyPacket,
} from "./lunasayDailyPacket.ts";

Deno.test("LunaSay daily instruction makes Family Synastry relational and safe", () => {
  const instruction = buildLunaSayDailyPacketInstruction({
    date: "2026-08-01",
    timezone: "America/Denver",
  });
  for (
    const required of [
      "Family Synastry",
      "Inner Weather",
      "Relationship Weather",
      "symbolic outlook",
      "reciprocal system",
      "three compact beats",
      "parentify a child",
      "Never recite raw measurements",
    ]
  ) {
    if (!instruction.includes(required)) {
      throw new Error(`missing Family Synastry instruction: ${required}`);
    }
  }
});

Deno.test("LunaSay daily packet accepts every face with bounded text", () => {
  const faces = Object.fromEntries(LUNASAY_DAILY_FACE_IDS.map((id) => [id, {
    mode: id === "journal"
      ? "offline"
      : id === "alethiometer" || id === "conversation"
      ? "live_question"
      : "daily",
    title: id,
    headline: "A small daily note",
    display: "A small daily note for the dial.",
    spoken: "A small daily note, ready to be spoken.",
    detail: "A little more context for an expanded view.",
    accent: "moon",
    ...(id === "tarot" ? { cardName: "The Star" } : {}),
  }]));
  const packet = parseLunaSayDailyPacket(
    JSON.stringify({
      schemaVersion: 1,
      date: "2026-08-01",
      timezone: "America/Denver",
      faces,
    }),
    { date: "2026-08-01", timezone: "America/Denver" },
  );
  if (packet.faces.tarot.cardName !== "The Star") {
    throw new Error("missing Tarot card");
  }
});

Deno.test("LunaSay daily packet rejects a missing face", () => {
  let rejected = false;
  try {
    parseLunaSayDailyPacket(
      JSON.stringify({
        schemaVersion: 1,
        date: "2026-08-01",
        timezone: "America/Denver",
        faces: {},
      }),
      { date: "2026-08-01", timezone: "America/Denver" },
    );
  } catch {
    rejected = true;
  }
  if (!rejected) throw new Error("missing face was accepted");
});

Deno.test("LunaSay packet stamps the server date rather than trusting the model envelope", () => {
  const faces = Object.fromEntries(LUNASAY_DAILY_FACE_IDS.map((id) => [id, {
    mode: id === "journal"
      ? "offline"
      : id === "alethiometer" || id === "conversation"
      ? "live_question"
      : "daily",
    title: id,
    headline: "A small daily note",
    display: "A small daily note for the dial.",
    spoken: "A small daily note, ready to be spoken.",
    detail: "A little more context for an expanded view.",
    accent: "moon",
    ...(id === "tarot" ? { cardName: "The Star" } : {}),
  }]));
  const packet = parseLunaSayDailyPacket(
    JSON.stringify({
      schemaVersion: 999,
      date: "wrong-date",
      timezone: "Mars/Olympus",
      faces,
    }),
    { date: "2026-08-01", timezone: "America/Denver" },
  );
  if (packet.date !== "2026-08-01" || packet.timezone !== "America/Denver") {
    throw new Error("server envelope was not authoritative");
  }
});

Deno.test("LunaSay packet accepts model faces returned at the root", () => {
  const faces = Object.fromEntries(LUNASAY_DAILY_FACE_IDS.map((id) => [id, {
    mode: id === "journal"
      ? "offline"
      : id === "alethiometer" || id === "conversation"
      ? "live_question"
      : "daily",
    title: id,
    headline: "A small daily note",
    display: "A small daily note for the dial.",
    spoken: "A small daily note, ready to be spoken.",
    detail: "A little more context for an expanded view.",
    accent: "moon",
    ...(id === "tarot" ? { cardName: "The Star" } : {}),
  }]));
  const packet = parseLunaSayDailyPacket(JSON.stringify(faces), {
    date: "2026-08-01",
    timezone: "America/Denver",
  });
  if (packet.faces.moon.title !== "moon") {
    throw new Error("root face payload was not normalized");
  }
});

Deno.test("LunaSay packet accepts model faces returned as identified array entries", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const arrayFaces = Object.entries(packet.faces).map(([id, face]) => ({
    id,
    ...face,
  }));
  const parsed = parseLunaSayDailyPacket(
    JSON.stringify({
      schemaVersion: 1,
      date: "2030-01-01",
      timezone: "UTC",
      faces: arrayFaces,
    }),
    { date: "2030-01-01", timezone: "UTC" },
  );
  if (parsed.faces.moon.title !== packet.faces.moon.title) {
    throw new Error("array face payload was not normalized");
  }
});

Deno.test("LunaSay packet supplies a stable daily Tarot card when omitted", () => {
  const faces = Object.fromEntries(LUNASAY_DAILY_FACE_IDS.map((id) => [id, {
    mode: id === "journal"
      ? "offline"
      : id === "alethiometer" || id === "conversation"
      ? "live_question"
      : "daily",
    title: id,
    headline: "A small daily note",
    display: "A small daily note for the dial.",
    spoken: "A small daily note, ready to be spoken.",
    detail: "A little more context for an expanded view.",
    accent: "moon",
  }]));
  const packet = parseLunaSayDailyPacket(JSON.stringify({ faces }), {
    date: "2026-08-01",
    timezone: "America/Denver",
  });
  if (packet.faces.tarot.cardName !== lunaSayDailyTarotCardName("2026-08-01")) {
    throw new Error("daily Tarot fallback was not deterministic");
  }
});

Deno.test("LunaSay date follows the supplied IANA timezone", () => {
  const epoch = Date.parse("2026-08-01T01:30:00Z") / 1000;
  if (lunaSayDateForEpoch(epoch, "America/Denver") !== "2026-07-31") {
    throw new Error("timezone date was not respected");
  }
});

Deno.test("LunaSay packet normalizes an invalid timezone", () => {
  if (normalizeLunaSayTimezone("not/a zone") !== "UTC") {
    throw new Error("invalid timezone was accepted");
  }
});

Deno.test("LunaSay fallback still provides every cacheable face", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2026-08-01",
    timezone: "America/Denver",
    facts: "Moon phase: waxing crescent.",
  });
  if (
    packet.faces.tarot.cardName !== "The Star" ||
    packet.faces.conversation.mode !== "live_question" ||
    packet.faces.astrology.title !== "Inner Weather" ||
    packet.faces.synastry.title !== "Relationship Weather" ||
    !packet.faces.synastry.spoken.includes("repair")
  ) {
    throw new Error("fallback packet is incomplete");
  }
});
