/**
 * Append Mynah conversation / artifact entries to Commonplace (Directus).
 * Set DIRECTUS_URL to enable logging to Commonplace (Directus).
 */

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
    const title = `${dateStr}: Mynah (${route})`;
    let content = `## Mynah conversation\n\n`;
    content += `**When:** ${dateStr}\n\n`;
    content += `${actorLine}\n\n`;
    content += `**Route:** \`${route}\`\n\n`;
    if (fac) content += `**Faculty slug:** \`${fac}\`\n\n`;
    content += `### User / transcript\n\n${fenceBlock(ts)}\n\n`;
    content += `### Assistant\n\n${fenceBlock(rep)}\n\n`;
    content += `---\n*Logged from Mynah → Commonplace (${route})*\n`;
    const abstract = truncate(`${route}: ${ts}`, 220);
    return { title, content, abstract, workType: "mynah_conversation" };
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

export async function appendMynahCommonplaceEntry(
  authHeader: string,
  payload: MynahCommonplacePayload,
): Promise<void> {
  if (Deno.env.get("MYNAH_COMMONPLACE_DISABLED") === "true") return;

  const DIRECTUS_URL = (Deno.env.get("DIRECTUS_URL") ?? "").trim().replace(/\/+$/, "");
  if (!DIRECTUS_URL) {
    console.warn("mynah commonplace: DIRECTUS_URL not set; skip log");
    return;
  }
  const DIRECTUS_TOKEN = Deno.env.get("DIRECTUS_STATIC_TOKEN")?.trim();
  if (!DIRECTUS_TOKEN) {
    console.warn("mynah commonplace: DIRECTUS_STATIC_TOKEN not set; skip log");
    return;
  }

  const authorSlug = (Deno.env.get("MYNAH_COMMONPLACE_AUTHOR_SLUG") ?? "custodian").trim();
  const SUPABASE_URL = Deno.env.get("SUPABASE_URL")?.trim();
  const SUPABASE_ANON_KEY = Deno.env.get("SUPABASE_ANON_KEY")?.trim();

  const actor = await resolveActor(authHeader, SUPABASE_URL, SUPABASE_ANON_KEY);
  const author = await fetchPersonBySlug(DIRECTUS_URL, DIRECTUS_TOKEN, authorSlug);
  if (!author) {
    console.warn(`mynah commonplace: author slug not found: ${authorSlug}`);
    return;
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
    work_type: workType,
    status,
    visibility,
    publication_date: new Date().toISOString().split("T")[0],
  });

  console.log("mynah commonplace: work created", { slug, workType });
}
