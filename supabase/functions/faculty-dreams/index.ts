import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { createClient } from "npm:@supabase/supabase-js@2.49.8";

import {
  corsHeaders,
  envKeys,
  geminiGenerate,
  jsonResponse,
} from "../_shared/googleVoice.ts";
import {
  embedRetrievalDocument,
  embeddingApiKey,
  GEMINI_EMBEDDING_MODEL,
  vectorLiteral,
} from "../_shared/geminiEmbedding.ts";

type DreamRequest = {
  windowHours?: number;
  limit?: number;
  dryRun?: boolean;
  facultySlug?: string;
  contentSearch?: string;
  geminiModel?: string;
  embedExisting?: boolean;
  embedLimit?: number;
};

type WorkRow = {
  id: string;
  title: string | null;
  abstract: string | null;
  content_md: string | null;
  created_at: string;
  primary_author_id: string | null;
};

type DreamGroup = {
  key: string;
  actorLine: string;
  facultySlug: string;
  facultyName: string;
  dateKey: string;
  authorId: string | null;
  works: WorkRow[];
};

function clampInt(value: unknown, fallback: number, min: number, max: number): number {
  const n = typeof value === "number" ? value : parseInt(String(value ?? ""), 10);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, Math.floor(n)));
}

function truncate(s: string, max: number): string {
  const t = s.trim();
  if (t.length <= max) return t;
  return `${t.slice(0, max)}...`;
}

function slugify(title: string): string {
  const base = title
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 58);
  return `${base}-${Date.now().toString(36)}`;
}

function field(content: string, name: string): string {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const m = content.match(new RegExp(`^\\*\\*${escaped}:\\*\\*\\s*(.+)$`, "im"));
  return m?.[1]?.trim() ?? "";
}

function fencedSection(content: string, heading: string): string {
  const escaped = heading.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const m = content.match(new RegExp(`### ${escaped}\\s+\\\`\\\`\\\`\\n([\\s\\S]*?)\\n\\\`\\\`\\\``, "m"));
  return m?.[1]?.trim() ?? "";
}

function facultySlug(content: string): string {
  return field(content, "Faculty slug").replace(/^`|`$/g, "").trim();
}

function actorLine(content: string): string {
  const account = field(content, "Account");
  if (account) return `**Account:** ${account}`;
  const session = field(content, "Session");
  if (session) return `**Session:** ${session}`;
  return "**Session:** Unsigned Mynah";
}

function actorScope(line: string): { scope: string; email: string | null } {
  const account = line.match(/\*\*Account:\*\*\s*(.+)$/i)?.[1]?.trim();
  if (account) return { scope: `Account:${account}`, email: account };
  return { scope: "Session", email: null };
}

function dateKey(row: WorkRow): string {
  return new Date(row.created_at).toISOString().slice(0, 10);
}

function displayDate(key: string): string {
  return new Date(`${key}T12:00:00Z`).toLocaleDateString("en-US", {
    weekday: "long",
    year: "numeric",
    month: "long",
    day: "numeric",
  });
}

function sourceDigest(row: WorkRow): string {
  const content = row.content_md ?? "";
  const user = fencedSection(content, "User / transcript");
  const assistant = fencedSection(content, "Assistant");
  return [
    `Source ID: ${row.id}`,
    `Title: ${row.title ?? "Mynah conversation"}`,
    `User: ${truncate(user, 2200)}`,
    `Faculty: ${truncate(assistant, 2200)}`,
  ].join("\n");
}

