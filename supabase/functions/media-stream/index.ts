import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { corsHeaders, jsonResponse } from "../_shared/googleVoice.ts";

type MediaKind = "video" | "audio" | "web";

type MediaStreamRequest = {
  url?: string;
  sourceUrl?: string;
  title?: string;
  kind?: MediaKind;
  provider?: string;
  launchSlug?: string;
  redirect?: boolean;
};

type MediaStreamResponse = {
  protocol: "mynah.media-stream.v1";
  ok: boolean;
  kind: MediaKind;
  provider: string;
  title: string;
  sourceUrl: string;
  watchUrl: string;
  embedUrl?: string;
  imageUrl?: string;
  qrUrl: string;
  displayUrl: string;
  openMode: "external";
  canProxy: false;
  timeline?: {
    source: string;
    entries: Array<{ offsetSeconds: number; label: string; phase: "pre" | "post" }>;
  };
  notes: string[];
};

const mediaCorsHeaders: Record<string, string> = {
  ...corsHeaders,
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type, accept",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
};

const DEFAULT_TITLE = "Astrolabe stream";
const STARSHIP_12_OFFICIAL_PAGE = "https://www.spacex.com/launches/starship-flight-12";
const STARSHIP_12_WATCH_URL = "https://x.com/SpaceX/status/2057292435709927922";
const STARSHIP_12_API_URL = "https://content.spacex.com/api/spacex-website/missions/starship-flight-12";

function cleanString(value: unknown): string | undefined {
  return typeof value === "string" && value.trim() ? value.trim() : undefined;
}

function cleanKind(value: unknown): MediaKind {
  const s = cleanString(value)?.toLowerCase();
  if (s === "audio" || s === "web") return s;
  return "video";
}

function requestOrigin(req: Request): string {
  const url = new URL(req.url);
  return `${url.protocol}//${url.host}`;
}

function safeUrl(raw: string | undefined, origin: string): URL | undefined {
  if (!raw) return undefined;
  try {
    const parsed = new URL(raw, origin);
    if (parsed.protocol !== "https:" && parsed.protocol !== "http:") {
      return undefined;
    }
    return parsed;
  } catch {
    return undefined;
  }
}

function compactUrl(url: URL): string {
  url.hash = "";
  for (const key of [...url.searchParams.keys()]) {
    const lower = key.toLowerCase();
    if (
      lower.startsWith("utm_") || lower === "fbclid" || lower === "gclid" ||
      lower === "mc_cid" || lower === "mc_eid"
    ) {
      url.searchParams.delete(key);
    }
  }
  return url.toString();
}

function providerFromUrl(url: URL): string {
  const host = url.hostname.toLowerCase().replace(/^www\./, "");
  if (host === "youtu.be" || host.endsWith("youtube.com")) return "youtube";
  if (host.endsWith("spacex.com")) return "spacex";
  if (host.endsWith("rocketlaunch.live")) return "rocketlaunch.live";
  if (host === "x.com" || host === "twitter.com") return "x";
  if (host === "twitch.tv" || host.endsWith(".twitch.tv")) return "twitch";
  return host;
}

function youtubeId(url: URL): string | undefined {
  const host = url.hostname.toLowerCase().replace(/^www\./, "");
  if (host === "youtu.be") {
    const id = url.pathname.split("/").filter(Boolean)[0];
    return id || undefined;
  }
  if (!host.endsWith("youtube.com")) return undefined;
  if (url.pathname === "/watch") {
    return cleanString(url.searchParams.get("v") ?? undefined);
  }
  const parts = url.pathname.split("/").filter(Boolean);
  if ((parts[0] === "live" || parts[0] === "shorts" || parts[0] === "embed") && parts[1]) {
    return parts[1];
  }
  return undefined;
}

