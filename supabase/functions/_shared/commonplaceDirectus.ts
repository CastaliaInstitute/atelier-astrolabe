/**
 * Append Mynah conversation / artifact entries to Commonplace.
 * Prefers Directus when DIRECTUS_STATIC_TOKEN is configured, then falls back
 * to the Supabase database that Directus fronts.
 */

import { createClient, type SupabaseClient } from "npm:@supabase/supabase-js@2.49.8";
import {
  embedRetrievalQuery,
  embeddingApiKey,
  vectorLiteral,
} from "./geminiEmbedding.ts";

type EdgeRt = { waitUntil: (promise: Promise<unknown>) => void };

function edgeWaitUntil(promise: Promise<unknown>): void {
  const er = (globalThis as unknown as { EdgeRuntime?: EdgeRt }).EdgeRuntime;
  if (er?.waitUntil) {
    er.waitUntil(promise);
  } else {
    void promise;
  }
}

export function scheduleMynahCommonplaceLog(
  authHeader: string,
  payload: MynahCommonplacePayload,
): void {
  edgeWaitUntil(
    appendMynahCommonplaceEntry(authHeader, payload).catch((e) =>
      console.error("mynah commonplace log:", e)
    ),
  );
}

export type MynahCommonplacePayload =
  | {
    kind: "conversation";
    route: string;
    transcript: string;
    reply: string;
    facultySlug?: string | null;
    facultyName?: string | null;
    memorySummary?: string | null;
  }
  | {
    kind: "faculty_memory";
    route: string;
    summary: string;
    transcript: string;
    reply: string;
    facultySlug: string;
    facultyName?: string | null;
  }
  | {
    kind: "artifact";
    artifactType: string;
    summary: string;
    detail?: string;
    deviceLabel?: string;
  }
  | {
    kind: "journal";
    transcript: string;
    deviceLabel?: string;
  };

function formatDate(): string {
  return new Date().toLocaleDateString("en-US", {
    weekday: "long",
    year: "numeric",
    month: "long",
    day: "numeric",
  });
}

function generateSlug(title: string): string {
  const base = title
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .substring(0, 50);
  return `${base}-${Date.now().toString(36)}`;
}

function truncate(s: string, max: number): string {
  const t = s.trim();
  if (t.length <= max) return t;
  return `${t.slice(0, max)}…`;
}

