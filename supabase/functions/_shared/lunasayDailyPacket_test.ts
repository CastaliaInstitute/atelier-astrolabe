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
  lunaSayRequiredTemporalEvidence,
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
        spoken:
          "Pattern: You both value steadiness. Today: Let lived experience lead. Practice: Ask what support would help.",
      }
      : { spoken: "A small daily note, ready to be spoken." }),
    detail: "A little more context for an expanded view.",
    ...(id === "transits" || id === "synastry"
      ? {
        now: "This may add useful pressure to one choice.",
        next: "The sampled pattern is closest on day +2.",
        temporalEvidence: id === "transits"
          ? "Ten-day transit arc: now at day +0; closest in the daily samples on day +2."
          : "No live relationship signal is available.",
      }
      : {}),
    ...(LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
        id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
      )
      ? {
        evidence: "Primary natal chart: Daniel, Sun Cancer.",
        ...(id === "synastry"
          ? {}
          : { action: "Notice one concrete detail before choosing." }),
      }
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
    !instruction.includes("Inner Weather") ||
    !instruction.includes("words resource and tension") ||
    !instruction.includes("spoken must end with one short imperative")
  ) {
    throw new Error("individual face instruction is not focused");
  }
  const transitInstruction = buildLunaSayDailyFaceInstruction({
    id: "transits",
    date: "2026-08-01",
    timezone: "America/Denver",
  });
  if (!transitInstruction.includes("words pressure and opening")) {
    throw new Error("individual transit instruction loses two-sided nuance");
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
      action: "Notice the next honest answer before choosing.",
      detail: "Mercury trine the natal Moon supports easier expression.",
      evidence: "Primary natal chart: Daniel, Sun Cancer.",
    }),
    "astrology",
    "2026-08-01",
    "Primary natal chart: Daniel, Sun Cancer. Current evidence: Mercury trine natal Moon, orb 0.8 degrees.",
  );
  if (
    face.title !== "Inner Weather" ||
    face.headline !== "Open" ||
    face.action !== "Notice the next honest answer before choosing." ||
    !face.spoken.endsWith("Notice the next honest answer before choosing.")
  ) {
    throw new Error("individual face metadata was not normalized");
  }
});

Deno.test("generated face rejects a vague non-imperative action", () => {
  let rejected = false;
  try {
    parseLunaSayDailyFace(
      JSON.stringify({
        headline: "Open",
        display: "Leave room for a different answer.",
        spoken: "The weather may feel open.",
        action: "A sense of harmony and possibility.",
        detail:
          "Cancer care can be a resource while self-protection becomes a tension.",
        evidence: "Primary natal chart: Daniel, Sun Cancer.",
      }),
      "astrology",
      "2026-08-01",
      "Primary natal chart: Daniel, Sun Cancer.",
    );
  } catch (error) {
    rejected = String(error).includes("action-not-imperative");
  }
  if (!rejected) {
    throw new Error("vague structured action was accepted");
  }
});

Deno.test("server bounds a verbose prelude before appending the exact action", () => {
  const face = parseLunaSayDailyFace(
    JSON.stringify({
      headline: "Open",
      display: "Leave room for a different answer.",
      spoken:
        "Care can be a resource. Precision can become a tension. Both belong in the picture. The chart is symbolic, not certain.",
      action: "Notice which need asks for care first.",
      detail:
        "Cancer care can be a resource while Virgo precision can become a tension.",
      evidence: "Primary natal chart: Daniel, Sun Cancer.",
    }),
    "astrology",
    "2026-08-01",
    "Primary natal chart: Daniel, Sun Cancer.",
  );
  const sentences = face.spoken.match(/[.!?]+(?:\s|$)/g)?.length ?? 0;
  if (
    sentences !== 4 ||
    !face.spoken.endsWith("Notice which need asks for care first.")
  ) {
    throw new Error(`verbose prelude was not bounded: ${face.spoken}`);
  }
});