function normalizeWatchUrl(url: URL): { watchUrl: string; embedUrl?: string; provider: string; notes: string[] } {
  const provider = providerFromUrl(url);
  const notes: string[] = [];

  if (provider === "youtube") {
    const id = youtubeId(url);
    if (id) {
      notes.push("normalized YouTube URL");
      return {
        provider,
        watchUrl: `https://www.youtube.com/watch?v=${encodeURIComponent(id)}`,
        embedUrl: `https://www.youtube.com/embed/${encodeURIComponent(id)}`,
        notes,
      };
    }
  }

  if (provider === "spacex" && url.pathname.includes("/launches/")) {
    notes.push("SpaceX launch page; phone opens official webcast page");
  }

  return { provider, watchUrl: compactUrl(new URL(url.toString())), notes };
}

function mediaEndpointUrl(req: Request, watchUrl: string, title: string, kind: MediaKind): string {
  const endpoint = new URL(req.url);
  endpoint.protocol = "https:";
  if (!endpoint.pathname.startsWith("/functions/v1/")) {
    endpoint.pathname = "/functions/v1/media-stream";
  }
  endpoint.search = "";
  endpoint.searchParams.set("url", watchUrl);
  endpoint.searchParams.set("title", title);
  endpoint.searchParams.set("kind", kind);
  endpoint.searchParams.set("redirect", "1");
  return endpoint.toString();
}

async function bodyFromRequest(req: Request): Promise<MediaStreamRequest> {
  if (req.method === "GET") {
    const url = new URL(req.url);
    const compactLaunch = url.searchParams.get("l");
    return {
      url: url.searchParams.get("url") ?? url.searchParams.get("u") ?? undefined,
      title: url.searchParams.get("title") ?? url.searchParams.get("t") ?? undefined,
      kind: cleanKind(url.searchParams.get("kind") ?? url.searchParams.get("k") ?? undefined),
      provider: url.searchParams.get("provider") ?? url.searchParams.get("p") ?? undefined,
      launchSlug: url.searchParams.get("launchSlug") ?? (compactLaunch === "s12" ? "starship-flight-12" : compactLaunch ?? undefined),
      redirect: url.searchParams.get("redirect") === "1" ||
        url.searchParams.get("redirect") === "true" ||
        url.searchParams.get("r") === "1" ||
        url.searchParams.get("r") === "true",
    };
  }
  const ctype = req.headers.get("Content-Type") ?? "";
  if (ctype.includes("application/json")) {
    return await req.json() as MediaStreamRequest;
  }
  const form = await req.formData();
  return {
    url: cleanString(form.get("url")),
    sourceUrl: cleanString(form.get("sourceUrl")),
    title: cleanString(form.get("title")),
    kind: cleanKind(form.get("kind")),
    provider: cleanString(form.get("provider")),
    redirect: cleanString(form.get("redirect")) === "1" ||
      cleanString(form.get("redirect")) === "true",
  };
}

function wantsHtml(req: Request): boolean {
  const accept = req.headers.get("Accept") ?? "";
  return accept.includes("text/html");
}

function timelineSeconds(value: unknown): number | undefined {
  const s = cleanString(value);
  if (!s) return undefined;
  const m = s.match(/^(\d{2}):(\d{2}):(\d{2})$/);
  if (!m) return undefined;
  return Number(m[1]) * 3600 + Number(m[2]) * 60 + Number(m[3]);
}

function timelineLabel(value: unknown): string | undefined {
  const s = cleanString(value);
  if (!s) return undefined;
  return s.replace(/\s+/g, " ").slice(0, 56);
}

type Starship12Media = {
  imageUrl?: string;
  timeline?: MediaStreamResponse["timeline"];
};

