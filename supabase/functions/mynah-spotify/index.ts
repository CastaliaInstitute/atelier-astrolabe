import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { corsHeaders, jsonResponse } from "../_shared/googleVoice.ts";

type Action =
  | "status"
  | "toggle"
  | "next"
  | "previous"
  | "play"
  | "pause"
  /** Same as pause — stops playback on the active Connect device. */
  | "stop";

type ReqBody = {
  action?: Action;
};

type TokenCache = {
  access_token: string;
  expires_at_ms: number;
};

let tokenCache: TokenCache | null = null;

function spotifyEnv(): {
  clientId: string;
  clientSecret: string;
  refreshToken: string;
} {
  const clientId = Deno.env.get("SPOTIFY_CLIENT_ID")?.trim() ?? "";
  const clientSecret = Deno.env.get("SPOTIFY_CLIENT_SECRET")?.trim() ?? "";
  const refreshToken = Deno.env.get("SPOTIFY_REFRESH_TOKEN")?.trim() ?? "";
  return { clientId, clientSecret, refreshToken };
}

async function getAccessToken(): Promise<string> {
  const { clientId, clientSecret, refreshToken } = spotifyEnv();
  if (!clientId || !clientSecret || !refreshToken) {
    throw new Error("Spotify env missing: SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN");
  }
  const now = Date.now();
  if (tokenCache && now < tokenCache.expires_at_ms - 30_000) {
    return tokenCache.access_token;
  }

  const basic = btoa(`${clientId}:${clientSecret}`);
  const body = new URLSearchParams({
    grant_type: "refresh_token",
    refresh_token: refreshToken,
  });

  const res = await fetch("https://accounts.spotify.com/api/token", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      Authorization: `Basic ${basic}`,
    },
    body,
  });
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`Spotify token ${res.status}: ${text.slice(0, 200)}`);
  }
  const data = JSON.parse(text) as {
    access_token: string;
    expires_in: number;
    refresh_token?: string;
  };
  tokenCache = {
    access_token: data.access_token,
    expires_at_ms: now + (data.expires_in ?? 3600) * 1000,
  };
  return data.access_token;
}

async function spotifyApi(
  method: string,
  path: string,
  opts?: { body?: string },
): Promise<Response> {
  const token = await getAccessToken();
  return await fetch(`https://api.spotify.com/v1${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${token}`,
      ...(opts?.body ? { "Content-Type": "application/json" } : {}),
    },
    body: opts?.body,
  });
}

function decodeJsonString(s: string): string {
  return s
    .replace(/\\\\/g, "\u0000")
    .replace(/\\"/g, '"')
    .replace(/\\n/g, "\n")
    .replace(/\u0000/g, "\\");
}

function parseDeviceName(json: string): string {
  const di = json.indexOf('"device"');
  if (di < 0) {
    return "";
  }
  const slice = json.slice(di, di + 2800);
  if (slice.includes('"device":null') || slice.includes('"device" : null')) {
    return "";
  }
  const nm = slice.match(/"name"\s*:\s*"((?:[^"\\]|\\.)*)"/);
  return nm ? decodeJsonString(nm[1]) : "";
}

function parsePlayerJson(json: string): {
  isPlaying: boolean;
  track: string;
  artist: string;
  deviceName: string;
} {
  const deviceName = parseDeviceName(json);
  let isPlaying = false;
  const mPlay = json.match(/"is_playing"\s*:\s*(true|false)/);
  if (mPlay) {
    isPlaying = mPlay[1] === "true";
  }
  if (json.includes('"item":null')) {
    return { isPlaying, track: "", artist: "", deviceName };
  }
  const itemIdx = json.indexOf('"item"');
  if (itemIdx < 0) {
    return { isPlaying, track: "", artist: "", deviceName };
  }
  const slice = json.slice(itemIdx, Math.min(json.length, itemIdx + 48_000));

  let artist = "";
  const artIdx = slice.indexOf(`"artists"`);
  if (artIdx >= 0) {
    const artSlice = slice.slice(artIdx, artIdx + 8000);
    const artMatch = artSlice.match(/"name"\s*:\s*"((?:[^"\\]|\\.)*)"/);
    if (artMatch) {
      artist = decodeJsonString(artMatch[1]);
    }
  }

  let track = "";
  const typeIdx = slice.lastIndexOf('"type":"track"');
  if (typeIdx > 0) {
    const before = slice.slice(0, typeIdx);
    const names = [...before.matchAll(/"name"\s*:\s*"((?:[^"\\]|\\.)*)"/g)];
    if (names.length > 0) {
      track = decodeJsonString(names[names.length - 1][1]);
    }
  }

  return { isPlaying, track, artist, deviceName };
}

async function readStatus(): Promise<{
  isPlaying: boolean;
  track: string;
  artist: string;
  deviceName: string;
}> {
  const res = await spotifyApi("GET", "/me/player");
  if (res.status === 204) {
    return { isPlaying: false, track: "", artist: "", deviceName: "" };
  }
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`player GET ${res.status}: ${text.slice(0, 200)}`);
  }
  return parsePlayerJson(text);
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  let body: ReqBody;
  try {
    body = (await req.json()) as ReqBody;
  } catch {
    return jsonResponse(400, { error: "Invalid JSON body" });
  }

  const action = (body.action ?? "status") as Action;

  try {
    if (action === "status") {
      const st = await readStatus();
      return jsonResponse(200, { ok: true, ...st });
    }

    if (action === "toggle") {
      const st = await readStatus();
      if (st.isPlaying) {
        const r = await spotifyApi("PUT", "/me/player/pause");
        if (!r.ok && r.status !== 204) {
          const t = await r.text();
          throw new Error(`pause ${r.status}: ${t.slice(0, 160)}`);
        }
      } else {
        const r = await spotifyApi("PUT", "/me/player/play");
        if (!r.ok && r.status !== 204) {
          const t = await r.text();
          throw new Error(`play ${r.status}: ${t.slice(0, 160)}`);
        }
      }
      const after = await readStatus();
      return jsonResponse(200, { ok: true, ...after });
    }

    if (action === "next") {
      const r = await spotifyApi("POST", "/me/player/next");
      if (!r.ok && r.status !== 204) {
        const t = await r.text();
        throw new Error(`next ${r.status}: ${t.slice(0, 160)}`);
      }
      const after = await readStatus();
      return jsonResponse(200, { ok: true, ...after });
    }

    if (action === "previous") {
      const r = await spotifyApi("POST", "/me/player/previous");
      if (!r.ok && r.status !== 204) {
        const t = await r.text();
        throw new Error(`previous ${r.status}: ${t.slice(0, 160)}`);
      }
      const after = await readStatus();
      return jsonResponse(200, { ok: true, ...after });
    }

    if (action === "play") {
      const r = await spotifyApi("PUT", "/me/player/play");
      if (!r.ok && r.status !== 204) {
        const t = await r.text();
        throw new Error(`play ${r.status}: ${t.slice(0, 160)}`);
      }
      const after = await readStatus();
      return jsonResponse(200, { ok: true, ...after });
    }

    if (action === "pause" || action === "stop") {
      const r = await spotifyApi("PUT", "/me/player/pause");
      if (!r.ok && r.status !== 204) {
        const t = await r.text();
        throw new Error(`pause ${r.status}: ${t.slice(0, 160)}`);
      }
      const after = await readStatus();
      return jsonResponse(200, { ok: true, ...after });
    }

    return jsonResponse(400, { error: "Unknown action", action });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    return jsonResponse(200, {
      ok: false,
      isPlaying: false,
      track: "",
      artist: "",
      deviceName: "",
      error: msg,
    });
  }
});
