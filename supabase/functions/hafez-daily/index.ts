import { createClient } from "https://esm.sh/@supabase/supabase-js@2.45.4";
import { corsHeaders, jsonResponse } from "../_shared/cors.ts";

type HafezRow = {
  quote_text: string;
  source: string | null;
  art_prompt: string | null;
  art_seed: number | string | null;
  palette: string | null;
};

function parseEpochSeconds(value: unknown): number {
  if (typeof value === "number" && Number.isFinite(value) && value > 0) {
    return Math.floor(value);
  }
  if (typeof value === "string") {
    const parsed = Number(value);
    if (Number.isFinite(parsed) && parsed > 0) {
      return Math.floor(parsed);
    }
  }
  return Math.floor(Date.now() / 1000);
}

function fnv1a32(input: string): number {
  let hash = 0x811c9dc5;
  for (let i = 0; i < input.length; i++) {
    hash ^= input.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

function defaultPalette(seed: number): string {
  const options = [
    "midnight-indigo",
    "rose-gold",
    "saffron-cobalt",
    "plum-amber",
    "seafoam-obsidian",
  ];
  return options[seed % options.length];
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "POST") {
    return jsonResponse({ ok: false, configured: false, error: "method not allowed" }, 405);
  }

  const payload = await req.json().catch(() => ({}));
  const epochSeconds = parseEpochSeconds(payload?.epochSeconds);
  const day = new Date(epochSeconds * 1000).toISOString().slice(0, 10);
  const dayKey = Number(day.replaceAll("-", ""));

  const supabaseUrl = Deno.env.get("SUPABASE_URL") ?? "";
  const serviceRoleKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? "";
  if (!supabaseUrl || !serviceRoleKey) {
    return jsonResponse(
      {
        ok: false,
        configured: false,
        dayKey,
        error: "missing SUPABASE_URL or SUPABASE_SERVICE_ROLE_KEY",
      },
      500,
    );
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey, {
    auth: { persistSession: false, autoRefreshToken: false },
  });

  const { data, error } = await supabase
    .rpc("pick_hafez_quote", { p_day: day, p_locale: "en" })
    .maybeSingle<HafezRow>();

  if (error) {
    return jsonResponse(
      {
        ok: false,
        configured: true,
        dayKey,
        error: `pick_hafez_quote failed: ${error.message}`,
      },
      500,
    );
  }

  if (!data?.quote_text) {
    return jsonResponse({
      ok: false,
      configured: false,
      dayKey,
      error: "no hafez quotes configured",
    });
  }

  const quote = data.quote_text.trim();
  const source = (data.source ?? "Hafez").trim() || "Hafez";
  const artPrompt = (data.art_prompt ?? "").trim();
  const parsedSeed = Number(data.art_seed ?? "");
  const artSeed = Number.isFinite(parsedSeed) && parsedSeed > 0
    ? Math.floor(parsedSeed) >>> 0
    : fnv1a32(`${day}|${quote}`);
  const palette = (data.palette ?? "").trim() || defaultPalette(artSeed);

  return jsonResponse({
    ok: true,
    configured: true,
    dayKey,
    quote,
    source,
    artPrompt,
    artSeed,
    palette,
  });
});
