type LunaSayGithubEntry = {
  mode: "journal" | "conversation";
  transcript: string;
  reply?: string;
  face?: string;
  route?: string;
};

export type LunaSayMlEvent = {
  event: "mood_checkin" | "reading_opened" | "reading_feedback";
  subjectId: string;
  consentVersion: string;
  occurredAt?: string;
  data: Record<string, string | number | boolean | null>;
};

export type LunaSayMlRecord = {
  schemaVersion: 1;
  event: LunaSayMlEvent["event"];
  occurredAt: string;
  subjectHash: string;
  consentVersion: string;
  data: Record<string, string | number | boolean | null>;
};

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  }
  return btoa(binary);
}

function base64ToText(value: string): string {
  const binary = atob(value.replace(/\s/g, ""));
  const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
  return new TextDecoder().decode(bytes);
}

function githubHeaders(token: string): HeadersInit {
  return {
    Authorization: `Bearer ${token}`,
    Accept: "application/vnd.github+json",
    "Content-Type": "application/json",
    "User-Agent": "Castalia-LunaSay-Session/1.0",
    "X-GitHub-Api-Version": "2022-11-28",
  };
}

function safeMlData(
  event: LunaSayMlEvent["event"],
  value: Record<string, string | number | boolean | null>,
): Record<string, string | number | boolean | null> {
  const allowed = new Set(
    event === "mood_checkin"
      ? ["mood", "arousal", "valence", "source"]
      : event === "reading_feedback"
      ? ["face", "rating", "reading_date", "source"]
      : ["face", "reading_date", "source", "dwell_bucket"],
  );
  const safe: Record<string, string | number | boolean | null> = {};
  for (const [key, item] of Object.entries(value).slice(0, 24)) {
    if (!allowed.has(key) || !/^[a-z][a-z0-9_]{0,31}$/.test(key)) continue;
    if (typeof item === "string") {
      safe[key] = item.replace(/\s+/g, " ").trim().slice(0, 96);
    } else if (
      item === null || (typeof item === "number" && Number.isFinite(item)) ||
      typeof item === "boolean"
    ) {
      safe[key] = item;
    }
  }
  return safe;
}

/** Build a pseudonymous, bounded record suitable for later consented ML work. */
export async function createLunaSayMlRecord(
  entry: LunaSayMlEvent,
  salt: string,
): Promise<LunaSayMlRecord> {
  const subject = entry.subjectId.trim();
  const consentVersion = entry.consentVersion.trim();
  if (
    !subject || subject.length > 160 || !consentVersion ||
    consentVersion.length > 48 || !salt
  ) {
    throw new Error("Invalid LunaSay ML consent envelope");
  }
  const occurredAt = entry.occurredAt
    ? new Date(entry.occurredAt).toISOString()
    : new Date().toISOString();
  const digest = await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(`${salt}\u0000${subject}`),
  );
  const subjectHash = [...new Uint8Array(digest)]
    .map((value) => value.toString(16).padStart(2, "0"))
    .join("");
  return {
    schemaVersion: 1,
    event: entry.event,
    occurredAt,
    subjectHash,
    consentVersion,
    data: safeMlData(entry.event, entry.data),
  };
}

/** Append one completed LunaSay turn to a private, server-configured GitHub repo. */
export async function appendLunaSayGithubLog(
  entry: LunaSayGithubEntry,
): Promise<boolean> {
  const repo = (Deno.env.get("LUNASAY_GITHUB_REPO") ?? "").trim();
  const token = (Deno.env.get("LUNASAY_GITHUB_TOKEN") ??
    Deno.env.get("FAMILY_RHYTHM_GITHUB_TOKEN") ?? "").trim();
  if (!repo || !token || !/^[\w.-]+\/[\w.-]+$/.test(repo)) return false;

  const branch = (Deno.env.get("LUNASAY_GITHUB_BRANCH") ?? "main").trim() ||
    "main";
  const prefix = (Deno.env.get("LUNASAY_GITHUB_PATH") ?? "lunasay-sessions")
    .trim().replace(/^\/+|\/+$/g, "") || "lunasay-sessions";
  const now = new Date();
  const day = now.toISOString().slice(0, 10);
  const month = day.slice(0, 7);
  const path = `${prefix}/${entry.mode}/${month}/${day}.md`;
  const encodedPath = path.split("/").map(encodeURIComponent).join("/");
  const url = `https://api.github.com/repos/${repo}/contents/${encodedPath}`;
  const block = [
    `\n## ${now.toISOString()} · ${entry.face || entry.mode}`,
    entry.route ? `\nRoute: \`${entry.route}\`` : "",
    `\n### User\n\n${entry.transcript.trim()}`,
    entry.mode === "conversation" && entry.reply?.trim()
      ? `\n### LunaSay\n\n${entry.reply.trim()}`
      : "",
    "\n",
  ].join("");

  for (let attempt = 0; attempt < 4; attempt++) {
    let existing = "# LunaSay sessions\n";
    let sha: string | undefined;
    const current = await fetch(`${url}?ref=${encodeURIComponent(branch)}`, {
      headers: githubHeaders(token),
    });
    if (current.ok) {
      const json = await current.json() as { content?: string; sha?: string };
      if (json.content) existing = base64ToText(json.content);
      sha = json.sha;
    } else if (current.status !== 404) {
      console.error("LunaSay GitHub read failed", current.status, path);
      return false;
    }

    const next = `${existing.replace(/\s*$/, "\n")}${block}`;
    if (new TextEncoder().encode(next).byteLength > 900 * 1024) {
      console.error(
        "LunaSay GitHub daily log exceeds safe Contents API size",
        path,
      );
      return false;
    }
    const payload: Record<string, unknown> = {
      message: `Append LunaSay ${entry.mode} turn for ${day}`,
      branch,
      content: bytesToBase64(new TextEncoder().encode(next)),
    };
    if (sha) payload.sha = sha;
    const saved = await fetch(url, {
      method: "PUT",
      headers: githubHeaders(token),
      body: JSON.stringify(payload),
    });
    if (saved.ok) return true;
    if (saved.status !== 409 && saved.status !== 422) {
      console.error("LunaSay GitHub write failed", saved.status, path);
      return false;
    }
    await new Promise((resolve) => setTimeout(resolve, 150 * (attempt + 1)));
  }
  return false;
}