async function starship12Media(): Promise<Starship12Media> {
  const res = await fetch(STARSHIP_12_API_URL, {
    headers: { "Accept": "application/json", "User-Agent": "Astrolabe/1.0" },
  });
  if (!res.ok) {
    console.error("media-stream timeline fetch failed:", res.status, await res.text());
    return {};
  }
  const data = await res.json() as {
    imageDesktop?: { formats?: Record<string, { url?: unknown }>; url?: unknown };
    imageMobile?: { formats?: Record<string, { url?: unknown }>; url?: unknown };
    preLaunchTimeline?: { timelineEntries?: Array<{ time?: unknown; description?: unknown }> };
    postLaunchTimeline?: { timelineEntries?: Array<{ time?: unknown; description?: unknown }> };
  };
  const imageUrl =
    cleanString(data.imageMobile?.formats?.small?.url) ??
    cleanString(data.imageMobile?.formats?.medium?.url) ??
    cleanString(data.imageDesktop?.formats?.small?.url) ??
    cleanString(data.imageDesktop?.formats?.medium?.url) ??
    cleanString(data.imageMobile?.url) ??
    cleanString(data.imageDesktop?.url);
  const entries: Array<{ offsetSeconds: number; label: string; phase: "pre" | "post" }> = [];
  for (const e of data.preLaunchTimeline?.timelineEntries ?? []) {
    const seconds = timelineSeconds(e.time);
    const label = timelineLabel(e.description);
    if (seconds != null && label) entries.push({ offsetSeconds: -seconds, label, phase: "pre" });
  }
  for (const e of data.postLaunchTimeline?.timelineEntries ?? []) {
    const seconds = timelineSeconds(e.time);
    const label = timelineLabel(e.description);
    if (seconds != null && label) entries.push({ offsetSeconds: seconds, label, phase: "post" });
  }
  return {
    imageUrl,
    timeline: entries.length ? { source: STARSHIP_12_API_URL, entries } : undefined,
  };
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: mediaCorsHeaders });
  }
  if (req.method !== "GET" && req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" }, mediaCorsHeaders);
  }

  try {
    const input = await bodyFromRequest(req);
    const origin = requestOrigin(req);
    const fallbackLaunch = input.launchSlug === "starship-flight-12"
      ? STARSHIP_12_WATCH_URL
      : undefined;
    const source = safeUrl(input.url ?? input.sourceUrl ?? fallbackLaunch, origin);
    if (!source) {
      return jsonResponse(400, { error: "Provide a valid http(s) url" }, mediaCorsHeaders);
    }

    const kind = cleanKind(input.kind);
    const normalized = normalizeWatchUrl(source);
    const provider = cleanString(input.provider) ?? normalized.provider;
    const title = cleanString(input.title) ?? DEFAULT_TITLE;
    const qrUrl = mediaEndpointUrl(req, normalized.watchUrl, title, kind);
    const payload: MediaStreamResponse = {
      protocol: "mynah.media-stream.v1",
      ok: true,
      kind,
      provider,
      title,
      sourceUrl: source.toString(),
      watchUrl: normalized.watchUrl,
      embedUrl: normalized.embedUrl,
      qrUrl,
      displayUrl: qrUrl,
      openMode: "external",
      canProxy: false,
      notes: [
        ...normalized.notes,
        ...(input.launchSlug === "starship-flight-12"
          ? [`official SpaceX page: ${STARSHIP_12_OFFICIAL_PAGE}`]
          : []),
        "Edge function normalizes stream metadata; video playback is delegated to the phone/browser.",
      ],
    };
    if (input.launchSlug === "starship-flight-12") {
      const media = await starship12Media();
      payload.imageUrl = media.imageUrl;
      payload.timeline = media.timeline;
    }

    if (input.redirect) {
      return Response.redirect(payload.watchUrl, 302);
    }
    if (wantsHtml(req)) {
      return Response.redirect(payload.watchUrl, 302);
    }
    return jsonResponse(200, payload, {
      ...mediaCorsHeaders,
      "x-mynah-media-kind": payload.kind,
      "x-mynah-media-provider": payload.provider,
      "x-mynah-media-watch-url": encodeURIComponent(payload.watchUrl),
    });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.error("media-stream error:", msg);
    return jsonResponse(500, { error: msg }, mediaCorsHeaders);
  }
});
