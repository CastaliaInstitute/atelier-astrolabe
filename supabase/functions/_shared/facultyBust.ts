import {
  createClient,
  type SupabaseClient,
} from "npm:@supabase/supabase-js@2.49.8";

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

  const leadStop = /^(about|regarding|the)\b/i.exec(rest);
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

/** Resolve faculty slug from common query param names (`faculty`, `handle`, `slug`). */
export function resolveFacultySlugFromSearchParams(
  params: URLSearchParams,
): string {
  const raw = params.get("faculty")?.trim() ||
    params.get("handle")?.trim() ||
    params.get("slug")?.trim() ||
    "";
  return normalizeFacultyParam(raw);
}

/** Device default: right 3/4 (`bust.png`). Web hero cards may request `frontal`; e-paper may request cached `line`. */
export type FacultyBustView = "right" | "frontal" | "line";

export function resolveFacultyBustView(
  params: URLSearchParams,
): FacultyBustView {
  const raw = (params.get("view") ?? params.get("variant") ?? "right").trim()
    .toLowerCase();
  if (raw === "frontal" || raw === "front" || raw === "forward") {
    return "frontal";
  }
  if (
    raw === "line" || raw === "line-bust" || raw === "line_bust" ||
    raw === "etch" || raw === "ink"
  ) {
    return "line";
  }
  return "right";
}

function bustStemNames(view: FacultyBustView): string[] {
  if (view === "frontal") {
    return ["bust_frontal"];
  }
  if (view === "line") {
    return ["line_bust", "bust_line"];
  }
  return ["bust"];
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

function storageBucket(): string {
  return Deno.env.get("FACULTY_BUST_BUCKET")?.trim() || "faculty";
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

function canonicalStorageSlug(slug: string): string {
  const candidates = slugCandidates(slug);
  const withoutNamespace = candidates.find((s) => !/^a[.-]/.test(s));
  return (withoutNamespace ?? candidates[0] ?? defaultSlug()).replace(
    /\./g,
    "-",
  );
}

function normalizeStoragePath(path: string, bucket: string): string {
  let p = path.trim();
  p = p.replace(/^ss:\/\//, "");
  p = p.replace(/^\/+/, "");
  if (p.startsWith(`${bucket}/`)) p = p.slice(bucket.length + 1);
  return p;
}

async function facultyLineBustRow(
  slug: string,
): Promise<
  { id?: string | null; slug?: string | null; path?: string | null } | null
> {
  const supabase = supabaseAdmin();
  const select = "id,slug,line_bust_path";
  for (const candidate of slugCandidates(slug)) {
    const byId = await supabase.from("faculty").select(select).eq(
      "id",
      candidate,
    ).maybeSingle();
    if (!byId.error && byId.data) {
      return {
        id: byId.data.id,
        slug: byId.data.slug,
        path: byId.data.line_bust_path,
      };
    }

    const bySlug = await supabase.from("faculty").select(select).eq(
      "slug",
      candidate,
    ).maybeSingle();
    if (!bySlug.error && bySlug.data) {
      return {
        id: bySlug.data.id,
        slug: bySlug.data.slug,
        path: bySlug.data.line_bust_path,
      };
    }
  }
  return null;
}

export function facultyLineBustStoragePath(slug: string): string {
  const prefix = storagePrefix();
  const leaf = `${canonicalStorageSlug(slug)}/line_bust.png`;
  return prefix ? `${prefix}/${leaf}` : leaf;
}

export async function updateFacultyLineBustPath(
  slug: string,
  path: string,
): Promise<void> {
  const supabase = supabaseAdmin();
  const row = await facultyLineBustRow(slug);
  const normalized = normalizeStoragePath(path, storageBucket());
  if (row?.id) {
    const byId = await supabase.from("faculty").update({
      line_bust_path: normalized,
    }).eq("id", row.id);
    if (!byId.error) return;
    console.warn("faculty line bust: update by id failed", byId.error.message);
  }
  if (row?.slug) {
    const bySlug = await supabase.from("faculty").update({
      line_bust_path: normalized,
    }).eq("slug", row.slug);
    if (!bySlug.error) return;
    console.warn(
      "faculty line bust: update by slug failed",
      bySlug.error.message,
    );
  }
  for (const candidate of slugCandidates(slug)) {
    const byId = await supabase.from("faculty").update({
      line_bust_path: normalized,
    }).eq("id", candidate);
    if (!byId.error) return;
    const bySlug = await supabase.from("faculty").update({
      line_bust_path: normalized,
    }).eq("slug", candidate);
    if (!bySlug.error) return;
  }
}

export async function uploadFacultyLineBustPng(
  slug: string,
  png: Uint8Array,
): Promise<{ bucket: string; path: string }> {
  const supabase = supabaseAdmin();
  const bucket = storageBucket();
  const path = facultyLineBustStoragePath(slug);
  const upload = await supabase.storage.from(bucket).upload(path, png, {
    contentType: "image/png",
    upsert: true,
  });
  if (upload.error) {
    throw new Error(`faculty line bust upload failed: ${upload.error.message}`);
  }
  await updateFacultyLineBustPath(slug, path);
  return { bucket, path };
}

export function facultyBustPathCandidates(
  slug: string,
  view: FacultyBustView = "right",
): { bucket: string; paths: string[] } {
  const bucket = storageBucket();
  const prefix = storagePrefix();
  const exts = (Deno.env.get("FACULTY_BUST_EXTENSIONS") ?? "png,webp,jpg,jpeg")
    .split(",")
    .map((s) => s.trim().toLowerCase())
    .filter(Boolean);
  const paths: string[] = [];
  for (const candidate of slugCandidates(slug)) {
    for (const stem of bustStemNames(view)) {
      for (const ext of exts) {
        const leaf = `${candidate}/${stem}.${ext}`;
        if (prefix) paths.push(`${prefix}/${leaf}`);
        paths.push(leaf);
      }
    }
  }
  return { bucket, paths };
}

/**
 * Signed URL for `bucket/{slug}/bust.{ext}` — tries extensions in order, then optional default slug.
 */
export async function signedFacultyBustUrl(
  slug: string,
  transform?: {
    width?: number;
    height?: number;
    quality?: number;
    resize?: "cover" | "contain" | "fill";
  },
  view: FacultyBustView = "right",
): Promise<{ url: string; slug: string; path: string; bucket: string } | null> {
  const bucket = storageBucket();
  const prefix = storagePrefix();
  const ttl = Number(Deno.env.get("FACULTY_BUST_SIGN_TTL_SEC") ?? "3600");
  const exts = (Deno.env.get("FACULTY_BUST_EXTENSIONS") ?? "png,webp,jpg,jpeg")
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

  if (view === "line") {
    const row = await facultyLineBustRow(slug);
    const cachedPath = row?.path?.trim();
    if (cachedPath) {
      const path = normalizeStoragePath(cachedPath, bucket);
      const ext = path.split(".").pop()?.toLowerCase() || "png";
      const url = await createUrl(path, ext);
      if (url) {
        return { url, slug, path, bucket };
      }
    }
  }

  async function trySlug(
    s: string,
  ): Promise<{ url: string; path: string } | null> {
    for (const candidate of slugCandidates(s)) {
      for (const stem of bustStemNames(view)) {
        for (const ext of exts) {
          const leaf = `${candidate}/${stem}.${ext}`;
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
