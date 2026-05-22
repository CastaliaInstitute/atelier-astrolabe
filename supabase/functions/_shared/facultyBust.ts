import { createClient, type SupabaseClient } from "npm:@supabase/supabase-js@2.49.8";

function defaultSlug(): string {
  return Deno.env.get("FACULTY_DEFAULT_BUST_SLUG")?.trim() || "einstein";
}

function slugifyToken(word: string): string {
  return word
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "")
    .replace(/^-+|-+$/g, "");
}

const badFirstWord =
  /^(what|when|where|why|how|is|are|was|were|can|could|would|should|do|does|did|will|please|the|a|an|about|regarding)\b/i;

/**
 * Best-effort slug for Storage path `{slug}/bust.{ext}` from the routed faculty prompt.
 */
export function inferFacultySlug(facultyMessage: string): string {
  let rest = facultyMessage.trim();
  if (!rest) return defaultSlug();

  rest = rest.replace(/^\s*faculty\b[\s,:.-]*/i, "").trim();
  if (!rest) return defaultSlug();

  const leadStop =
    /^(about|regarding|the)\b/i.exec(rest);
  if (leadStop) {
    rest = rest.slice(leadStop[0].length).trim();
  }
  if (!rest) return defaultSlug();

  const firstWord = rest.split(/\s+/)[0] ?? "";
  if (badFirstWord.test(firstWord)) {
    return defaultSlug();
  }

  const slug = slugifyToken(firstWord.replace(/['’.]/g, ""));
  if (slug.length >= 2) return slug;
  return defaultSlug();
}

/** Treat query/body value as slug when it already looks slug-like; otherwise infer. */
export function normalizeFacultyParam(raw: string): string {
  const t = raw.trim();
  if (!t) return defaultSlug();
  if (/^[a-z0-9]+(?:[._-][a-z0-9]+)*$/.test(t)) return t.toLowerCase();
  return inferFacultySlug(t);
}

function supabaseAdmin(): SupabaseClient {
  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !key) {
    throw new Error("SUPABASE_URL or SUPABASE_SERVICE_ROLE_KEY missing");
  }
  return createClient(url, key);
}

function storagePrefix(): string {
  const p = Deno.env.get("FACULTY_BUST_PREFIX")?.trim() ?? "busts";
  return p.replace(/^\/+|\/+$/g, "");
}

function slugCandidates(slug: string): string[] {
  const out: string[] = [];
  const add = (s: string) => {
    const t = s.trim().toLowerCase();
    if (t && !out.includes(t)) out.push(t);
  };
  add(slug);
  if (/^a[.-]/.test(slug)) add(slug.slice(2));
  add(slug.replace(/\./g, "-"));
  add(slug.replace(/^a\./, "a-"));
  return out;
}

function bustLeaves(pose?: string): string[] {
  const normalized = (pose ?? "").trim().toLowerCase();
  if (normalized === "right" || normalized === "right-facing" || normalized === "profile-right") {
    return ["bust-right", "right-facing", "right", "bust"];
  }
  if (normalized === "left" || normalized === "left-facing" || normalized === "profile-left") {
    return ["bust-left", "left-facing", "left", "bust"];
  }
  return ["bust-right", "right-facing", "right", "bust"];
}

function generatedBustLeaf(pose?: string): string {
  const normalized = (pose ?? "").trim().toLowerCase();
  if (normalized === "left" || normalized === "left-facing" || normalized === "profile-left") {
    return "bust-left";
  }
  return "bust-right";
}

export function facultyBustPathIsPoseFallback(path: string, pose?: string): boolean {
  const normalized = (pose ?? "").trim().toLowerCase();
  if (
    normalized !== "right" && normalized !== "right-facing" && normalized !== "profile-right" &&
    normalized !== "left" && normalized !== "left-facing" && normalized !== "profile-left"
  ) {
    return false;
  }
  return /(^|\/)bust\.[a-z0-9]+$/i.test(path);
}

export function facultyBustPathCandidates(slug: string, pose?: string): { bucket: string; paths: string[] } {
  const bucket = Deno.env.get("FACULTY_BUST_BUCKET")?.trim() || "faculty";
  const prefix = storagePrefix();
  const exts =
    (Deno.env.get("FACULTY_BUST_EXTENSIONS") ?? "jpg,jpeg,png,webp")
      .split(",")
      .map((s) => s.trim().toLowerCase())
      .filter(Boolean);
  const paths: string[] = [];
  const leaves = bustLeaves(pose);
  for (const candidate of slugCandidates(slug)) {
    for (const ext of exts) {
      for (const name of leaves) {
        const leaf = `${candidate}/${name}.${ext}`;
        if (prefix) paths.push(`${prefix}/${leaf}`);
        paths.push(leaf);
      }
    }
  }
  return { bucket, paths };
}

function apiKey(): string {
  return (
    Deno.env.get("GOOGLE_GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_AI_API_KEY")?.trim() ||
    Deno.env.get("GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    ""
  );
}

function bytesToBase64(bytes: Uint8Array): string {
  let out = "";
  const chunkSize = 0x8000;
  for (let i = 0; i < bytes.length; i += chunkSize) {
    out += String.fromCharCode(...bytes.subarray(i, i + chunkSize));
  }
  return btoa(out);
}

function base64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) {
    out[i] = bin.charCodeAt(i);
  }
  return out;
}

function imageExtension(mimeType: string): string {
  if (/jpe?g/i.test(mimeType)) return "jpg";
  if (/webp/i.test(mimeType)) return "webp";
  return "png";
}

function firstGeneratedImage(data: unknown): { bytes: Uint8Array; mimeType: string } | null {
  const candidates = (data as {
    candidates?: Array<{ content?: { parts?: Array<Record<string, unknown>> } }>;
  })?.candidates;
  const parts = candidates?.[0]?.content?.parts ?? [];
  for (const part of parts) {
    const inline = (part.inlineData ?? part.inline_data) as { data?: unknown; mimeType?: unknown; mime_type?: unknown } | undefined;
    const b64 = typeof inline?.data === "string" ? inline.data : "";
    if (!b64) continue;
    const mimeType =
      (typeof inline?.mimeType === "string" && inline.mimeType) ||
      (typeof inline?.mime_type === "string" && inline.mime_type) ||
      "image/png";
    return { bytes: base64ToBytes(b64), mimeType };
  }
  return null;
}

export async function generateFacultyBustPoseIfMissing(slug: string, pose: string): Promise<{
  generated: boolean;
  path?: string;
  error?: string;
}> {
  const key = apiKey();
  if (!key) {
    return { generated: false, error: "missing Gemini image API key" };
  }

  const source = await signedFacultyBustUrl(slug);
  if (!source) {
    return { generated: false, error: "source bust not found" };
  }

  const sourceRes = await fetch(source.url, {
    headers: { Accept: "image/png,image/jpeg,image/webp,image/*;q=0.8,*/*;q=0.1" },
  });
  if (!sourceRes.ok) {
    return { generated: false, error: `source bust fetch failed: ${sourceRes.status}` };
  }

  const sourceMime = sourceRes.headers.get("Content-Type")?.split(";")[0]?.trim() || "image/png";
  const sourceBytes = new Uint8Array(await sourceRes.arrayBuffer());
  const model = Deno.env.get("FACULTY_BUST_IMAGE_MODEL")?.trim() || "gemini-3.1-flash-image-preview";
  const direction = /left/i.test(pose) ? "left-facing" : "right-facing";
  const prompt =
    `Use the reference portrait to create a ${direction} classical cameo profile bust of the same faculty figure. ` +
    "Preserve the person's identity and recognizable features. Show head, neck, shoulders, and upper bust only. " +
    "The figure should face sideways in true profile, sculptural and academic, softly lit, ethereal pale blue rim light. " +
    "Transparent or plain dark background, no text, no frame, no extra objects, square composition.";

  const res = await fetch(
    `https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(model)}:generateContent`,
    {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "x-goog-api-key": key,
      },
      body: JSON.stringify({
        contents: [{
          parts: [
            { text: prompt },
            {
              inline_data: {
                mime_type: sourceMime,
                data: bytesToBase64(sourceBytes),
              },
            },
          ],
        }],
        generationConfig: {
          responseModalities: ["IMAGE"],
        },
      }),
    },
  );
  const text = await res.text();
  if (!res.ok) {
    return { generated: false, error: `Gemini image failed: ${res.status} ${text.slice(0, 240)}` };
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return { generated: false, error: "Gemini image returned invalid JSON" };
  }
  const image = firstGeneratedImage(parsed);
  if (!image) {
    return { generated: false, error: "Gemini image returned no image" };
  }

  const bucket = Deno.env.get("FACULTY_BUST_BUCKET")?.trim() || "faculty";
  const ext = imageExtension(image.mimeType);
  const slash = source.path.lastIndexOf("/");
  const dir = slash >= 0 ? source.path.slice(0, slash) : (slugCandidates(source.slug)[0] ?? source.slug);
  const path = `${dir}/${generatedBustLeaf(pose)}.${ext}`;
  const supabase = supabaseAdmin();
  const uploaded = await supabase.storage.from(bucket).upload(path, image.bytes, {
    contentType: image.mimeType,
    upsert: true,
    cacheControl: "86400",
  });
  if (uploaded.error) {
    return { generated: false, error: uploaded.error.message };
  }
  return { generated: true, path };
}

