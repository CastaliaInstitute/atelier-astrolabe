import {
  type LunaSayDailyFace,
  type LunaSayDailyFaceId,
  type LunaSayDailyPacket,
  lunaSayDailyPacketFallback,
} from "./lunasayDailyPacket.ts";
import { scoreLunaSayReadingQuality } from "./lunasayReadingQuality.ts";

const FACTS = [
  "Lunar phase estimate: waxing gibbous, 72 percent illuminated.",
  "Primary natal chart: Sun in Cancer; Moon in Virgo; Ascendant in Libra.",
  "Tight current-to-natal aspects: transiting Mars square natal Moon at 1.2 degrees.",
  "Ten-day transit arc: transiting Mars square natal Moon is closest in daily samples on day +2; outside the 4.5-degree window by day +7.",
  "The selected relationship between Daniel and Finn is parent and child.",
  "Tight major aspects: Daniel Sun trine Finn Moon at 1.8 degrees.",
  "No live relationship signal is available; lived experience must lead.",
  "Visible tarot card: The Star.",
  "Current sky positions: Venus is visible low in western sky after sunset.",
].join("\n");

function dailyFace(
  id: LunaSayDailyFaceId,
  values: Partial<LunaSayDailyFace>,
): LunaSayDailyFace {
  const metadata: Record<
    LunaSayDailyFaceId,
    Pick<LunaSayDailyFace, "mode" | "title" | "accent">
  > = {
    moon: { mode: "daily", title: "Moon", accent: "moon" },
    astrology: {
      mode: "daily",
      title: "Inner Weather",
      accent: "violet",
    },
    transits: { mode: "daily", title: "Transits", accent: "amber" },
    synastry: {
      mode: "daily",
      title: "Relationship Weather",
      accent: "rose",
    },
    tarot: { mode: "daily", title: "Tarot", accent: "amber" },
    alethiometer: {
      mode: "live_question",
      title: "Alethiometer",
      accent: "silver",
    },
    sky: { mode: "daily", title: "Sky", accent: "blue" },
    journal: { mode: "offline", title: "Journal", accent: "moon" },
    conversation: {
      mode: "live_question",
      title: "Companion",
      accent: "violet",
    },
  };
  return {
    ...metadata[id],
    headline: "",
    display: "",
    spoken: "",
    detail: "",
    ...values,
  };
}

