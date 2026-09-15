import { createLunaSayMlRecord } from "./lunasayGithubLog.ts";

Deno.test("LunaSay ML record pseudonymizes subject and bounds fields", async () => {
  const record = await createLunaSayMlRecord({
    event: "mood_checkin",
    subjectId: "user-private-id",
    consentVersion: "research-v2",
    occurredAt: "2026-08-01T12:00:00Z",
    data: {
      mood: "tender",
      arousal: 35,
      valence: 45,
      "unsafe key": "drop me",
      note: "this must never cross the research boundary",
    },
  }, "server-only-salt");

  const encoded = JSON.stringify(record);
  if (
    record.subjectHash.length !== 64 ||
    encoded.includes("user-private-id") ||
    "unsafe key" in record.data ||
    "note" in record.data
  ) {
    throw new Error("ML event privacy boundary failed");
  }
});

Deno.test("reading feedback exports only non-textual bounded labels", async () => {
  const record = await createLunaSayMlRecord({
    event: "reading_feedback",
    subjectId: "private-device-id",
    consentVersion: "research-v2",
    data: {
      face: "synastry",
      rating: "mixed",
      reading_date: "2026-08-01",
      source: "pwa",
      note: "A private family detail that must not be exported",
      transcript: "also private",
    },
  }, "server-only-salt");
  if (
    record.data.face !== "synastry" || record.data.rating !== "mixed" ||
    "note" in record.data || "transcript" in record.data ||
    JSON.stringify(record).includes("family detail")
  ) {
    throw new Error("reading feedback allowlist leaked free text");
  }
});

Deno.test("LunaSay ML record requires a consent version", async () => {
  let rejected = false;
  try {
    await createLunaSayMlRecord({
      event: "mood_checkin",
      subjectId: "user-private-id",
      consentVersion: "",
      data: { mood: "calm" },
    }, "server-only-salt");
  } catch {
    rejected = true;
  }
  if (!rejected) throw new Error("unconsented event was accepted");
});
