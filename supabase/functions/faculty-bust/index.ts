import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { Image } from "https://deno.land/x/imagescript@1.2.15/mod.ts";

import {
  facultyBustPathIsPoseFallback,
  facultyBustPathCandidates,
  generateFacultyBustPoseIfMissing,
  normalizeFacultyParam,
  signedFacultyBustUrl,
} from "../_shared/facultyBust.ts";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, OPTIONS",
};

function clampInt(value: string | null, fallback: number, lo: number, hi: number): number {
  const n = Number(value ?? "");
  if (!Number.isFinite(n)) return fallback;
  return Math.max(lo, Math.min(hi, Math.round(n)));
}

async function resizeBust(upstream: Response, width: number, height: number, quality: number, fit: string, format: string) {
  const src = new Uint8Array(await upstream.arrayBuffer());
  const image = await Image.decode(src);
  if (fit === "cover") {
    image.cover(width, height);
  } else {
    image.fit(width, height);
  }
  if (format === "jpg" || format === "jpeg") {
    return { bytes: await image.encodeJPEG(quality), contentType: "image/jpeg", transform: "imagescript-jpeg" };
  }
  return { bytes: await image.encode(2), contentType: "image/png", transform: "imagescript-png" };
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "GET") {
    return Response.json({ error: "Use GET" }, { status: 405, headers: corsHeaders });
  }

  const u = new URL(req.url);
  const slug = normalizeFacultyParam(u.searchParams.get("faculty") ?? u.searchParams.get("slug") ?? "");
  const width = clampInt(u.searchParams.get("w") ?? u.searchParams.get("width"), 200, 48, 400);
  const height = clampInt(u.searchParams.get("h") ?? u.searchParams.get("height"), 200, 48, 400);
  const quality = clampInt(u.searchParams.get("q") ?? u.searchParams.get("quality"), 72, 35, 90);
  const resize = (u.searchParams.get("resize") ?? "contain").trim().toLowerCase();
  const fit = resize === "cover" || resize === "fill" ? resize : "contain";
  const format = (u.searchParams.get("format") ?? "png").trim().toLowerCase();
  const pose = (u.searchParams.get("pose") ?? "right").trim().toLowerCase();

  try {
    let generatedPath = "";
    let generatedError = "";
    let signed = await signedFacultyBustUrl(slug, { width, height, quality, resize: fit }, pose);
    if (signed && facultyBustPathIsPoseFallback(signed.path, pose)) {
      const generated = await generateFacultyBustPoseIfMissing(slug, pose);
      if (generated.generated) {
        generatedPath = generated.path ?? "";
        signed = await signedFacultyBustUrl(slug, { width, height, quality, resize: fit }, pose) ?? signed;
      } else {
        generatedError = generated.error ?? "generation skipped";
      }
    }
    if (!signed) {
      const candidates = facultyBustPathCandidates(slug, pose);
      return Response.json(
        { error: "faculty bust not found", faculty: slug, pose, ...candidates },
        { status: 404, headers: { ...corsHeaders, "Cache-Control": "no-store" } },
      );
    }

    const upstream = await fetch(signed.url, {
      headers: { Accept: "image/jpeg,image/png,image/webp,image/*;q=0.8,*/*;q=0.1" },
    });
    if (!upstream.ok || !upstream.body) {
      return Response.json(
        { error: "faculty bust storage fetch failed", status: upstream.status, faculty: signed.slug },
        { status: 502, headers: { ...corsHeaders, "Cache-Control": "no-store" } },
      );
    }

    const out = await resizeBust(upstream, width, height, quality, fit, format);

    const h = new Headers(corsHeaders);
    h.set("Content-Type", out.contentType);
    h.set("Cache-Control", "public, max-age=86400, stale-while-revalidate=604800");
    h.set("X-Faculty-Slug", signed.slug);
    h.set("X-Faculty-Bust-Bucket", signed.bucket);
    h.set("X-Faculty-Bust-Path", signed.path);
    h.set("X-Faculty-Bust-Pose", pose);
    if (generatedPath) h.set("X-Faculty-Bust-Generated", generatedPath);
    if (generatedError) h.set("X-Faculty-Bust-Generation-Error", generatedError.slice(0, 180));
    h.set("X-Faculty-Bust-Size", `${width}x${height}`);
    h.set("X-Faculty-Bust-Transform", out.transform);
    h.set("Content-Length", String(out.bytes.byteLength));
    return new Response(out.bytes, { status: 200, headers: h });
  } catch (err) {
    console.error("faculty-bust failed", err);
    return Response.json(
      { error: "faculty bust failed", faculty: slug },
      { status: 500, headers: { ...corsHeaders, "Cache-Control": "no-store" } },
    );
  }
});
