import {
  buildLunaSayDailyFaceInstruction,
  buildLunaSayDailyPacketInstruction,
  LUNASAY_DAILY_FACE_IDS,
  LUNASAY_GENERATED_DAILY_FACE_IDS,
  lunaSayDailyFaceJsonSchema,
  lunaSayDailyPacketFallback,
  lunaSayDailyPacketJsonSchema,
  lunaSayDailyTarotCardName,
  lunaSayDateForEpoch,
  lunaSayFocusedFacts,
  lunaSayRequiredEvidence,
  lunaSayRequiredWeatherEvidence,
  normalizeLunaSayTimezone,
  parseLunaSayDailyFace,
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
        weatherEvidence:
          "Family biometrics: no live wellness packets received yet.",
        practice: "Ask what kind of support would feel useful right now.",
      }
      : { spoken: "A small daily note, ready to be spoken." }),
    detail: "A little more context for an expanded view.",
    ...(LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
        id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
      )
      ? { evidence: "Primary natal chart: Daniel, Sun Cancer." }
      : {}),
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

Deno.test("LunaSay can request and validate one Gemini 2.5 face at a time", () => {
  const instruction = buildLunaSayDailyFaceInstruction({
    id: "astrology",
    date: "2026-08-01",
    timezone: "America/Denver",
  });
  if (
    !instruction.includes("only the astrology face") ||
    !instruction.includes("Inner Weather")
  ) {
    throw new Error("individual face instruction is not focused");
  }
  const schema = lunaSayDailyFaceJsonSchema("astrology") as {
    properties?: Record<string, unknown>;
  };
  if (!schema.properties?.spoken || schema.properties?.cardName) {
    throw new Error("individual astrology schema is incorrect");
  }
  const face = parseLunaSayDailyFace(
    JSON.stringify({
      headline: "Open",
      display: "Leave room for a different answer.",
      spoken:
        "The weather feels open. Let the next honest answer surprise you.",
      detail: "Mercury trine the natal Moon supports easier expression.",
      evidence: "Primary natal chart: Daniel, Sun Cancer.",
    }),
    "astrology",
    "2026-08-01",
    "Primary natal chart: Daniel, Sun Cancer. Current evidence: Mercury trine natal Moon, orb 0.8 degrees.",
  );
  if (face.title !== "Inner Weather" || face.headline !== "Open") {
    throw new Error("individual face metadata was not normalized");
  }
});

Deno.test("individual Family Synastry keeps three bounded beats", () => {
  const face = parseLunaSayDailyFace(
    JSON.stringify({
      face: {
        headline: "Tender",
        display: "Care can be specific without making anyone the problem.",
        dynamic: "Both of you seek steadiness before opening up.",
        weather: "No current transit facts are supplied; let experience lead.",
        practice:
          "Ask what support would be useful, then listen without fixing.",
        detail:
          "The supplied natal contacts emphasize safety and responsiveness.",
        evidence: "Tight major aspects: Moon sextile Moon orb 1.2",
        weatherEvidence:
          "Family biometrics: no live wellness packets received yet.",
      },
    }),
    "synastry",
    "2026-08-01",
    "Tight major aspects: Moon sextile Moon orb 1.2. Family biometrics: no live wellness packets received yet.",
  );
  if (
    !face.spoken.includes("The lasting pattern:") ||
    !face.spoken.includes("Today's weather:") ||
    !face.spoken.includes("A small practice:")
  ) {
    throw new Error("individual synastry beats were not composed");
  }
});

Deno.test("individual daily face rejects unsupported evidence", () => {
  let rejected = false;
  try {
    parseLunaSayDailyFace(
      JSON.stringify({
        headline: "Open",
        display: "Stay curious.",
        spoken: "Stay curious about what the day actually brings.",
        detail: "A grounded symbolic orientation.",
        evidence: "Venus conjunct Jupiter",
      }),
      "astrology",
      "2026-08-01",
      "Supplied fact: Moon trine Mercury.",
    );
  } catch (error) {
    rejected = String(error).includes("unsupported-evidence");
  }
  if (!rejected) {
    throw new Error("invented reading evidence was accepted");
  }
});

Deno.test("daily face rejects a real fact assigned to the wrong face", () => {
  let rejected = false;
  try {
    parseLunaSayDailyFace(
      JSON.stringify({
        headline: "Open",
        display: "Stay curious.",
        spoken: "Stay curious about what the day actually brings.",
        detail: "A grounded symbolic orientation.",
        evidence: "Visible tarot card is The Star",
      }),
      "astrology",
      "2026-08-01",
      "Primary natal chart: Daniel, Sun Cancer. Visible tarot card is The Star.",
    );
  } catch (error) {
    rejected = String(error).includes("wrong-face-evidence");
  }
  if (!rejected) {
    throw new Error("cross-face evidence was accepted");
  }
});