/**
 * Append a machine-readable event only when the operator has explicitly
 * enabled research logging. The user id is salted and hashed before GitHub.
 */
export async function appendLunaSayMlEvent(
  entry: LunaSayMlEvent,
): Promise<boolean> {
  if (
    (Deno.env.get("LUNASAY_ML_LOGGING_ENABLED") ?? "").toLowerCase() !== "true"
  ) {
    return false;
  }
  const repo = (Deno.env.get("LUNASAY_GITHUB_REPO") ?? "").trim();
  const token = (Deno.env.get("LUNASAY_GITHUB_TOKEN") ??
    Deno.env.get("FAMILY_RHYTHM_GITHUB_TOKEN") ?? "").trim();
  const salt = (Deno.env.get("LUNASAY_ML_SUBJECT_SALT") ?? "").trim();
  if (!repo || !token || !salt || !/^[\w.-]+\/[\w.-]+$/.test(repo)) {
    return false;
  }

  const record = await createLunaSayMlRecord(entry, salt);
  const branch = (Deno.env.get("LUNASAY_GITHUB_BRANCH") ?? "main").trim() ||
    "main";
  const prefix = (Deno.env.get("LUNASAY_GITHUB_PATH") ?? "lunasay-sessions")
    .trim().replace(/^\/+|\/+$/g, "") || "lunasay-sessions";
  const day = record.occurredAt.slice(0, 10);
  const month = day.slice(0, 7);
  const path = `${prefix}/ml-events/${month}/${day}.jsonl`;
  const encodedPath = path.split("/").map(encodeURIComponent).join("/");
  const url = `https://api.github.com/repos/${repo}/contents/${encodedPath}`;
  const line = `${JSON.stringify(record)}\n`;

  for (let attempt = 0; attempt < 4; attempt++) {
    let existing = "";
    let sha: string | undefined;
    const current = await fetch(`${url}?ref=${encodeURIComponent(branch)}`, {
      headers: githubHeaders(token),
    });
    if (current.ok) {
      const json = await current.json() as { content?: string; sha?: string };
      if (json.content) existing = base64ToText(json.content);
      sha = json.sha;
    } else if (current.status !== 404) {
      console.error("LunaSay ML GitHub read failed", current.status, path);
      return false;
    }
    const next = `${existing}${line}`;
    if (new TextEncoder().encode(next).byteLength > 900 * 1024) return false;
    const payload: Record<string, unknown> = {
      message: `Append consented LunaSay event for ${day}`,
      branch,
      content: bytesToBase64(new TextEncoder().encode(next)),
    };
    if (sha) payload.sha = sha;
    const saved = await fetch(url, {
      method: "PUT",
      headers: githubHeaders(token),
      body: JSON.stringify(payload),
    });
    if (saved.ok) return true;
    if (saved.status !== 409 && saved.status !== 422) return false;
    await new Promise((resolve) => setTimeout(resolve, 150 * (attempt + 1)));
  }
  return false;
}

export function scheduleLunaSayGithubLog(entry: LunaSayGithubEntry): void {
  const task = appendLunaSayGithubLog(entry).catch((error) => {
    console.error("LunaSay GitHub append error", error);
    return false;
  });
  const runtime = globalThis as typeof globalThis & {
    EdgeRuntime?: { waitUntil(promise: Promise<unknown>): void };
  };
  if (runtime.EdgeRuntime?.waitUntil) runtime.EdgeRuntime.waitUntil(task);
}
