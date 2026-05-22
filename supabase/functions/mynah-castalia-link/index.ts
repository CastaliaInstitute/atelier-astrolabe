import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { corsHeaders, jsonResponse } from "../_shared/googleVoice.ts";

const PAIR_TTL_MS = 20 * 60_000;

function supabaseUrl(): string {
  return (Deno.env.get("SUPABASE_URL") ?? "").trim().replace(/\/+$/, "");
}

function serviceRoleKey(): string {
  return (Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? "").trim();
}

function anonKey(): string {
  return (Deno.env.get("SUPABASE_ANON_KEY") ?? "").trim();
}

async function sha256Hex(input: string): Promise<string> {
  const data = new TextEncoder().encode(input);
  const buf = await crypto.subtle.digest("SHA-256", data);
  const bytes = new Uint8Array(buf);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function randomSecret(): string {
  const bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  let bin = "";
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/u, "");
}

function restHeaders(service: boolean): Record<string, string> {
  const key = service ? serviceRoleKey() : anonKey();
  return {
    apikey: key,
    Authorization: `Bearer ${key}`,
    "Content-Type": "application/json",
  };
}

async function restDeleteOldPending(): Promise<void> {
  const base = supabaseUrl();
  const key = serviceRoleKey();
  if (!base || !key) return;
  const cutoff = new Date(Date.now() - PAIR_TTL_MS).toISOString();
  const url =
    `${base}/rest/v1/mynah_device_pairings?status=eq.pending&created_at=lt.${encodeURIComponent(cutoff)}`;
  await fetch(url, { method: "DELETE", headers: restHeaders(true) });
}

async function insertPairing(id: string, secretHash: string): Promise<boolean> {
  const base = supabaseUrl();
  const res = await fetch(`${base}/rest/v1/mynah_device_pairings`, {
    method: "POST",
    headers: { ...restHeaders(true), Prefer: "return=minimal" },
    body: JSON.stringify({
      id,
      secret_hash: secretHash,
      status: "pending",
    }),
  });
  return res.ok;
}

type PairRow = {
  id: string;
  secret_hash: string;
  access_token: string | null;
  refresh_token: string | null;
  expires_at_ms: number | null;
  status: string;
};

async function fetchPairRow(id: string): Promise<PairRow | null> {
  const base = supabaseUrl();
  const res = await fetch(
    `${base}/rest/v1/mynah_device_pairings?id=eq.${id}&select=id,secret_hash,access_token,refresh_token,expires_at_ms,status`,
    { headers: restHeaders(true) },
  );
  if (!res.ok) return null;
  const rows = (await res.json()) as PairRow[];
  return rows[0] ?? null;
}

async function patchPair(
  id: string,
  body: Record<string, unknown>,
  extraFilter?: string,
): Promise<boolean> {
  const base = supabaseUrl();
  const q = extraFilter ? `id=eq.${id}&${extraFilter}` : `id=eq.${id}`;
  const res = await fetch(`${base}/rest/v1/mynah_device_pairings?${q}`, {
    method: "PATCH",
    headers: { ...restHeaders(true), Prefer: "return=minimal" },
    body: JSON.stringify(body),
  });
  return res.ok;
}

function functionPathSuffix(url: URL): string {
  const parts = url.pathname.split("/").filter(Boolean);
  const i = parts.indexOf("mynah-castalia-link");
  if (i < 0) return "";
  return parts.slice(i + 1).join("/");
}

/** Supabase Edge rewrites text/html → text/plain; browsers show source. Redirect to castalia.institute. */
function castaliaWebOrigin(): string {
  return (Deno.env.get("CASTALIA_WEB_ORIGIN") ?? "https://castalia.institute").trim().replace(/\/+$/, "");
}

function redirectToCastaliaSignin(pair: string, key: string): Response {
  const origin = castaliaWebOrigin();
  const back = `/auth/mynah-device/?pair=${encodeURIComponent(pair)}&key=${encodeURIComponent(key)}`;
  const signin =
    `${origin}/auth/signin/?provider=google&redirect=${encodeURIComponent(back)}`;
  const headers = new Headers(corsHeaders);
  headers.set("Location", signin);
  return new Response(null, { status: 302, headers });
}

