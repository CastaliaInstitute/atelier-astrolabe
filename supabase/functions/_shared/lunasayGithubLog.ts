type LunaSayGithubEntry = {
  mode: "journal" | "conversation";
  transcript: string;
  reply?: string;
  face?: string;
  route?: string;
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

/** Append one completed LunaSay turn to a private, server-configured GitHub repo. */
export async function appendLunaSayGithubLog(entry: LunaSayGithubEntry): Promise<boolean> {
  const repo = (Deno.env.get("LUNASAY_GITHUB_REPO") ?? "").trim();
  const token = (Deno.env.get("LUNASAY_GITHUB_TOKEN") ??
    Deno.env.get("FAMILY_RHYTHM_GITHUB_TOKEN") ?? "").trim();
  if (!repo || !token || !/^[\w.-]+\/[\w.-]+$/.test(repo)) return false;

  const branch = (Deno.env.get("LUNASAY_GITHUB_BRANCH") ?? "main").trim() || "main";
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
      console.error("LunaSay GitHub daily log exceeds safe Contents API size", path);
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