function strongPacket(): LunaSayDailyPacket {
  const packet = lunaSayDailyPacketFallback({
    date: "2026-07-24",
    timezone: "America/Denver",
    facts: FACTS,
  });
  packet.faces.moon = dailyFace("moon", {
    headline: "Waxing light",
    display: "The Moon is 72 percent illuminated and still gaining light.",
    spoken:
      "The waxing Moon is seventy-two percent illuminated. Tonight, look for what has become visible before choosing what needs more time.",
    detail:
      "A waxing gibbous phase can be a useful prompt to compare present progress with the intention that began this cycle.",
    action:
      "Look for what has become visible before choosing what needs more time.",
    evidence: "Lunar phase estimate: waxing gibbous, 72 percent illuminated.",
  });
  packet.faces.astrology = dailyFace("astrology", {
    headline: "Protect the tender center",
    display:
      "Cancer warmth and Virgo discernment can resource each other today.",
    spoken:
      "Your inner weather may hold both care and exacting attention. Name the need first, then choose one useful detail to tend.",
    detail:
      "Cancer Sun emphasizes protection, while Virgo Moon can notice what needs adjustment; the tension is caring without making care into perfection.",
    action: "Name the need first, then choose one useful detail to tend.",
    evidence:
      "Primary natal chart: Sun in Cancer; Moon in Virgo; Ascendant in Libra.",
  });
  packet.faces.transits = dailyFace("transits", {
    headline: "Pressure seeks a clean outlet",
    display:
      "Mars square the natal Moon can make reactions feel closer to the surface.",
    spoken:
      "Current pressure may shorten your pause before reacting. Breathe once, name the boundary, and wait before answering if the body still feels charged.",
    detail:
      "Mars square the natal Moon describes friction between momentum and emotional safety; use it as a question about capacity, not a prediction.",
    action:
      "Breathe once, name the boundary, and wait before answering if the body still feels charged.",
    now: "The aspect is applying and may feel more immediate now.",
    next: "It is closest on day +2 and outside the window by day +7.",
    temporalEvidence:
      "Ten-day transit arc: transiting Mars square natal Moon is closest in daily samples on day +2; outside the 4.5-degree window by day +7.",
    evidence:
      "Tight current-to-natal aspects: transiting Mars square natal Moon at 1.2 degrees.",
  });
  packet.faces.synastry = dailyFace("synastry", {
    headline: "Warmth needs translation",
    display:
      "A Sun–Moon trine can support recognition without erasing different needs.",
    perspectiveA: "Daniel may lead with protective warmth.",
    perspectiveB: "Finn can answer through felt response.",
    spoken:
      "Pattern: Daniel may lead with protective warmth; Finn can answer through felt response. Today: Lived experience must lead. Next: Check again after one honest exchange. Practice: Ask what support would feel useful, then listen.",
    detail:
      "Daniel's Sun trine Finn's Moon suggests a relational resource for recognition; parent and child still need unequal responsibilities and room for different responses.",
    now:
      "No live signal is available, so this is a natal family pattern rather than a claim about today's mood.",
    next:
      "Let one real exchange update the reading before drawing a conclusion.",
    temporalEvidence:
      "No live relationship signal is available; lived experience must lead.",
    weatherEvidence:
      "No live relationship signal is available; lived experience must lead.",
    action: "Ask what support would feel useful, then listen.",
    evidence: "Tight major aspects: Daniel Sun trine Finn Moon at 1.8 degrees.",
  });
  packet.faces.tarot = dailyFace("tarot", {
    headline: "The Star",
    display: "Let hope become a question you can test.",
    spoken:
      "The Star offers a reflective image, not a forecast. Ask what small act would make hope more tangible today, then write the first honest answer.",
    detail:
      "Notice the card's open sky and poured water as symbols of renewal; consider where care can continue without demanding certainty.",
    action: "Write the first honest answer.",
    evidence: "Visible tarot card: The Star.",
    cardName: "The Star",
  });
  packet.faces.sky = dailyFace("sky", {
    headline: "Venus after sunset",
    display: "Venus is low in the west after sunset.",
    spoken:
      "After sunset, look low toward the western horizon for Venus. Notice how long it remains visible before the sky fully darkens.",
    detail:
      "This is an observable sky invitation: compare Venus with the fading light and let the actual horizon, weather, and visibility lead.",
    action: "Notice how long it remains visible before the sky fully darkens.",
    evidence:
      "Current sky positions: Venus is visible low in western sky after sunset.",
  });
  return packet;
}

Deno.test("strong, distinct, evidence-grounded packet passes both gates", () => {
  const report = scoreLunaSayReadingQuality(strongPacket(), FACTS);
  if (!report.hardGatePassed || !report.releaseGatePassed) {
    throw new Error(
      `strong packet failed:\n${JSON.stringify(report, null, 2)}`,
    );
  }
});

Deno.test("valid but generic and repetitive packet fails the user-value gate", () => {
  const packet = strongPacket();
  for (
    const id of [
      "moon",
      "astrology",
      "transits",
      "tarot",
      "sky",
    ] as const
  ) {
    packet.faces[id] = {
      ...packet.faces[id],
      headline: "Pause",
      display: "Pause and notice what changes.",
      spoken: `You may pause and notice what changes today. ${
        packet.faces[id].action
      }`,
      detail:
        "This symbolic reflection may invite you to pause and notice what changes today before making a choice.",
    };
  }
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  if (!report.hardGatePassed) {
    throw new Error("generic fixture should remain evidence-valid");
  }
  if (report.releaseGatePassed || report.maximumSimilarity <= 0.42) {
    throw new Error(
      `generic packet passed:\n${JSON.stringify(report, null, 2)}`,
    );
  }
});

Deno.test("unsupported evidence and deterministic language fail hard gate", () => {
  const packet = strongPacket();
  packet.faces.astrology.evidence =
    "Primary natal chart: Sun in Leo; Moon in Aries.";
  packet.faces.astrology.spoken =
    "This proves you are destined to make the correct choice.";
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  if (report.hardGatePassed || report.releaseGatePassed) {
    throw new Error(
      `unsafe packet passed:\n${JSON.stringify(report, null, 2)}`,
    );
  }
  const hardIssues = report.hardIssues.join("\n");
  if (
    !hardIssues.includes("evidence is not verbatim") ||
    !hardIssues.includes("deterministic language")
  ) {
    throw new Error(`expected hard issues missing:\n${hardIssues}`);
  }
});