Deno.test("server retains complete bounded detail sentences", () => {
  const longTail = "A".repeat(500);
  const face = parseLunaSayDailyFace(
    JSON.stringify({
      headline: "Open",
      display: "Leave room for a different answer.",
      spoken: "Care can be a resource.",
      action: "Notice which need asks for care first.",
      detail:
        `Cancer care is a resource and precision can become a tension. ${longTail}`,
      evidence: "Primary natal chart: Daniel, Sun Cancer.",
    }),
    "astrology",
    "2026-08-01",
    "Primary natal chart: Daniel, Sun Cancer.",
  );
  if (
    face.detail !==
      "Cancer care is a resource and precision can become a tension."
  ) {
    throw new Error(`oversized detail was not safely bounded: ${face.detail}`);
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
        spoken:
          "Pattern: You both seek steadiness. Today: Let experience lead. Practice: Ask what support would help.",
        detail:
          "The supplied natal contacts emphasize safety and responsiveness.",
        now: "This timing touches Daniel's chart, not the whole relationship.",
        next: "The sampled contact is closest on day +2.",
        temporalEvidence:
          "Current relationship transit arc: now at day +0, transiting Moon sextile Daniel natal Moon at orb 1.2 degrees; closest in the daily samples on day +2.",
        evidence: "Tight major aspects: Moon sextile Moon orb 1.2",
        weatherEvidence:
          "Family biometrics: no live wellness packets received yet.",
      },
    }),
    "synastry",
    "2026-08-01",
    "Tight major aspects: Moon sextile Moon orb 1.2. Current relationship transit arc: now at day +0, transiting Moon sextile Daniel natal Moon at orb 1.2 degrees; closest in the daily samples on day +2. Family biometrics: no live wellness packets received yet.",
  );
  if (
    !face.spoken.includes("Pattern:") ||
    !face.spoken.includes("Both of you seek steadiness") ||
    !face.spoken.includes("Today:") ||
    !face.spoken.includes("let experience lead") ||
    !face.spoken.includes("Next:") ||
    !face.spoken.includes("Practice: Ask what support") ||
    face.action !==
      "Ask what support would be useful, then listen without fixing."
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
    "Local time 2026-08-01 07:00; timezone America/Denver. Primary natal chart: Alex, Sun Cancer, Moon Virgo. Current sky positions: Sun Leo, Moon Scorpio. Tight current-to-natal aspects, strongest first: transiting Mercury trine natal Moon, orb 0.8 degrees; transiting Saturn square natal Venus, orb 1.4 degrees. Ten-day transit arc: now at day +0, transiting Mercury trine natal Moon at orb 0.8 degrees; closest in the daily samples on day +1 at orb 0.2 degrees; outside the 4.5-degree window by day +4. Lunar phase estimate: WAXING GIBBOUS, cycle fraction 0.384. Tight major aspects: Moon sextile Moon orb 1.1; Mercury square Mars orb 1.6. Current relationship transit arc: now at day +0, transiting Saturn square Alex natal Venus at orb 1.4 degrees; closest in the daily samples on day +2 at orb 0.1 degrees; outside the 4.5-degree window by day +7. Family biometrics: no live wellness packets received yet. Visible tarot card is The Star, with reflective keyword hope.";
  const expected: Record<string, string> = {
    moon: "Lunar phase estimate:",
    astrology: "Primary natal chart:",
    transits: "Tight current-to-natal aspects",
    synastry: "Tight major aspects:",
    tarot: "Visible tarot card",
    sky: "Current sky positions:",
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
    !weather?.startsWith("Current relationship transit arc:") ||
    !weather.includes("Alex natal Venus")
  ) {
    throw new Error(`invalid exact synastry weather evidence: ${weather}`);
  }
  const synastryFacts = lunaSayFocusedFacts("synastry", facts);
  if (
    !synastryFacts.includes("Tight major aspects:") ||
    !synastryFacts.includes("Current relationship transit arc:") ||
    synastryFacts.includes("current-to-natal")
  ) {
    throw new Error(`synastry focus leaked unrelated facts: ${synastryFacts}`);
  }
  const transitTiming = lunaSayRequiredTemporalEvidence("transits", facts);
  if (
    !transitTiming?.startsWith("Ten-day transit arc:") ||
    !transitTiming.includes("day +4")
  ) {
    throw new Error(
      `transit timing was not retained exactly: ${transitTiming}`,
    );
  }
  const relationshipTiming = lunaSayRequiredTemporalEvidence(
    "synastry",
    facts,
  );
  if (
    !relationshipTiming?.startsWith("Current relationship transit arc:") ||
    !relationshipTiming.includes("Alex natal Venus")
  ) {
    throw new Error(
      `relationship timing was not retained exactly: ${relationshipTiming}`,
    );
  }
  const skyFacts = lunaSayFocusedFacts("sky", facts);
  if (
    !skyFacts.includes(
      "Local time 2026-08-01 07:00; timezone America/Denver.",
    ) ||
    !skyFacts.includes("Current sky positions: Sun Leo, Moon Scorpio.") ||
    skyFacts.includes("Primary natal chart:")
  ) {
    throw new Error(`sky focus leaked astrology: ${skyFacts}`);
  }
});