/**
 * Signed URL for `bucket/{slug}/bust.{ext}` — tries extensions in order, then optional default slug.
 */
export async function signedFacultyBustUrl(
  slug: string,
  transform?: { width?: number; height?: number; quality?: number; resize?: "cover" | "contain" | "fill" },
  pose?: string,
): Promise<{ url: string; slug: string; path: string; bucket: string } | null> {
  const bucket = Deno.env.get("FACULTY_BUST_BUCKET")?.trim() || "faculty";
  const prefix = storagePrefix();
  const ttl = Number(Deno.env.get("FACULTY_BUST_SIGN_TTL_SEC") ?? "3600");
  const exts =
    (Deno.env.get("FACULTY_BUST_EXTENSIONS") ?? "jpg,jpeg,png,webp")
      .split(",")
      .map((s) => s.trim().toLowerCase())
      .filter(Boolean);

  const supabase = supabaseAdmin();

  async function createUrl(path: string, ext: string): Promise<string | null> {
    const opts: { transform?: typeof transform } = {};
    if (transform && ext !== "svg") {
      opts.transform = transform;
      const transformed = await supabase.storage
        .from(bucket)
        .createSignedUrl(path, ttl, opts);
      if (!transformed.error && transformed.data?.signedUrl) {
        return transformed.data.signedUrl;
      }
    }
    const original = await supabase.storage
      .from(bucket)
      .createSignedUrl(path, ttl);
    if (!original.error && original.data?.signedUrl) {
      return original.data.signedUrl;
    }
    return null;
  }

  async function trySlug(s: string): Promise<{ url: string; path: string } | null> {
    const leaves = bustLeaves(pose);
    for (const candidate of slugCandidates(s)) {
      for (const ext of exts) {
        for (const name of leaves) {
          const leaf = `${candidate}/${name}.${ext}`;
          const paths = prefix ? [`${prefix}/${leaf}`, leaf] : [leaf];
          for (const path of paths) {
            const url = await createUrl(path, ext);
            if (url) {
              return { url, path };
            }
          }
        }
      }
    }
    return null;
  }

  const primary = await trySlug(slug);
  if (primary) return { url: primary.url, slug, path: primary.path, bucket };

  const fallbackSlug = defaultSlug();
  if (fallbackSlug !== slug) {
    const fb = await trySlug(fallbackSlug);
    if (fb) return { url: fb.url, slug: fallbackSlug, path: fb.path, bucket };
  }

  return null;
}