Deno.test("Family Synastry cannot pass the hard gate with only reciprocal-sounding prose", () => {
  const packet = strongPacket();
  packet.faces.synastry = {
    ...packet.faces.synastry,
    perspectiveA: undefined,
    perspectiveB: undefined,
    spoken:
      "Pattern: Both people value care. Today: Lived experience must lead. Next: Wait for a real exchange. Practice: Ask what support would feel useful.",
  };
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  if (
    report.hardGatePassed ||
    !report.hardIssues.some((issue) =>
      issue.includes("missing two-sided relationship perspectives")
    )
  ) {
    throw new Error(
      `shallow reciprocal prose passed the hard gate: ${
        JSON.stringify(report.hardIssues)
      }`,
    );
  }
});

Deno.test("Family Synastry rejects paraphrase, blame, and child emotional labor", () => {
  const cases: Array<[string, Partial<LunaSayDailyFace>, string]> = [
    [
      "paraphrase",
      {
        perspectiveA: "Daniel may seek quiet before answering.",
        perspectiveB: "Finn can seek quiet before answering.",
      },
      "not meaningfully distinct",
    ],
    [
      "blame",
      { perspectiveB: "Finn can trigger Daniel's tension." },
      "makes one person the problem",
    ],
    [
      "child labor",
      { action: "Ask Finn to reassure Daniel, then listen." },
      "assigns emotional labor to a child",
    ],
  ];
  for (const [label, change, expectedIssue] of cases) {
    const packet = strongPacket();
    packet.faces.synastry = {
      ...packet.faces.synastry,
      ...change,
    };
    if (change.action) {
      packet.faces.synastry.spoken =
        `${packet.faces.synastry.spoken} ${change.action}`;
    }
    const report = scoreLunaSayReadingQuality(packet, FACTS);
    if (
      report.hardGatePassed ||
      !report.hardIssues.some((issue) => issue.includes(expectedIssue))
    ) {
      throw new Error(
        `${label} escaped the hard gate: ${JSON.stringify(report.hardIssues)}`,
      );
    }
  }
});

Deno.test("one thin face is caught by the per-face release floor", () => {
  const packet = strongPacket();
  packet.faces.tarot = {
    ...packet.faces.tarot,
    headline: "Reflection",
    display: "A reflection.",
    spoken: "A reflection.",
    detail: "A reflection.",
    action: undefined,
  };
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  const tarot = report.faces.find((face) => face.face === "tarot");
  if (!tarot || tarot.score >= 60 || report.releaseGatePassed) {
    throw new Error(
      `thin face escaped floor:\n${JSON.stringify(report, null, 2)}`,
    );
  }
});

Deno.test("natural spoken action verbs count as concrete guidance", () => {
  const packet = strongPacket();
  packet.faces.astrology.spoken =
    "Consider creating a comforting space before making the next choice.";
  packet.faces.transits.spoken =
    "Consider what actions support comfort, then pause before responding.";
  packet.faces.sky.spoken =
    "After sunset, gaze west and notice how long Venus remains visible.";
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  for (const id of ["astrology", "transits", "sky"] as const) {
    const face = report.faces.find((entry) => entry.face === id);
    if (!face || face.dimensions.actionability < 15) {
      throw new Error(`${id} natural action was not recognized`);
    }
  }
});

Deno.test("model fallback retains enough provenance to pass the hard gate", () => {
  const packet = lunaSayDailyPacketFallback({
    date: "2026-07-24",
    timezone: "America/Denver",
    facts: FACTS,
  });
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  if (!report.hardGatePassed) {
    throw new Error(
      `fallback lost provenance:\n${
        JSON.stringify(report.hardIssues, null, 2)
      }`,
    );
  }
});

Deno.test("a supplied card name establishes Tarot face identity", () => {
  const packet = strongPacket();
  packet.faces.tarot = {
    ...packet.faces.tarot,
    headline: "The Star",
    display: "The Star opens a quiet reflection.",
    spoken: "The Star may offer a different angle. Notice your first answer.",
    detail:
      "The Star can hold renewal and uncertainty together without becoming a forecast.",
  };
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  const tarot = report.faces.find((entry) => entry.face === "tarot");
  if (!tarot || tarot.dimensions.distinctness !== 15) {
    throw new Error("supplied card name did not establish Tarot identity");
  }
});

Deno.test("four short action-composed sentences remain TTS-coherent", () => {
  const packet = strongPacket();
  packet.faces.tarot.spoken =
    "The Star is a reflective image. Hope need not become certainty. Let the image open a question. Write the first honest answer.";
  const report = scoreLunaSayReadingQuality(packet, FACTS);
  const tarot = report.faces.find((entry) => entry.face === "tarot");
  if (!tarot || tarot.dimensions.speech !== 15) {
    throw new Error("bounded four-sentence action composition was penalized");
  }
});
