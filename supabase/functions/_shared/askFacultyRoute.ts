export type AskFacultyRoute =
  | { kind: "none" }
  | { kind: "ask-faculty"; facultyMessage: string; selectFaculty?: boolean };

/**
 * Detects faculty-directed asks after STT / typed input so `voice-pipeline` can forward to `ask-faculty`.
 *
 * Routing rules (first match wins):
 * 1. Phrases starting with **ask faculty** or **ask the faculty** → remainder is the faculty prompt.
 * 2. Optional env **FACULTY_ASK_TRIGGERS**: comma-separated prefixes (case-insensitive) that must match the text after `ask `.
 * 3. **ask &lt;Name&gt;…** when the first token looks like a proper name (capitalized Latin letters).
 * 4. If **ASK_FACULTY_ROUTE_ANY=1**, any **ask …** that is not blocked by stop-words routes.
 *
 * Blocklist trims generic asks: `ask me`, `ask about …`, `ask what/when/…`, etc.
 */
export function matchAskFacultyRoute(transcript: string): AskFacultyRoute {
  const t = transcript.trim();
  if (!t) return { kind: "none" };

  const explicit =
    /^\s*ask\s+the\s+faculty\b/i.exec(t) ?? /^\s*ask\s+faculty\b/i.exec(t);
  if (explicit) {
    const rest = t.slice(explicit[0].length).replace(/^[\s,:.-]+/, "").trim();
    return { kind: "ask-faculty", facultyMessage: rest || t, selectFaculty: true };
  }

  const mAsk = /^\s*ask\s+(.+)$/i.exec(t);
  if (!mAsk) return { kind: "none" };
  const rest = mAsk[1].trim();
  if (!rest) return { kind: "none" };

  const restLower = rest.toLowerCase();

  const stopPrefixes =
    /^(me\b|you\b|us\b|my\b|your\b|about\b|for\s+help\b|for\s+a\b|for\s+the\b|what\b|when\b|where\b|why\b|how\b|if\b|whether\b|to\b|google\b|alexa\b|siri\b)/i;
  if (stopPrefixes.test(rest)) return { kind: "none" };

  const triggers = (Deno.env.get("FACULTY_ASK_TRIGGERS") ?? "")
    .split(",")
    .map((s) => s.trim().toLowerCase())
    .filter(Boolean);
  for (const tr of triggers) {
    if (
      restLower === tr ||
      restLower.startsWith(tr + " ") ||
      restLower.startsWith(tr + ",")
    ) {
      return { kind: "ask-faculty", facultyMessage: rest };
    }
  }

  const firstWord = rest.split(/\s+/)[0] ?? "";
  const looksLikeProperName =
    /^[A-Z][a-zA-Z.'-]+$/.test(firstWord) &&
    !/^(What|When|Where|Why|How|Can|Could|Would|Should|Is|Are|Was|Were|Do|Does|Did|Will|Please|The|A|An|This|That|Someone|Everybody|Everyone)\b/
      .test(firstWord);

  if (looksLikeProperName) {
    return { kind: "ask-faculty", facultyMessage: rest };
  }

  if (Deno.env.get("ASK_FACULTY_ROUTE_ANY") === "1") {
    return { kind: "ask-faculty", facultyMessage: rest };
  }

  return { kind: "none" };
}

/** Builds absolute URL for another Edge Function on the same project. */
export function siblingFunctionUrl(functionName: string): string {
  const base = Deno.env.get("SUPABASE_URL")?.trim().replace(/\/$/, "") ?? "";
  if (!base) {
    throw new Error(
      "SUPABASE_URL is not set; cannot route between Edge Functions.",
    );
  }
  return `${base}/functions/v1/${functionName}`;
}
