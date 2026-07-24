import { createLunaSayMlRecord } from "./lunasayGithubLog.ts";

Deno.test("LunaSay ML record pseudonymizes subject and bounds fields", async () => {
  const record = await createLunaSayMlRecord({
    event: "mood_checkin",
    subjectId: "user-private-id",
    consentVersion: "research-v1",
    occurredAt: "2026-08-01T12:00:00Z",
    data: {
      mood: "tender",
      arousal: 35,
      valence: 45,
      "unsafe key": "drop me",
      note: "x".repeat(200),
    },
  }, "server-only-salt");

  const encoded = JSON.stringify(record);
  if (
    record.subjectHash.length !== 64 ||
    encoded.includes("user-private-id") ||
    "unsafe key" in record.data ||
    String(record.data.note).length !== 96
  ) {
    throw new Error("ML event privacy boundary failed");
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