function fenceBlock(text: string): string {
  const escaped = text.replace(/```/g, "\\`\\`\\`");
  return "```\n" + escaped + "\n```";
}

async function resolveActor(
  authHeader: string,
  supabaseUrl: string | undefined,
  anonKey: string | undefined,
): Promise<{ email: string | null; label: string }> {
  const token = authHeader.replace(/^Bearer\s+/i, "").trim();
  if (!token || !supabaseUrl || !anonKey || token === anonKey) {
    return { email: null, label: "Unsigned Mynah" };
  }
  try {
    const r = await fetch(`${supabaseUrl.replace(/\/$/, "")}/auth/v1/user`, {
      headers: {
        Authorization: `Bearer ${token}`,
        apikey: anonKey,
      },
    });
    if (!r.ok) return { email: null, label: "Mynah user" };
    const j = (await r.json()) as { email?: string; id?: string };
    const email = j.email?.trim() || null;
    return {
      email,
      label: email ?? (j.id ? `user ${j.id.slice(0, 8)}…` : "Mynah user"),
    };
  } catch {
    return { email: null, label: "Mynah user" };
  }
}

async function fetchPersonBySlug(
  directusUrl: string,
  token: string,
  slug: string,
): Promise<{ id: string; name: string; slug: string } | null> {
  const headers = {
    Authorization: `Bearer ${token}`,
    "Content-Type": "application/json",
  };
  const q = `${directusUrl}/items/persons?filter[slug][_eq]=${encodeURIComponent(slug)}&fields=id,name,slug`;
  const r = await fetch(q, { headers });
  if (!r.ok) return null;
  const j = (await r.json()) as { data?: Array<{ id: string; name: string; slug: string }> };
  const row = j.data?.[0];
  if (row) return row;

  const q2 =
    `${directusUrl}/items/persons?filter[slug][_eq]=${encodeURIComponent("a." + slug)}&fields=id,name,slug`;
  const r2 = await fetch(q2, { headers });
  if (!r2.ok) return null;
  const j2 = (await r2.json()) as { data?: Array<{ id: string; name: string; slug: string }> };
  return j2.data?.[0] ?? null;
}

function buildContent(
  dateStr: string,
  actor: { email: string | null; label: string },
  payload: MynahCommonplacePayload,
): { title: string; content: string; abstract: string; workType: string } {
  const actorLine = actor.email
    ? `**Account:** ${actor.email}`
    : `**Session:** ${actor.label}`;

  if (payload.kind === "conversation") {
    const route = payload.route.trim() || "unknown";
    const ts = truncate(payload.transcript, 12_000);
    const rep = truncate(payload.reply, 12_000);
    const fac = payload.facultySlug?.trim();
    const facultyName = payload.facultyName?.trim();
    const memorySummary = payload.memorySummary?.trim();
    const title = `${dateStr}: Mynah (${route})`;
    let content = `## Mynah conversation\n\n`;
    content += `**When:** ${dateStr}\n\n`;
    content += `${actorLine}\n\n`;
    content += `**Route:** \`${route}\`\n\n`;
    if (fac) content += `**Faculty slug:** \`${fac}\`\n\n`;
    if (facultyName) content += `**Faculty:** ${facultyName}\n\n`;
    if (memorySummary) content += `### Memory summary\n\n${memorySummary}\n\n`;
    content += `### User / transcript\n\n${fenceBlock(ts)}\n\n`;
    content += `### Assistant\n\n${fenceBlock(rep)}\n\n`;
    content += `---\n*Logged from Mynah → Commonplace (${route})*\n`;
    const abstract = truncate(memorySummary || `${route}: ${ts}`, 220);
    return { title, content, abstract, workType: "mynah_conversation" };
  }

  if (payload.kind === "faculty_memory") {
    const route = payload.route.trim() || "ask-faculty";
    const facultySlug = payload.facultySlug.trim();
    const facultyName = payload.facultyName?.trim();
    const summary = truncate(payload.summary, 4000);
    const title = `${dateStr}: Faculty memory — ${facultyName || facultySlug}`;
    let content = `## Faculty memory\n\n`;
    content += `**When:** ${dateStr}\n\n`;
    content += `${actorLine}\n\n`;
    content += `**Route:** \`${route}\`\n\n`;
    content += `**Faculty slug:** \`${facultySlug}\`\n\n`;
    if (facultyName) content += `**Faculty:** ${facultyName}\n\n`;
    content += `### Memory\n\n${summary}\n\n`;
    content += `### Source turn\n\n`;
    content += `**User / transcript**\n\n${fenceBlock(truncate(payload.transcript, 4000))}\n\n`;
    content += `**Assistant**\n\n${fenceBlock(truncate(payload.reply, 4000))}\n\n`;
    content += `---\n*Faculty memory generated from Mynah → Commonplace (${route})*\n`;
    return {
      title,
      content,
      abstract: truncate(summary, 220),
      workType: "mynah_faculty_memory",
    };
  }

  if (payload.kind === "journal") {
    const ts = truncate(payload.transcript, 24_000);
    const dev = payload.deviceLabel?.trim();
    const title = `${dateStr}: Journal`;
    let content = `## Journal\n\n`;
    content += `**When:** ${dateStr}\n\n`;
    content += `${actorLine}\n\n`;
    if (dev) content += `**Device:** ${dev}\n\n`;
    content += `${ts}\n\n`;
    content += `---\n*Logged from ${dev ?? "Mynah"} → Commonplace*\n`;
    const abstract = truncate(ts, 220);
    return { title, content, abstract, workType: "journal" };
  }

  const dev = payload.deviceLabel?.trim();
  const detail = payload.detail?.trim();
  const title = `${dateStr}: Mynah artifact — ${payload.artifactType}`;
  let content = `## Mynah artifact\n\n`;
  content += `**When:** ${dateStr}\n\n`;
  content += `${actorLine}\n\n`;
  content += `**Type:** \`${payload.artifactType}\`\n\n`;
  if (dev) content += `**Device:** ${dev}\n\n`;
  content += `### Summary\n\n${payload.summary.trim()}\n\n`;
  if (detail) content += `### Detail\n\n${fenceBlock(truncate(detail, 8000))}\n\n`;
  content += `---\n*Logged from Mynah → Commonplace*\n`;
  const abstract = truncate(payload.summary, 220);
  return { title, content, abstract, workType: "mynah_artifact" };
}

function toDatabaseWorkType(workType: string): string {
  if (workType === "journal" || workType.startsWith("mynah_")) return "note";
  return workType;
}

function actorNeedle(actor: { email: string | null; label: string }): string {
  return actor.email ? `**Account:** ${actor.email}` : `**Session:** ${actor.label}`;
}

function actorMemoryScope(actor: { email: string | null; label: string }): string {
  return actor.email ? `Account:${actor.email}` : "Session";
}

function memoryExcerpt(content: string | null | undefined): string {
  const source = content ?? "";
  const match = source.match(/### Memory\s+([\s\S]*?)(?:\n### |\n---|$)/);
  const extracted = match?.[1]?.trim() || source.trim();
  return truncate(extracted.replace(/\s+/g, " "), 900);
}

function applyActorMemoryScope(
  builder: any,
  actor: { email: string | null; label: string },
): any {
  if (actor.email) {
    return builder.ilike("content_md", `%${actorNeedle(actor)}%`);
  }

  // Unsigned device calls do not have a stable user id. Treat anonymous session
  // labels as equivalent, but do not cross into authenticated account memories.
  return builder.ilike("content_md", "%**Session:**%");
}

function formatMemoryRows(data: Array<any>, limit: number): string {
  return data
    .slice(0, limit)
    .map((row, index) => {
      const title = typeof row.title === "string" ? row.title : `Memory ${index + 1}`;
      const abstract = typeof row.abstract === "string" ? row.abstract.trim() : "";
      const excerpt = memoryExcerpt(row.content_md);
      const similarity = typeof row.similarity === "number"
        ? ` (similarity ${row.similarity.toFixed(2)})`
        : "";
      return `- ${title}${similarity}: ${abstract || excerpt}`;
    })
    .join("\n");
}

async function insertWork(
  directusUrl: string,
  token: string,
  work: Record<string, unknown>,
): Promise<void> {
  const r = await fetch(`${directusUrl}/items/works`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify(work),
  });
  if (!r.ok) {
    const txt = await r.text();
    throw new Error(`Directus ${r.status}: ${truncate(txt, 400)}`);
  }
}

async function fetchSupabasePersonBySlug(
  db: SupabaseClient,
  slug: string,
): Promise<{ id: string; name: string; slug: string } | null> {
  const select = "id,name,slug";
  const { data, error } = await db.from("persons").select(select).eq("slug", slug).maybeSingle();
  if (error) {
    console.warn("mynah commonplace: Supabase author lookup failed", {
      slug,
      message: error.message,
    });
    return null;
  }
  if (data) return data as { id: string; name: string; slug: string };

  const alternateSlug = `a.${slug}`;
  const alt = await db.from("persons").select(select).eq("slug", alternateSlug).maybeSingle();
  if (alt.error) {
    console.warn("mynah commonplace: Supabase alternate author lookup failed", {
      slug: alternateSlug,
      message: alt.error.message,
    });
    return null;
  }
  return (alt.data as { id: string; name: string; slug: string } | null) ?? null;
}

async function appendViaSupabase(
  authHeader: string,
  payload: MynahCommonplacePayload,
  authorSlug: string,
): Promise<boolean> {
  const SUPABASE_URL = Deno.env.get("SUPABASE_URL")?.trim();
  const SUPABASE_ANON_KEY = Deno.env.get("SUPABASE_ANON_KEY")?.trim();
  const serviceRole = (
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? Deno.env.get("SERVICE_ROLE_KEY") ?? ""
  ).trim();

  if (!SUPABASE_URL || !serviceRole) {
    console.warn("mynah commonplace: Supabase service credentials not set; skip log");
    return false;
  }

  const db = createClient(SUPABASE_URL, serviceRole, {
    auth: { autoRefreshToken: false, persistSession: false },
  });
  const actor = await resolveActor(authHeader, SUPABASE_URL, SUPABASE_ANON_KEY);
  const author = await fetchSupabasePersonBySlug(db, authorSlug);
  if (!author) {
    console.warn(`mynah commonplace: author slug not found: ${authorSlug}`);
    return false;
  }

  const dateStr = formatDate();
  const { title, content, abstract, workType } = buildContent(dateStr, actor, payload);
  const slug = generateSlug(title);
  const status = Deno.env.get("MYNAH_COMMONPLACE_STATUS")?.trim() || "draft";
  const visibility = Deno.env.get("MYNAH_COMMONPLACE_VISIBILITY")?.trim() || "private";

  const { error } = await db.from("works").insert({
    title,
    slug,
    abstract,
    content_md: content,
    primary_author_id: author.id,
    work_type: toDatabaseWorkType(workType),
    status,
    visibility,
    publication_date: new Date().toISOString().split("T")[0],
  });

  if (error) {
    console.warn("mynah commonplace: Supabase work insert failed", {
      message: error.message,
      code: error.code,
    });
    return false;
  }

  console.log("mynah commonplace: work created via supabase", { slug, workType });
  return true;
}

export async function fetchFacultyMemoryContext(
  authHeader: string,
  facultySlug: string | null | undefined,
  query: string,
  limit = 5,
): Promise<string> {
  const slug = facultySlug?.trim();
  if (!slug || Deno.env.get("MYNAH_COMMONPLACE_MEMORY_DISABLED") === "true") return "";

  const SUPABASE_URL = Deno.env.get("SUPABASE_URL")?.trim();
  const SUPABASE_ANON_KEY = Deno.env.get("SUPABASE_ANON_KEY")?.trim();
  const serviceRole = (
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? Deno.env.get("SERVICE_ROLE_KEY") ?? ""
  ).trim();
  if (!SUPABASE_URL || !serviceRole) return "";

  const db = createClient(SUPABASE_URL, serviceRole, {
    auth: { autoRefreshToken: false, persistSession: false },
  });
  const actor = await resolveActor(authHeader, SUPABASE_URL, SUPABASE_ANON_KEY);
  const apiKey = embeddingApiKey();

  if (apiKey && query.trim()) {
    try {
      const embedding = await embedRetrievalQuery(apiKey, query);
      const { data, error } = await db.rpc("search_faculty_memory_embeddings", {
        query_embedding: vectorLiteral(embedding),
        filter_actor_scope: actorMemoryScope(actor),
        filter_faculty_slug: slug,
        match_count: limit,
        match_threshold: Number(Deno.env.get("MYNAH_COMMONPLACE_MEMORY_VECTOR_THRESHOLD") ?? 0.35),
      });
      if (!error && data?.length) {
        return formatMemoryRows(data as Array<any>, limit);
      }
      if (error) {
        console.warn("mynah commonplace: faculty vector memory lookup failed", error.message);
      }
    } catch (e) {
      console.warn("mynah commonplace: faculty memory embedding failed", e);
    }
  }

  const terms = query
    .toLowerCase()
    .replace(/[^a-z0-9\s]+/g, " ")
    .split(/\s+/)
    .filter((term) => term.length >= 4)
    .slice(0, 8);

  try {
    let builder = db
      .from("works")
      .select("title,abstract,content_md,created_at")
      .eq("work_type", "note")
      .ilike("content_md", "%## Faculty memory%")
      .ilike("content_md", `%**Faculty slug:** \`${slug}\`%`)
      .order("created_at", { ascending: false })
      .limit(Math.max(1, Math.min(limit * 3, 15)));
    builder = applyActorMemoryScope(builder, actor);

    if (terms.length) {
      builder = builder.or(
        terms
          .map((term) => `abstract.ilike.%${term}%,content_md.ilike.%${term}%`)
          .join(","),
      );
    }

    let { data, error } = await builder;
    if (error || !data?.length) {
      const fallback = db
        .from("works")
        .select("title,abstract,content_md,created_at")
        .eq("work_type", "note")
        .ilike("content_md", "%## Faculty memory%")
        .ilike("content_md", `%**Faculty slug:** \`${slug}\`%`)
        .order("created_at", { ascending: false })
        .limit(Math.max(1, Math.min(limit, 10)));
      const scopedFallback = applyActorMemoryScope(fallback, actor);
      const scopedResult = await scopedFallback;
      data = scopedResult.data;
      error = scopedResult.error;
    }
    if (error || !data?.length) return "";

    return formatMemoryRows(data as Array<any>, limit);
  } catch (e) {
    console.warn("mynah commonplace: faculty memory lookup failed", e);
    return "";
  }
}

export async function appendMynahCommonplaceEntry(
  authHeader: string,
  payload: MynahCommonplacePayload,
): Promise<boolean> {
  if (Deno.env.get("MYNAH_COMMONPLACE_DISABLED") === "true") return false;

  const DIRECTUS_URL = (
    Deno.env.get("DIRECTUS_URL") ?? "https://commonplace.castalia.institute"
  ).replace(/\/+$/, "");
  const DIRECTUS_TOKEN = Deno.env.get("DIRECTUS_STATIC_TOKEN")?.trim();
  const authorSlug = (Deno.env.get("MYNAH_COMMONPLACE_AUTHOR_SLUG") ?? "custodian").trim();
  if (!DIRECTUS_TOKEN) {
    console.warn("mynah commonplace: DIRECTUS_STATIC_TOKEN not set; using Supabase fallback");
    return await appendViaSupabase(authHeader, payload, authorSlug);
  }

  const SUPABASE_URL = Deno.env.get("SUPABASE_URL")?.trim();
  const SUPABASE_ANON_KEY = Deno.env.get("SUPABASE_ANON_KEY")?.trim();

  const actor = await resolveActor(authHeader, SUPABASE_URL, SUPABASE_ANON_KEY);
  const author = await fetchPersonBySlug(DIRECTUS_URL, DIRECTUS_TOKEN, authorSlug);
  if (!author) {
    console.warn(`mynah commonplace: author slug not found: ${authorSlug}`);
    return false;
  }

  const dateStr = formatDate();
  const { title, content, abstract, workType } = buildContent(dateStr, actor, payload);
  const slug = generateSlug(title);
  const status = Deno.env.get("MYNAH_COMMONPLACE_STATUS")?.trim() || "draft";
  const visibility = Deno.env.get("MYNAH_COMMONPLACE_VISIBILITY")?.trim() || "private";

  await insertWork(DIRECTUS_URL, DIRECTUS_TOKEN, {
    title,
    slug,
    abstract,
    content_md: content,
    primary_author_id: author.id,
    work_type: toDatabaseWorkType(workType),
    status,
    visibility,
    publication_date: new Date().toISOString().split("T")[0],
  });

  console.log("mynah commonplace: work created", { slug, workType });
  return true;
}