Deno.test("line-oriented device facts never bleed into the next face", () => {
  const facts = [
    "Lunar phase estimate: waxing gibbous, 72 percent illuminated.",
    "Primary natal chart: Sun in Cancer; Moon in Virgo.",
    "Visible tarot card: The Star.",
    "Current sky positions: Venus is visible low in western sky after sunset.",
  ].join("\n");
  const moon = lunaSayRequiredEvidence("moon", facts);
  const tarot = lunaSayRequiredEvidence("tarot", facts);
  const sky = lunaSayFocusedFacts("sky", facts);
  if (
    moon !== "Lunar phase estimate: waxing gibbous, 72 percent illuminated." ||
    tarot !== "Visible tarot card: The Star." ||
    !sky.includes(
      "Current sky positions: Venus is visible low in western sky after sunset.",
    ) ||
    sky.includes("Primary natal chart:") ||
    sky.includes("Visible tarot card:")
  ) {
    throw new Error(
      `line-oriented facts crossed a face boundary: ${
        JSON.stringify({ moon, tarot, sky })
      }`,
    );
  }
});

Deno.test("server owns next timing and safely normalizes a bare will", () => {
  const transitEvidence =
    "Ten-day transit arc: now at day +0, transiting Saturn square natal Venus at orb 1.4 degrees; closest in the daily samples on day +2 at orb 0.1 degrees; outside the 4.5-degree window by day +7.";
  const baseTransit = {
    headline: "Pressure with room",
    display: "Make one deliberate choice before adding more.",
    spoken:
      "Pressure may sharpen priorities now; day +2 is the closest sampled point.",
    action: "Pause before adding another commitment.",
    detail: "Saturn square natal Venus can symbolize careful value choices.",
    evidence:
      "Tight current-to-natal aspects, strongest first: transiting Saturn square natal Venus, orb 1.4 degrees.",
    now: "This may make value choices feel more deliberate.",
    next: "The contact is closest in the samples on day +2.",
    temporalEvidence: transitEvidence,
  };
  const facts = `${baseTransit.evidence} ${transitEvidence}`;
  const canonical = parseLunaSayDailyFace(
    JSON.stringify({
      ...baseTransit,
      next: "Everything turns around on day +5.",
    }),
    "transits",
    "2026-08-01",
    facts,
  );
  if (
    canonical.next !==
      "Closest in daily samples on day +2; outside the active window by day +7."
  ) {
    throw new Error(`server did not own next timing: ${canonical.next}`);
  }
  const softened = parseLunaSayDailyFace(
    JSON.stringify({
      ...baseTransit,
      now: "This will make value choices feel more deliberate.",
    }),
    "transits",
    "2026-08-01",
    facts,
  );
  if (softened.now !== "This may make value choices feel more deliberate.") {
    throw new Error(`server did not soften bare will: ${softened.now}`);
  }
  for (
    const [label, change] of [
      ["invented date", { now: "Everything changes on 2026-08-09." }],
      ["certainty", { now: "This inevitably forces a relationship decision." }],
    ] as const
  ) {
    let rejected = false;
    try {
      parseLunaSayDailyFace(
        JSON.stringify({ ...baseTransit, ...change }),
        "transits",
        "2026-08-01",
        facts,
      );
    } catch (error) {
      rejected = String(error).includes("Invalid LunaSay daily face");
    }
    if (!rejected) throw new Error(`${label} passed temporal validation`);
  }

  const relationshipEvidence =
    "Current relationship transit arc: now at day +0, transiting Moon sextile Rowan natal Moon at orb 1.2 degrees; closest in the daily samples on day +1 at orb 0.3 degrees.";
  const relationship = parseLunaSayDailyFace(
    JSON.stringify({
      headline: "Tender",
      display: "Let one person's timing stay personal.",
      dynamic: "Both people may seek safety before opening up.",
      weather: "This transit touches Rowan's chart, not the whole bond.",
      practice: "Ask Rowan what support would help before assuming.",
      spoken:
        "Pattern: Both people seek safety. Today: Rowan's chart holds the timing. Practice: Ask what support would help.",
      detail: "The timing is individual context, not a family verdict.",
      evidence: "Tight major aspects: Moon sextile Moon orb 1.2",
      weatherEvidence: relationshipEvidence,
      now: "One person may need more room today.",
      next: "The sampled contact is closest on day +8.",
      temporalEvidence: relationshipEvidence,
    }),
    "synastry",
    "2026-08-01",
    `Tight major aspects: Moon sextile Moon orb 1.2. ${relationshipEvidence}`,
  );
  if (
    !relationship.now?.includes("Rowan") ||
    relationship.next !== "Closest in daily samples on day +1."
  ) {
    throw new Error(
      `relationship subject or canonical timing was lost: ${
        JSON.stringify({ now: relationship.now, next: relationship.next })
      }`,
    );
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
        !("spoken" in faceProperties))
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
    if (
      LUNASAY_GENERATED_DAILY_FACE_IDS.includes(
        id as typeof LUNASAY_GENERATED_DAILY_FACE_IDS[number],
      )
    ) {
      const actionField = id === "synastry" ? "practice" : "action";
      const faceRequired = (face.required ?? []) as string[];
      if (
        !(actionField in faceProperties) ||
        !faceRequired.includes(actionField)
      ) {
        throw new Error(
          `generated face schema omitted structured action for ${id}`,
        );
      }
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
      weather:
        "Alex may be navigating a period that feels serious or calls for patience and a clearer boundary between care and over-responsibility.",
      practice: "Ask before offering advice, then listen for one minute.",
      spoken:
        "Pattern: You both protect closeness slowly. Today: Move gently. Practice: Ask before offering advice.",
      detail: "A Moon sextile supports ease without guaranteeing an outcome.",
      now: "No verified relationship timing is loaded.",
      next: "Let lived experience lead until a current signal is available.",
      temporalEvidence: "No live relationship signal is available.",
    },
  };
  const parsed = parseLunaSayDailyPacket(JSON.stringify({ faces }), {
    date: "2030-01-01",
    timezone: "UTC",
  });
  for (
    const beat of [
      "Pattern: You can both protect closeness by moving slowly.",
      "Today: No current signal",
      "Next:",
      "Practice:",
    ]
  ) {
    if (!parsed.faces.synastry.spoken.includes(beat)) {
      throw new Error(`composed synastry omitted ${beat}`);
    }
  }
});

