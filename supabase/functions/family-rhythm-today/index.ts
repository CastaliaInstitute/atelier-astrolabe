import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { corsHeaders, jsonResponse } from "../_shared/googleVoice.ts";

type SupabaseUser = {
  id?: string;
  email?: string;
};

type RhythmPerson = {
  key?: string;
  name?: string;
  pulse?: string;
  best_window?: string;
  cue?: string;
};

type RhythmWindow = {
  label?: string;
  text?: string;
};

type RhythmArtifact = {
  date?: string;
  generated_at?: string;
  household?: {
    headline?: string;
    summary?: string;
    parent_practice?: string;
    back_rhythm?: RhythmWindow[];
  };
  people?: RhythmPerson[];
};

function trimSlash(s: string): string {
  return s.replace(/\/+$/, "");
}

function env(name: string): string {
  return Deno.env.get(name)?.trim() ?? "";
}

function todayIso(): string {
  return new Date().toISOString().slice(0, 10);
}

function cleanDate(value: string | null): string {
  const raw = (value ?? "").trim();
  return /^\d{4}-\d{2}-\d{2}$/.test(raw) ? raw : todayIso();
}

function bearer(req: Request): string {
  const auth = req.headers.get("Authorization") ?? "";
  const m = auth.match(/^Bearer\s+(.+)$/i);
  return m?.[1]?.trim() ?? "";
}

async function userFromBearer(token: string): Promise<SupabaseUser | null> {
  const url = trimSlash(env("SUPABASE_URL"));
  const anon = env("SUPABASE_ANON_KEY");
  if (!url || !anon) {
    throw new Error("SUPABASE_URL or SUPABASE_ANON_KEY missing");
  }

  const res = await fetch(`${url}/auth/v1/user`, {
    headers: {
      apikey: anon,
      Authorization: `Bearer ${token}`,
    },
  });
  if (res.status === 401 || res.status === 403) {
    return null;
  }
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`Supabase auth failed: ${res.status} ${text}`);
  }
  return JSON.parse(text) as SupabaseUser;
}

function loadRepoMap(): Record<string, unknown> {
  const raw = env("FAMILY_RHYTHM_REPO_MAP_JSON");
  if (!raw) return {};
  const parsed = JSON.parse(raw) as unknown;
  return parsed && typeof parsed === "object" && !Array.isArray(parsed)
    ? parsed as Record<string, unknown>
    : {};
}

function stringMap(value: unknown): Record<string, string> {
  if (!value || typeof value !== "object" || Array.isArray(value)) return {};
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(value as Record<string, unknown>)) {
    if (typeof v === "string" && v.trim()) out[k.toLowerCase()] = v.trim();
  }
  return out;
}

function repoForUser(user: SupabaseUser): string {
  const mapping = loadRepoMap();
  const byUserId = stringMap(mapping.user_ids);
  const byEmail = stringMap(mapping.emails);

  const userId = (user.id ?? "").trim().toLowerCase();
  const email = (user.email ?? "").trim().toLowerCase();
  const repo = (userId && byUserId[userId]) || (email && byEmail[email]) || "";
  if (!repo) return "";

  if (repo.includes("/")) return repo;
  const org = env("GITHUB_ORG") || "CastaliaInstitute";
  return `${org}/${repo}`;
}

async function githubRaw(ownerRepo: string, path: string): Promise<Uint8Array | null> {
  const token = env("GITHUB_TOKEN") || env("FAMILY_RHYTHM_GITHUB_TOKEN");
  if (!token) {
    throw new Error("GITHUB_TOKEN or FAMILY_RHYTHM_GITHUB_TOKEN missing");
  }
  const ref = encodeURIComponent(env("FAMILY_RHYTHM_GITHUB_REF") || "main");
  const encodedPath = path.split("/").map((part) => encodeURIComponent(part)).join("/");
  const url = `https://api.github.com/repos/${ownerRepo}/contents/${encodedPath}?ref=${ref}`;
  const res = await fetch(url, {
    headers: {
      Authorization: `Bearer ${token}`,
      Accept: "application/vnd.github.raw",
      "User-Agent": "mynah-family-rhythm",
      "X-GitHub-Api-Version": "2022-11-28",
    },
  });
  if (res.status === 404) return null;
  const body = new Uint8Array(await res.arrayBuffer());
  if (!res.ok) {
    const text = new TextDecoder().decode(body.slice(0, 800));
    throw new Error(`GitHub fetch failed: ${res.status} ${text}`);
  }
  const maxBytes = Number.parseInt(env("FAMILY_RHYTHM_MAX_BYTES") || "131072", 10);
  if (Number.isFinite(maxBytes) && body.byteLength > maxBytes) {
    throw new Error(`Rhythm artifact too large: ${body.byteLength}`);
  }
  return body;
}

function compact(artifact: RhythmArtifact) {
  const household = artifact.household ?? {};
  const windows = Array.isArray(household.back_rhythm) ? household.back_rhythm : [];
  const people = Array.isArray(artifact.people) ? artifact.people : [];
  return {
    ok: true,
    configured: true,
    date: artifact.date ?? "",
    household: {
      headline: household.headline ?? "",
      summary: household.summary ?? "",
      parentPractice: household.parent_practice ?? "",
    },
    windows: windows.slice(0, 4).map((row) => ({
      label: row.label ?? "",
      text: row.text ?? "",
    })),
    people: people.slice(0, 8).map((person) => ({
      key: person.key ?? "",
      name: person.name ?? "",
      pulse: person.pulse ?? "",
      bestWindow: person.best_window ?? "",
      cue: person.cue ?? "",
    })),
    updatedAt: artifact.generated_at ?? "",
  };
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "GET" && req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  try {
    let requestedDate = new URL(req.url).searchParams.get("date");
    if (req.method === "POST") {
      const body = await req.json().catch(() => ({})) as { date?: string };
      requestedDate = body.date ?? requestedDate;
    }
    const date = cleanDate(requestedDate);

    const token = bearer(req);
    if (!token) {
      return jsonResponse(401, { ok: false, configured: false, error: "missing_bearer" });
    }

    const user = await userFromBearer(token);
    if (!user?.id) {
      return jsonResponse(401, { ok: false, configured: false, error: "invalid_bearer" });
    }

    const repo = repoForUser(user);
    if (!repo) {
      return jsonResponse(200, { ok: true, configured: false, date });
    }

    const raw = await githubRaw(repo, `outputs/${date}/synastry-rhythms.json`);
    if (!raw) {
      return jsonResponse(200, { ok: true, configured: false, date });
    }

    const artifact = JSON.parse(new TextDecoder().decode(raw)) as RhythmArtifact;
    return jsonResponse(200, compact(artifact), {
      "Cache-Control": "private, max-age=300",
      "X-Mynah-Route": "family-rhythm-today",
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    console.error("family-rhythm-today error:", msg);
    return jsonResponse(500, { ok: false, configured: false, error: "server_error" });
  }
});
