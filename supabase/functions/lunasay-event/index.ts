import { appendLunaSayMlEvent } from "../_shared/lunasayGithubLog.ts";
import { verifiedAstrolabeDeviceIdentity } from "../_shared/deviceAuth.ts";
import { parseLunaSayMoodEvent } from "../_shared/lunasayMoodEvent.ts";

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, apikey, content-type, x-client-info",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

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
  const contentLength = Number(request.headers.get("content-length") ?? "0");
  if (Number.isFinite(contentLength) && contentLength > 2048) {
    return json({ error: "Request too large" }, 413);
  }

  let body: Record<string, unknown>;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid JSON" }, 400);
  }
  const event = parseLunaSayMoodEvent(body);
  if (!event) {
    return json({ error: "Invalid or unconsented mood event" }, 422);
  }
  const device = await verifiedAstrolabeDeviceIdentity(request);
  if (device instanceof Response) return device;

  const logged = await appendLunaSayMlEvent({
    event: "mood_checkin",
    subjectId: device.ownerUserId
      ? `user:${device.ownerUserId}`
      : `device:${device.mac}`,
    consentVersion: event.consentVersion,
    occurredAt: event.occurredAt,
    data: {
      mood: event.mood,
      arousal: event.arousal,
      valence: event.valence,
      source: event.source,
    },
  });
  return json({ accepted: true, logged });
});