Deno.test("LunaSay keeps verbose synastry details while bounding device speech", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2030-01-01",
    timezone: "UTC",
    facts: "A quiet test fact.",
  });
  const verbose =
    "This deliberately verbose relationship sentence contains more language than the small device needs for one spoken beat and keeps adding generalized clauses that obscure both people's actual perspectives instead of naming the reciprocal pattern clearly";
  const parsed = parseLunaSayDailyPacket(
    JSON.stringify({
      faces: {
        ...packet.faces,
        synastry: {
          headline: "Tender",
          display: "Let lived experience lead.",
          dynamic: verbose,
          weather: verbose.slice(0, 190),
          practice: "Ask before offering advice, then listen for one minute.",
          spoken:
            "Pattern: Both people move carefully. Today: Capacity may be lower. Practice: Ask before offering advice.",
          detail: "A bounded speech composer protects the device cache.",
          now: "No verified relationship timing is loaded.",
          temporalEvidence: "No live relationship signal is available.",
        },
      },
    }),
    {
      date: "2030-01-01",
      timezone: "UTC",
    },
  );
  if (
    parsed.faces.synastry.detail !==
      "A bounded speech composer protects the device cache." ||
    new TextEncoder().encode(parsed.faces.synastry.spoken).length >= 384
  ) {
    throw new Error("synastry speech did not preserve detail and fit cache");
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
  for (const id of LUNASAY_GENERATED_DAILY_FACE_IDS) {
    const face = packet.faces[id];
    if (
      !face.action ||
      !face.spoken.toLowerCase().replace(/[^a-z0-9]+/g, " ").includes(
        face.action.toLowerCase().replace(/[^a-z0-9]+/g, " "),
      )
    ) {
      throw new Error(`fallback lost structured spoken action for ${id}`);
    }
  }
});
