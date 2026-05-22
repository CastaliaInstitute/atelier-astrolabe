import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import {
  appendMynahCommonplaceEntry,
} from "../_shared/commonplaceDirectus.ts";
import {
  corsHeaders,
  envKeys,
  jsonResponse,
  speechRecognize,
} from "../_shared/googleVoice.ts";

type ReqBody = {
  audioBase64?: string;
  sampleRateHertz?: number;
  languageCode?: string;
  deviceLabel?: string;
};

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  const authHeader = req.headers.get("Authorization") ?? "";
  if (!authHeader.trim()) {
    return jsonResponse(401, { error: "Missing Authorization" });
  }

  let body: ReqBody;
  try {
    body = (await req.json()) as ReqBody;
  } catch {
    return jsonResponse(400, { error: "Invalid JSON body" });
  }

  const audio = (body.audioBase64 ?? "").trim();
  if (!audio) {
    return jsonResponse(400, { error: "audioBase64 is required" });
  }

  const { speech } = envKeys();
  if (!speech) {
    return jsonResponse(500, {
      error: "Server missing GOOGLE_SPEECH_API_KEY or GOOGLE_CLOUD_API_KEY",
    });
  }

  const languageCode = (body.languageCode ?? "en-US").trim() || "en-US";
  const sampleRateHertz = body.sampleRateHertz ?? 16000;
  const deviceLabel = (body.deviceLabel ?? "Astrolabe").trim() || "Astrolabe";

  try {
    const transcript = await speechRecognize(
      speech,
      audio,
      languageCode,
      sampleRateHertz,
    );
    if (!transcript.trim()) {
      return jsonResponse(422, {
        error: "No speech detected",
        transcript: "",
        ok: false,
      });
    }

    await appendMynahCommonplaceEntry(authHeader, {
      kind: "journal",
      transcript: transcript.trim(),
      deviceLabel,
    });

    return jsonResponse(200, {
      ok: true,
      transcript: transcript.trim(),
    });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.error("mynah-pocket-journal:", msg);
    return jsonResponse(502, { error: msg });
  }
});