function memorySummary(content: string): string {
  const m = content.match(/### Memory\s+([\s\S]*?)(?:\n### |\n---|$)/);
  return (m?.[1] ?? content).trim();
}

async function embedFacultyMemoryWork(
  db: any,
  work: WorkRow,
  apiKey: string,
): Promise<boolean> {
  if (!apiKey) return false;
  const content = work.content_md ?? "";
  const slug = facultySlug(content);
  if (!slug) return false;
  const actor = actorLine(content);
  const facultyName = field(content, "Faculty") || slug;
  const summary = memorySummary(content);
  const embeddingText = [
    `Faculty slug: ${slug}`,
    `Faculty name: ${facultyName}`,
    `Actor: ${actor}`,
    "",
    summary,
  ].join("\n");
  const embedding = await embedRetrievalDocument(apiKey, embeddingText);
  const scope = actorScope(actor);
  const { error } = await db.from("faculty_memory_embeddings").upsert({
    work_id: work.id,
    actor_scope: scope.scope,
    actor_email: scope.email,
    faculty_slug: slug,
    content_text: embeddingText,
    embedding: vectorLiteral(embedding),
    embedding_model: GEMINI_EMBEDDING_MODEL,
  }, { onConflict: "work_id" });
  if (error) {
    console.warn("faculty-dreams: memory embedding upsert failed", error.message);
    return false;
  }
  return true;
}

async function backfillFacultyMemoryEmbeddings(
  db: any,
  apiKey: string,
  requestedFaculty: string | undefined,
  limit: number,
): Promise<number> {
  if (!apiKey) return 0;
  let query = db
    .from("works")
    .select("id,title,abstract,content_md,created_at,primary_author_id")
    .eq("work_type", "note")
    .ilike("content_md", "%## Faculty memory%")
    .order("created_at", { ascending: false })
    .limit(limit);
  if (requestedFaculty) {
    query = query.ilike("content_md", `%**Faculty slug:** \`${requestedFaculty}\`%`);
  }

  const { data, error } = await query;
  if (error || !data?.length) {
    if (error) console.warn("faculty-dreams: memory backfill query failed", error.message);
    return 0;
  }

  let embedded = 0;
  for (const work of data as WorkRow[]) {
    const existing = await db
      .from("faculty_memory_embeddings")
      .select("id")
      .eq("work_id", work.id)
      .maybeSingle();
    if (existing.data) continue;
    try {
      if (await embedFacultyMemoryWork(db, work, apiKey)) embedded += 1;
    } catch (e) {
      console.warn("faculty-dreams: memory backfill embedding failed", e);
    }
  }
  return embedded;
}

async function sourceAlreadyDreamed(
  db: any,
  sourceId: string,
): Promise<boolean> {
  const { data, error } = await db
    .from("works")
    .select("id")
    .eq("work_type", "note")
    .ilike("content_md", "%## Faculty memory%")
    .ilike("content_md", `%**Dream source ids:**%${sourceId}%`)
    .limit(1);
  if (error) {
    console.warn("faculty-dreams: duplicate check failed", error.message);
    return false;
  }
  return Boolean(data?.length);
}

async function summarizeDream(
  apiKey: string,
  model: string,
  group: DreamGroup,
): Promise<string> {
  const sourceText = group.works.map(sourceDigest).join("\n\n---\n\n");
  if (!apiKey) {
    return truncate(sourceText.replace(/\s+/g, " "), 1200);
  }
  return await geminiGenerate({
    apiKey,
    model,
    systemInstruction:
      "You are the nightly dream process for Castalia faculty memory. " +
      "Turn source conversation transcripts into durable, user-specific memory for the named faculty member. " +
      "Preserve facts, interests, preferences, unresolved questions, commitments, and emotionally salient context. " +
      "Do not overgeneralize from one turn. Write 4-8 concise bullets.",
    userText:
      `Faculty slug: ${group.facultySlug}\n` +
      `Faculty name: ${group.facultyName}\n` +
      `Actor: ${group.actorLine}\n` +
      `Dream date: ${group.dateKey}\n\n` +
      `Source conversations:\n${sourceText}`,
  });
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  let body: DreamRequest = {};
  try {
    body = (await req.json()) as DreamRequest;
  } catch {
    body = {};
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL")?.trim();
  const serviceRole = (
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? Deno.env.get("SERVICE_ROLE_KEY") ?? ""
  ).trim();
  if (!supabaseUrl || !serviceRole) {
    return jsonResponse(500, { error: "SUPABASE_URL or SUPABASE_SERVICE_ROLE_KEY missing" });
  }

  const windowHours = clampInt(body.windowHours, 30, 1, 24 * 14);
  const limit = clampInt(body.limit, 200, 1, 1000);
  const since = new Date(Date.now() - windowHours * 60 * 60 * 1000).toISOString();
  const requestedFaculty = body.facultySlug?.trim();
  const contentSearch = body.contentSearch?.trim();
  const db = createClient(supabaseUrl, serviceRole, {
    auth: { autoRefreshToken: false, persistSession: false },
  });
  const embeddingKey = embeddingApiKey();
  const embedLimit = clampInt(body.embedLimit, 100, 1, 500);
  const backfilled = body.embedExisting
    ? await backfillFacultyMemoryEmbeddings(db, embeddingKey, requestedFaculty, embedLimit)
    : 0;

  let query = db
    .from("works")
    .select("id,title,abstract,content_md,created_at,primary_author_id")
    .eq("work_type", "note")
    .gte("created_at", since)
    .ilike("content_md", "%## Mynah conversation%")
    .ilike("content_md", "%**Route:** `ask-faculty`%")
    .order("created_at", { ascending: true })
    .limit(limit);
  if (contentSearch) {
    query = query.ilike("content_md", `%${contentSearch}%`);
  }
  const { data, error } = await query;
  if (error) return jsonResponse(500, { error: error.message });

  const groups = new Map<string, DreamGroup>();
  for (const row of (data ?? []) as WorkRow[]) {
    const content = row.content_md ?? "";
    const slug = facultySlug(content);
    if (!slug) continue;
    if (requestedFaculty && slug !== requestedFaculty) continue;
    if (await sourceAlreadyDreamed(db, row.id)) continue;

    const facultyName = field(content, "Faculty") || slug;
    const actor = actorLine(content);
    const keyDate = dateKey(row);
    const key = `${keyDate}::${actor}::${slug}`;
    const existing = groups.get(key);
    if (existing) {
      existing.works.push(row);
    } else {
      groups.set(key, {
        key,
        actorLine: actor,
        facultySlug: slug,
        facultyName,
        dateKey: keyDate,
        authorId: row.primary_author_id,
        works: [row],
      });
    }
  }

  const { gemini } = envKeys();
  const geminiModel =
    (body.geminiModel ?? Deno.env.get("GEMINI_MODEL") ?? "gemini-2.5-flash").trim();
  const status = Deno.env.get("MYNAH_COMMONPLACE_STATUS")?.trim() || "draft";
  const visibility = Deno.env.get("MYNAH_COMMONPLACE_VISIBILITY")?.trim() || "private";
  const created: Array<{ title: string; sourceIds: string[]; dryRun: boolean }> = [];
  let embedded = 0;

  for (const group of groups.values()) {
    const summary = await summarizeDream(gemini, geminiModel, group);
    const sourceIds = group.works.map((work) => work.id);
    const dateLabel = displayDate(group.dateKey);
    const title = `${dateLabel}: Faculty dream - ${group.facultyName}`;
    const content = [
      "## Faculty memory",
      "",
      `**When:** ${dateLabel}`,
      "",
      group.actorLine,
      "",
      "**Route:** `ask-faculty`",
      "",
      `**Faculty slug:** \`${group.facultySlug}\``,
      "",
      `**Faculty:** ${group.facultyName}`,
      "",
      `**Dream source ids:** ${sourceIds.join(", ")}`,
      "",
      "### Memory",
      "",
      summary.trim(),
      "",
      "### Dreamed from",
      "",
      group.works.map((work) => `- ${work.title ?? work.id} (${work.id})`).join("\n"),
      "",
      "---",
      "*Generated by Castalia nightly faculty dreams from realtime Commonplace transcripts*",
    ].join("\n");

    if (!body.dryRun) {
      const { data: inserted, error: insertError } = await db.from("works").insert({
          title,
          slug: slugify(title),
          abstract: truncate(summary, 220),
          content_md: content,
          primary_author_id: group.authorId,
          work_type: "note",
          status,
          visibility,
          publication_date: group.dateKey,
        })
        .select("id")
        .single();
      if (insertError) {
        console.warn("faculty-dreams: insert failed", insertError.message);
        continue;
      }
      const workId = inserted?.id;
      if (workId && embeddingKey) {
        try {
          if (
            await embedFacultyMemoryWork(db, {
              id: workId,
              title,
              abstract: truncate(summary, 220),
              content_md: content,
              created_at: new Date().toISOString(),
              primary_author_id: group.authorId,
            }, embeddingKey)
          ) embedded += 1;
        } catch (e) {
          console.warn("faculty-dreams: embedding failed", e);
        }
      }
    }
    created.push({ title, sourceIds, dryRun: Boolean(body.dryRun) });
  }

  return jsonResponse(200, {
    windowHours,
    contentSearch: contentSearch || undefined,
    scanned: data?.length ?? 0,
    groups: groups.size,
    embedded,
    backfilled,
    created,
  });
});
