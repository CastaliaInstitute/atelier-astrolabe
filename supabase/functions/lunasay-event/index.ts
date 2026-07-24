import { appendLunaSayMlEvent } from "../_shared/lunasayGithubLog.ts";

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, apikey, content-type, x-client-info",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

const MOODS = new Set([
  "calm",
  "bright",
  "tender",
  "low",
  "tense",
  "energized",
]);

function json(body: Record<string, unknown>, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...CORS, "Content-Type": "application/json" },
  });
}

Deno.serve(async (request) => {
  if (request.method === "OPTIONS") {
    return new Response(null, { headers: CORS });
  }
  if (request.method !== "POST") return json({ error: "POST required" }, 405);

  let body: Record<string, unknown>;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid JSON" }, 400);
  }
  const mood = typeof body.mood === "string"
    ? body.mood.trim().toLowerCase()
    : "";
  const subjectId = typeof body.subjectId === "string"
    ? body.subjectId.trim()
    : "";
  const consentVersion = typeof body.consentVersion === "string"
    ? body.consentVersion.trim()
    : "";
  const arousal = Number(body.arousal);
  const valence = Number(body.valence);
  if (
    body.consent !== true || !MOODS.has(mood) || !subjectId ||
    !consentVersion || !Number.isFinite(arousal) ||
    !Number.isFinite(valence) || arousal < 0 || arousal > 100 ||
    valence < 0 || valence > 100
  ) {
    return json({ error: "Invalid or unconsented mood event" }, 422);
  }

  const logged = await appendLunaSayMlEvent({
    event: "mood_checkin",
    subjectId,
    consentVersion,
    data: {
      mood,
      arousal: Math.round(arousal),
      valence: Math.round(valence),
      source: typeof body.source === "string"
        ? body.source.slice(0, 32)
        : "unknown",
    },
  });
  return json({ accepted: true, logged });
});
