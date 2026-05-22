import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import {
  loadCalciferEvents,
  pickCurrentAndNext,
  readCalciferEnv,
} from "../_shared/calciferClockBrief.ts";
import { corsHeaders, jsonResponse } from "../_shared/googleVoice.ts";

function unixSec(d: Date): number {
  return Math.floor(d.getTime() / 1000);
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "POST" && req.method !== "GET") {
    return jsonResponse(405, { ok: false, error: "method_not_allowed" });
  }

  let epochSeconds = Math.floor(Date.now() / 1000);
  if (req.method === "POST") {
    try {
      const j = await req.json().catch(() => ({})) as Record<string, unknown>;
      const e = j["epochSeconds"];
      if (typeof e === "number" && Number.isFinite(e)) {
        epochSeconds = Math.floor(e);
      }
    } catch {
      // keep default now
    }
  } else {
    const u = new URL(req.url);
    const q = u.searchParams.get("epoch");
    if (q) {
      const n = parseInt(q, 10);
      if (Number.isFinite(n)) epochSeconds = n;
    }
  }

  const env = readCalciferEnv();
  if (!env.calendarUrl || !env.user || !env.password) {
    return jsonResponse(200, {
      ok: true,
      configured: false,
      displayTz: env.displayTz,
      current: null,
      next: null,
    });
  }

  try {
    const events = await loadCalciferEvents({
      calendarUrl: env.calendarUrl,
      user: env.user,
      password: env.password,
      epochSeconds,
    });
    const now = new Date(epochSeconds * 1000);
    const { current, next } = pickCurrentAndNext(events, now);

    const pack = (e: typeof current) =>
      e
        ? {
          summary: e.summary,
          startUnix: unixSec(e.start),
          endUnix: unixSec(e.end),
        }
        : null;

    return jsonResponse(200, {
      ok: true,
      configured: true,
      displayTz: env.displayTz,
      current: pack(current),
      next: pack(next),
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    return jsonResponse(200, {
      ok: false,
      configured: true,
      error: msg.slice(0, 200),
      displayTz: env.displayTz,
      current: null,
      next: null,
    });
  }
});
