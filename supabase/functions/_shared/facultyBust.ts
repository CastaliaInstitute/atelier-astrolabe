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

export function facultyBustPathCandidates(slug: string): { bucket: string; paths: string[] } {
  const bucket = Deno.env.get("FACULTY_BUST_BUCKET")?.trim() || "faculty";
  const prefix = storagePrefix();
  const exts =
    (Deno.env.get("FACULTY_BUST_EXTENSIONS") ?? "jpg,jpeg,png,webp")
      .split(",")
      .map((s) => s.trim().toLowerCase())
      .filter(Boolean);
  const paths: string[] = [];
  for (const candidate of slugCandidates(slug)) {
    for (const ext of exts) {
      const leaf = `${candidate}/bust.${ext}`;
      if (prefix) paths.push(`${prefix}/${leaf}`);
      paths.push(leaf);
    }
  }
  return { bucket, paths };
}

/**
 * Signed URL for `bucket/{slug}/bust.{ext}` — tries extensions in order, then optional default slug.
 */
export async function signedFacultyBustUrl(
  slug: string,
  transform?: { width?: number; height?: number; quality?: number; resize?: "cover" | "contain" | "fill" },
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
    for (const candidate of slugCandidates(s)) {
      for (const ext of exts) {
        const leaf = `${candidate}/bust.${ext}`;
        const paths = prefix ? [`${prefix}/${leaf}`, leaf] : [leaf];
        for (const path of paths) {
          const url = await createUrl(path, ext);
          if (url) {
            return { url, path };
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
