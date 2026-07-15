import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { createClient } from "npm:@supabase/supabase-js@2.49.8";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, OPTIONS",
};

type DeviceLookupRow = {
  short_id: string;
  channel: string;
  kind: string;
  label: string | null;
  enabled: boolean;
};

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json",
    },
  });
}

function normalizeShortId(value: string | null): string {
  const id = (value ?? "").trim().toLowerCase();
  return /^[0-9a-f]{4}$/.test(id) ? id : "";
}

function normalizeChannel(value: string | null): string {
  return (value ?? "astrolabe-faculty-amoled175").trim();
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "GET") {
    return jsonResponse({ error: "Method not allowed" }, 405);
  }

  const url = new URL(req.url);
  const shortId = normalizeShortId(
    url.searchParams.get("shortId") ?? url.searchParams.get("id"),
  );
  const channel = normalizeChannel(url.searchParams.get("channel"));
  if (!shortId || !channel) {
    return jsonResponse({ error: "shortId and channel are required" }, 400);
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const serviceRole = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!supabaseUrl || !serviceRole) {
    return jsonResponse({ error: "Device lookup is not configured" }, 500);
  }

  const admin = createClient(supabaseUrl, serviceRole, {
    auth: { autoRefreshToken: false, persistSession: false },
  });
  const { data, error } = await admin
    .from("astrolabe_devices")
    .select("short_id,channel,kind,label,enabled")
    .eq("short_id", shortId)
    .eq("channel", channel)
    .eq("enabled", true)
    .maybeSingle<DeviceLookupRow>();

  if (error) {
    console.error("device lookup failed", error);
    return jsonResponse({ error: "Lookup failed" }, 500);
  }
  if (!data) {
    return jsonResponse({ found: false, shortId, channel }, 404);
  }

  return jsonResponse({
    found: true,
    shortId: data.short_id,
    channel: data.channel,
    kind: data.kind,
    label: data.label ?? null,
  });
});