Deno.test("daily face rejects stock horoscope language", () => {
  let rejected = false;
  try {
    parseLunaSayDailyFace(
      JSON.stringify({
        headline: "The Star",
        display: "A reflective draw.",
        spoken: "My dear, trust in the journey and embrace this beautiful day.",
        detail: "Use the card as a reflective prompt, not a prediction.",
        evidence: "Visible tarot card is The Star",
        cardName: "The Star",
      }),
      "tarot",
      "2026-08-01",
      "Visible tarot card is The Star, with reflective keyword hope.",
    );
  } catch (error) {
    rejected = String(error).includes("generic-cliche");
  }
  if (!rejected) {
    throw new Error("stock horoscope language was accepted");
  }
});

Deno.test("Sky face rejects natal or transit interpretation", () => {
  let rejected = false;
  try {
    parseLunaSayDailyFace(
      JSON.stringify({
        headline: "Quiet Night",
        display: "The local night is still.",
        spoken:
          "Transiting Mercury trines your natal Moon, making this a lovely time to reflect.",
        detail: "A local-time observation should remain observational.",
        evidence: "Local time 2026-08-01 01:30",
      }),
      "sky",
      "2026-08-01",
      "Local time 2026-08-01 01:30; timezone America/Denver.",
    );
  } catch (error) {
    rejected = String(error).includes("cross-face-language");
  }
  if (!rejected) {
    throw new Error("Sky accepted a second astrology reading");
  }
});

Deno.test("server assigns distinct exact evidence to each daily face", () => {
  const facts =
    "Local time 2026-08-01 07:00; timezone America/Denver. Primary natal chart: Alex, Sun Cancer, Moon Virgo. Current sky positions: Sun Leo, Moon Scorpio. Tight current-to-natal aspects, strongest first: transiting Mercury trine natal Moon, orb 0.8 degrees; transiting Saturn square natal Venus, orb 1.4 degrees. Lunar phase estimate: WAXING GIBBOUS, cycle fraction 0.384. Tight major aspects: Moon sextile Moon orb 1.1; Mercury square Mars orb 1.6. Family biometrics: no live wellness packets received yet. Visible tarot card is The Star, with reflective keyword hope.";
  const expected: Record<string, string> = {
    moon: "Lunar phase estimate:",
    astrology: "Primary natal chart:",
    transits: "Tight current-to-natal aspects",
    synastry: "Tight major aspects:",
    tarot: "Visible tarot card",
    sky: "Local time",
  };
  for (const [id, prefix] of Object.entries(expected)) {
    const evidence = lunaSayRequiredEvidence(
      id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
      facts,
    );
    if (!evidence?.startsWith(prefix) || !facts.includes(evidence)) {
      throw new Error(`invalid exact evidence for ${id}: ${evidence}`);
    }
  }
  const weather = lunaSayRequiredWeatherEvidence("synastry", facts);
  if (
    weather !== "Family biometrics: no live wellness packets received yet."
  ) {
    throw new Error(`invalid exact synastry weather evidence: ${weather}`);
  }
  const synastryFacts = lunaSayFocusedFacts("synastry", facts);
  if (
    !synastryFacts.includes("Tight major aspects:") ||
    !synastryFacts.includes("Family biometrics:") ||
    synastryFacts.includes("current-to-natal")
  ) {
    throw new Error(`synastry focus leaked unrelated facts: ${synastryFacts}`);
  }
  const skyFacts = lunaSayFocusedFacts("sky", facts);
  if (skyFacts !== "Local time 2026-08-01 07:00; timezone America/Denver.") {
    throw new Error(`sky focus leaked astrology: ${skyFacts}`);
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
        !("practice" in faceProperties) ||
        !("weatherEvidence" in faceProperties) ||
        "spoken" in faceProperties)
    ) {
      throw new Error("synastry schema did not separate its three beats");
    }
    if (
      LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
        id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
      ) &&
      !("evidence" in faceProperties)
    ) {
      throw new Error(`generated face schema omitted evidence for ${id}`);
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

Deno.test("LunaSay rejects verbose synastry beats instead of clipping meaning", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const verbose =
    "This deliberately verbose relationship sentence contains more language than the small device needs for one spoken beat and keeps adding generalized clauses that obscure both people's actual perspectives instead of naming the reciprocal pattern clearly";
  let rejected = false;
  try {
    parseLunaSayDailyPacket(
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
  } catch (error) {
    rejected = String(error).includes("Invalid LunaSay daily face: synastry");
  }
  if (!rejected) {
    throw new Error("overlong synastry was silently clipped");
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
    packet.faces.tarot.cardName !==
      lunaSayDailyTarotCardName("2026-08-01") ||
    packet.faces.conversation.mode !== "live_question" ||
    packet.faces.astrology.title !== "Inner Weather" ||
    packet.faces.synastry.title !== "Relationship Weather" ||
    !packet.faces.synastry.spoken.includes("repair")
  ) {
    throw new Error("fallback packet is incomplete");
  }
  for (const face of Object.values(packet.faces)) {
    if (
      face.display.includes("Moon phase: waxing crescent") ||
      face.spoken.includes("Moon phase: waxing crescent") ||
      face.detail.includes("Moon phase: waxing crescent")
    ) {
      throw new Error("fallback leaked raw device facts");
    }
  }
});
