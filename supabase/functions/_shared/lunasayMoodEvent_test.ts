import { parseLunaSayMoodEvent } from "./lunasayMoodEvent.ts";

const NOW = Date.parse("2026-07-24T05:00:00Z");
const valid = {
  consent: true,
  consentVersion: "research-v1",
  mood: "Tender",
  arousal: 35.2,
  valence: 44.8,
  source: "device-face",
  occurredAt: "2026-07-24T04:59:00Z",
};

Deno.test("parses one bounded, consented LunaSay mood event", () => {
  const event = parseLunaSayMoodEvent(valid, NOW);
  if (
    !event || event.mood !== "tender" || event.arousal !== 35 ||
    event.valence !== 45 ||
    event.occurredAt !== "2026-07-24T04:59:00.000Z"
  ) {
    throw new Error("valid mood event was not normalized");
  }
});

Deno.test("rejects missing consent and stale consent versions", () => {
  if (
    parseLunaSayMoodEvent({ ...valid, consent: false }, NOW) ||
    parseLunaSayMoodEvent({ ...valid, consentVersion: "research-v0" }, NOW)
  ) {
    throw new Error("invalid consent envelope was accepted");
  }
});

Deno.test("rejects unknown moods, sources, and out-of-range scores", () => {
  if (
    parseLunaSayMoodEvent({ ...valid, mood: "ecstatic" }, NOW) ||
    parseLunaSayMoodEvent({ ...valid, source: "journal" }, NOW) ||
    parseLunaSayMoodEvent({ ...valid, arousal: 101 }, NOW) ||
    parseLunaSayMoodEvent({ ...valid, valence: -1 }, NOW)
  ) {
    throw new Error("unbounded research data was accepted");
  }
});

Deno.test("rejects invalid and implausibly future timestamps", () => {
  if (
    parseLunaSayMoodEvent({ ...valid, occurredAt: "not-a-date" }, NOW) ||
    parseLunaSayMoodEvent(
      { ...valid, occurredAt: "2026-07-26T05:00:00Z" },
      NOW,
    )
  ) {
    throw new Error("invalid research timestamp was accepted");
  }
});
