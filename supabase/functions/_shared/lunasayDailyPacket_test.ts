import {
  buildLunaSayDailyPacketInstruction,
  LUNASAY_DAILY_FACE_IDS,
  lunaSayDailyPacketFallback,
  lunaSayDailyPacketJsonSchema,
  lunaSayDailyTarotCardName,
  lunaSayDateForEpoch,
  normalizeLunaSayTimezone,
  parseLunaSayDailyPacket,
} from "./lunasayDailyPacket.ts";

function modelFaces(includeTarotCard = true): Record<string, unknown> {
  return Object.fromEntries(LUNASAY_DAILY_FACE_IDS.map((id) => [id, {
    headline: id === "astrology" || id === "synastry"
      ? "Clear"
      : "A small daily note",
    display: "A small daily note for the dial.",
    ...(id === "synastry"
      ? {
        dynamic: "You both value steadiness when the day feels uncertain.",
        weather: "No live signal is supplied; let lived experience lead.",
        practice: "Ask what kind of support would feel useful right now.",
      }
      : { spoken: "A small daily note, ready to be spoken." }),
    detail: "A little more context for an expanded view.",
    ...(id === "tarot" && includeTarotCard ? { cardName: "The Star" } : {}),
  }]));
}

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
      "three distinct beats",
      "parentify a child",
      "Never recite raw measurements",
      "coherent without making every face repeat",
      "user's authority over their own experience",
    ]
  ) {
    if (!instruction.includes(required)) {
      throw new Error(`missing Family Synastry instruction: ${required}`);
    }
  }
});

Deno.test("LunaSay daily packet accepts every face with bounded text", () => {
  const faces = modelFaces();
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
  const faces = modelFaces();
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
  const faces = modelFaces();
  const packet = parseLunaSayDailyPacket(JSON.stringify(faces), {
    date: "2026-08-01",
    timezone: "America/Denver",
  });
  if (packet.faces.moon.title !== "Moon") {
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

Deno.test("LunaSay packet normalizes title-case keys and a nested packet envelope", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const titleCaseFaces = Object.fromEntries(
    Object.entries(packet.faces).map(([id, face]) => [
      `${id[0].toUpperCase()}${id.slice(1)} Face`,
      face,
    ]),
  );
  const parsed = parseLunaSayDailyPacket(
    JSON.stringify({ packet: { faces: titleCaseFaces } }),
    { date: "2030-01-01", timezone: "UTC" },
  );
  if (parsed.faces.synastry.title !== "Relationship Weather") {
    throw new Error("nested title-case faces were not normalized");
  }
});

Deno.test("LunaSay structured schema requires every exact face id", () => {
  const schema = lunaSayDailyPacketJsonSchema();
  const faces = (schema.properties as Record<string, unknown>).faces as Record<
    string,
    unknown
  >;
  const required = faces.required as string[];
  const properties = faces.properties as Record<string, unknown>;
  for (const id of LUNASAY_DAILY_FACE_IDS) {
    if (!required.includes(id) || !(id in properties)) {
      throw new Error(`structured schema omitted ${id}`);
    }
    const face = properties[id] as Record<string, unknown>;
    const faceProperties = face.properties as Record<string, unknown>;
    if (
      "mode" in faceProperties || "title" in faceProperties ||
      "accent" in faceProperties
    ) {
      throw new Error(`model was allowed to control ${id} metadata`);
    }
    if (
      id === "synastry" &&
      (!("dynamic" in faceProperties) || !("weather" in faceProperties) ||
        !("practice" in faceProperties) || "spoken" in faceProperties)
    ) {
      throw new Error("synastry schema did not separate its three beats");
    }
  }
});

Deno.test("LunaSay composes synastry from durable, temporary, and practice beats", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const faces = {
    ...packet.faces,
    synastry: {
      headline: "Tender",
      display: "Move gently and let lived experience lead.",
      dynamic: "You can both protect closeness by moving slowly.",
      weather: "No live relationship signal is supplied; notice what is real.",
      practice: "Ask before offering advice, then listen for one minute.",
      detail: "A Moon sextile supports ease without guaranteeing an outcome.",
    },
  };
  const parsed = parseLunaSayDailyPacket(JSON.stringify({ faces }), {
    date: "2030-01-01",
    timezone: "UTC",
  });
  for (
    const beat of [
      "The lasting pattern:",
      "Today's weather:",
      "A small practice:",
    ]
  ) {
    if (!parsed.faces.synastry.spoken.includes(beat)) {
      throw new Error(`composed synastry omitted ${beat}`);
    }
  }
});

Deno.test("LunaSay bounds verbose synastry beats without discarding the packet", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const verbose =
    "This deliberately verbose relationship sentence contains more language than the small device needs for one spoken beat and should be clipped safely";
  const parsed = parseLunaSayDailyPacket(
    JSON.stringify({
      faces: {
        ...packet.faces,
        synastry: {
          headline: "Tender",
          display: "Let lived experience lead.",
          dynamic: verbose,
          weather: verbose,
          practice: verbose,
          detail: "A bounded parser protects the device cache.",
        },
      },
    }),
    {
      date: "2030-01-01",
      timezone: "UTC",
    },
  );
  if (
    parsed.faces.synastry.spoken.length > 350 ||
    !parsed.faces.synastry.spoken.includes("A small practice:")
  ) {
    throw new Error("verbose synastry packet was not bounded");
  }
});

Deno.test("LunaSay server owns face metadata even when model metadata is malformed", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const faces = Object.fromEntries(
    Object.entries(packet.faces).map(([id, face]) => [id, {
      ...face,
      mode: "wrong",
      title: "A model-controlled title that is intentionally far too long",
      accent: "wrong",
    }]),
  );
  const parsed = parseLunaSayDailyPacket(JSON.stringify({ faces }), {
    date: "2030-01-01",
    timezone: "UTC",
  });
  if (
    parsed.faces.synastry.title !== "Relationship Weather" ||
    parsed.faces.synastry.mode !== "daily" ||
    parsed.faces.synastry.accent !== "rose"
  ) {
    throw new Error("canonical face metadata was not restored");
  }
});

Deno.test("LunaSay packet supplies a stable daily Tarot card when omitted", () => {
  const faces = modelFaces(false);
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
