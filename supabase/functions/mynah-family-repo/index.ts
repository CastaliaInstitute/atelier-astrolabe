import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { verifyAstrolabeDevice } from "../_shared/deviceAuth.ts";

const ALLOWED_REPO = "CastaliaInstitute/castalia-family-mcshan";
const ALLOWED_PATHS = new Set([
  "castalia-family.json",
  "settings/family.json",
  "settings/charts.json",
  "family.json",
  "charts.json",
  "settings/birth.json",
]);

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, apikey, content-type, x-astrolabe-device-mac, x-astrolabe-device-nonce, x-astrolabe-device-channel, x-astrolabe-device-signature",
  "Access-Control-Allow-Methods": "GET, OPTIONS",
};

function json(body: unknown, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json" },
  });
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "GET") return json({ error: "method_not_allowed" }, 405);

  const authError = await verifyAstrolabeDevice(req, true);
  if (authError) return authError;

  const url = new URL(req.url);
  const repo = (url.searchParams.get("repo") ?? "").trim();
  const path = (url.searchParams.get("path") ?? "").trim();
  if (repo !== ALLOWED_REPO || !ALLOWED_PATHS.has(path)) {
    return json({ error: "not_found" }, 404);
  }

  const token = (Deno.env.get("FAMILY_RHYTHM_GITHUB_TOKEN") ??
    Deno.env.get("GITHUB_TOKEN") ?? "").trim();
  if (!token) return json({ error: "family_repo_not_configured" }, 500);

  const encodedPath = path.split("/").map(encodeURIComponent).join("/");
  const githubUrl =
    `https://api.github.com/repos/${repo}/contents/${encodedPath}?ref=main`;
  const upstream = await fetch(githubUrl, {
    headers: {
      Authorization: `Bearer ${token}`,
      Accept: "application/vnd.github.raw",
      "User-Agent": "Castalia-LunaSay-Family/1.0",
      "X-GitHub-Api-Version": "2022-11-28",
    },
  });
  if (upstream.status === 404) return json({ error: "not_found" }, 404);
  if (!upstream.ok) {
    console.error("family repo fetch failed", upstream.status);
    return json({ error: "upstream_failed" }, 502);
  }

  const body = await upstream.arrayBuffer();
  if (body.byteLength > 64 * 1024) {
    return json({ error: "artifact_too_large" }, 413);
  }
  return new Response(body, {
    status: 200,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "private, max-age=300",
      "X-Castalia-Source": `${repo}/${path}`,
    },
  });
});