function loginPageHtml(req: Request): Response {
  const u = new URL(req.url);
  const pair = (u.searchParams.get("pair") ?? "").trim();
  const key = (u.searchParams.get("key") ?? "").trim();
  if (!pair || !key) {
    return jsonResponse(400, { error: "pair and key required" });
  }
  return redirectToCastaliaSignin(pair, key);
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  const url = new URL(req.url);
  const suffix = functionPathSuffix(url);
  const base = supabaseUrl();
  const srk = serviceRoleKey();

  if (!base || !srk) {
    return jsonResponse(500, { error: "Missing SUPABASE_URL or SUPABASE_SERVICE_ROLE_KEY" });
  }

  if (req.method === "POST" && suffix === "start") {
    await restDeleteOldPending();
    const id = crypto.randomUUID();
    const secret = randomSecret();
    const hash = await sha256Hex(secret);
    const ok = await insertPairing(id, hash);
    if (!ok) {
      return jsonResponse(500, { error: "Could not create pairing row (run SQL migration?)" });
    }
    return jsonResponse(200, { pair_id: id, pair_secret: secret });
  }

  if (req.method === "GET" && suffix === "poll") {
    const pairId = (url.searchParams.get("pair_id") ?? "").trim();
    const secret = (url.searchParams.get("pair_secret") ?? "").trim();
    if (!pairId || !secret) {
      return jsonResponse(400, { error: "pair_id and pair_secret required" });
    }
    const row = await fetchPairRow(pairId);
    if (!row) return jsonResponse(404, { status: "unknown" });
    const h = await sha256Hex(secret);
    if (h !== row.secret_hash) return jsonResponse(403, { error: "bad_secret" });
    if (row.status === "consumed") {
      return jsonResponse(200, { status: "consumed" });
    }
    if (row.status !== "ready" || !row.access_token || !row.refresh_token) {
      return jsonResponse(200, { status: "pending" });
    }
    const ok = await patchPair(pairId, {
      status: "consumed",
      access_token: null,
      refresh_token: null,
      expires_at_ms: null,
    }, "status=eq.ready");
    if (!ok) return jsonResponse(409, { error: "race" });
    return jsonResponse(200, {
      status: "ready",
      access_token: row.access_token,
      refresh_token: row.refresh_token,
      expires_in: Math.max(
        60,
        Math.floor(((row.expires_at_ms ?? Date.now() + 3600_000) - Date.now()) / 1000),
      ),
    });
  }

  if (req.method === "POST" && suffix === "complete") {
    let body: {
      pair_id?: string;
      pair_secret?: string;
      access_token?: string;
      refresh_token?: string;
      expires_in?: number;
    };
    try {
      body = (await req.json()) as typeof body;
    } catch {
      return jsonResponse(400, { error: "Invalid JSON" });
    }
    const pairId = (body.pair_id ?? "").trim();
    const secret = (body.pair_secret ?? "").trim();
    const access = (body.access_token ?? "").trim();
    const refresh = (body.refresh_token ?? "").trim();
    const expIn = Number(body.expires_in ?? 3600) || 3600;
    if (!pairId || !secret || !access || !refresh) {
      return jsonResponse(400, { error: "missing_fields" });
    }
    const row = await fetchPairRow(pairId);
    if (!row) return jsonResponse(404, { error: "unknown_pair" });
    const h = await sha256Hex(secret);
    if (h !== row.secret_hash) return jsonResponse(403, { error: "bad_secret" });
    if (row.status !== "pending") {
      return jsonResponse(200, { ok: true, note: "already_completed" });
    }
    const expMs = Date.now() + expIn * 1000;
    const ok = await patchPair(
      pairId,
      {
        access_token: access,
        refresh_token: refresh,
        expires_at_ms: expMs,
        status: "ready",
      },
      "status=eq.pending",
    );
    if (!ok) return jsonResponse(409, { error: "race" });
    return jsonResponse(200, { ok: true });
  }

  if (req.method === "GET" && suffix === "callback") {
    const u = new URL(req.url);
    const err = (u.searchParams.get("error") ?? "").trim();
    if (err) {
      return jsonResponse(400, { error: err, error_description: u.searchParams.get("error_description") ?? "" });
    }
    return jsonResponse(400, {
      error: "oauth_callback_on_edge",
      hint: "Use castalia.institute sign-in; watch QR should open /auth/signin/ not this URL.",
    });
  }

  if (req.method === "GET" && (suffix === "" || suffix === "/")) {
    return loginPageHtml(req);
  }

  return jsonResponse(404, { error: "not_found" });
});
