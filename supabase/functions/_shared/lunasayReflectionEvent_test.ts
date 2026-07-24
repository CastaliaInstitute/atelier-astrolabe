import { parseLunaSayReflectionEvent } from "./lunasayReflectionEvent.ts";

const NOW = Date.parse("2026-08-01T18:00:00Z");
const valid = {
  event: "reading_feedback",
  consent: true,
  consentVersion: "research-v2",
  face: "synastry",
  rating: "helpful",
  readingDate: "2026-08-01",
  source: "pwa",
  occurredAt: "2026-08-01T17:58:00Z",
};

Deno.test("parses one bounded consented reading reflection", () => {
  const event = parseLunaSayReflectionEvent(valid, NOW);
  if (
    !event || event.face !== "synastry" || event.rating !== "helpful" ||
    event.readingDate !== "2026-08-01"
  ) {
    throw new Error("valid reading reflection was not normalized");
  }
});

Deno.test("rejects stale consent and unsupported faces or ratings", () => {
  for (
    const candidate of [
      { ...valid, consent: false },
      { ...valid, consentVersion: "research-v1" },
      { ...valid, face: "conversation" },
      { ...valid, rating: "accurate" },
      { ...valid, event: "reading_opened" },
    ]
  ) {
    if (parseLunaSayReflectionEvent(candidate, NOW)) {
      throw new Error(
        `invalid reflection accepted: ${JSON.stringify(candidate)}`,
      );
    }
  }
});

Deno.test("rejects distant, malformed, or future reading dates", () => {
  for (
    const readingDate of [
      "2026-06-01",
      "2026-08-03",
      "2026-02-31",
      "08/01/2026",
      "not-a-date",
    ]
  ) {
    if (parseLunaSayReflectionEvent({ ...valid, readingDate }, NOW)) {
      throw new Error(`invalid reading date accepted: ${readingDate}`);
    }
  }
});
