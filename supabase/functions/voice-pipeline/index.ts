import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { buildClockAgendaTranscript } from "../_shared/calciferClockBrief.ts";
import {
  buildDailyBriefingTranscript,
  SYSTEM_VOICE_FACE_DAILY_BRIEFING,
} from "../_shared/dailyBriefing.ts";
import {
  capTextForWatchTts,
  corsHeaders,
  envKeys,
  geminiGenerate,
  jsonResponse,
  speechRecognize,
  ttsMp3Base64,
  ttsMp3Bytes,
  watchTtsVoiceSelection,
  watchTtsMaxChars,
  watchTtsMaxCharsDailyBriefing,
} from "../_shared/googleVoice.ts";
import { inferFacultySlug } from "../_shared/facultyBust.ts";
import {
  buildFacultyVoicePrompt,
  normalizeFacultyVoiceSlug,
  resolveFacultyVoiceProfile,
  voiceFromUnknown,
} from "../_shared/facultyVoice.ts";
import {
  SYSTEM_VOICE_FACE_CLOCK_AGENDA,
  VOICE_FACE_ASTRO,
  VOICE_FACE_CLOCK_AGENDA,
  VOICE_FACE_DAILY_BRIEFING,
  VOICE_FACE_SYNASTRY,
} from "../_shared/mynahVoiceFaces.ts";
import {
  matchAskFacultyRoute,
  siblingFunctionUrl,
} from "../_shared/askFacultyRoute.ts";
import { scheduleMynahCommonplaceLog } from "../_shared/commonplaceDirectus.ts";

type ReqBody = {
  audioBase64?: string;
  sampleRateHertz?: number;
  languageCode?: string;
  message?: string;
  systemInstruction?: string;
  skipLlm?: boolean;
  geminiModel?: string;
  /**
   * Pipeline profile (optional). Each face can imply server-side context + default prompts.
   * Example: `clock_agenda` loads Calcifer CalDAV; no `message` / `audioBase64` required.
   */
  face?: string;
  /** Unix seconds for time-aware faces (e.g. clock agenda); defaults to server now. */
  epochSeconds?: number;
  /** Device-built sky facts for `daily_briefing` (astrology, synastry, moon). */
  briefingFacts?: string;
  /** Device local civil hour, 0-23, used to soften evening/night TTS. */
  localHour?: number;
  /** Faculty id/slug whose Google TTS voice should be used for faculty-flavored replies. */
  facultySlug?: string;
  /** Optional display name for the faculty metadata headers / logging fallback. */
  facultyName?: string;
  /**
   * `json` (default): `{ transcript, reply, audioBase64 }`.
   * `mp3`: raw MPEG body (~33% smaller download); text in `X-Voice-*` headers.
   */
  responseFormat?: "json" | "mp3";
};

type AskFacultyResponse = {
  transcript?: string;
  reply?: string;
  audioBase64?: string;
  route?: string;
  facultySlug?: string;
  facultyName?: string;
  facultyBustUrl?: string | null;
  facultyVoice?: {
    ethnicity?: string;
    accent?: string;
    language?: string;
    prompt?: string;
    ttsVoice?: unknown;
  };
};

function commonplaceRoute(face: string, fallback: string): string {
  switch (face.trim().toLowerCase()) {
    case VOICE_FACE_SYNASTRY:
      return "synastry";
    case VOICE_FACE_ASTRO:
    case "astrology":
      return "astrology";
    case VOICE_FACE_CLOCK_AGENDA:
      return "clock_agenda";
    case VOICE_FACE_DAILY_BRIEFING:
      return "daily_briefing";
    default:
      return fallback;
  }
}

function wantsMp3Response(req: Request, body: ReqBody): boolean {
  const fmt = (body.responseFormat ?? "").trim().toLowerCase();
  if (fmt === "mp3") return true;
  const accept = (req.headers.get("Accept") ?? "").toLowerCase();
  return accept.includes("audio/mpeg") || accept.includes("audio/mp3");
}

function headerMetaValue(s: string, maxLen: number): string {
  const t = s.trim();
  if (t.length <= maxLen) return encodeURIComponent(t);
  return encodeURIComponent(t.slice(0, maxLen));
}

function requestLocalHour(body: ReqBody): number | undefined {
  const hour = Number(body.localHour);
  if (!Number.isFinite(hour)) return undefined;
  if (hour < 0 || hour > 23) return undefined;
  return Math.floor(hour);
}

async function voicePipelineOk(
  req: Request,
  body: ReqBody,
  payload: {
    transcript: string;
    reply: string;
    route: string;
    face?: string;
    facultySlug?: string;
    facultyName?: string;
    extraHeaders?: Record<string, string>;
    ttsMaxChars?: number;
  },
): Promise<Response> {
  const { tts } = envKeys();
  if (!tts) {
    return jsonResponse(500, {
      error: "Server missing GOOGLE_TTS_API_KEY or GOOGLE_CLOUD_API_KEY",
    });
  }

  const spoken = capTextForWatchTts(
    payload.reply,
    payload.ttsMaxChars ?? watchTtsMaxChars(),
  );
  if (spoken.length < payload.reply.trim().length) {
    console.log(
      `voice-pipeline: TTS capped ${payload.reply.length} -> ${spoken.length} chars`,
    );
  }

  const authHdr = req.headers.get("Authorization") ?? "";
  scheduleMynahCommonplaceLog(authHdr, {
    kind: "conversation",
    route: payload.route,
    transcript: payload.transcript,
    reply: payload.reply,
  });

  const facultyVoice = await resolveFacultyVoiceProfile(payload.facultySlug);
  const ttsVoice = facultyVoice?.ttsVoice;

  if (wantsMp3Response(req, body)) {
    const localHour = requestLocalHour(body);
    const voice = watchTtsVoiceSelection({ voice: ttsVoice });
    const mp3 = await ttsMp3Bytes(tts, spoken, { localHour, voice: ttsVoice });
    const headers: Record<string, string> = {
      ...corsHeaders,
      "Content-Type": "audio/mpeg",
      "X-Voice-Route": payload.route,
      "X-Voice-Language": voice.languageCode,
      "X-Voice-Name": voice.name,
      "X-Voice-Transcript": headerMetaValue(payload.transcript, 300),
      "X-Voice-Reply": headerMetaValue(payload.reply, 700),
      "X-Voice-Tts-Chars": String(spoken.length),
      ...(payload.facultySlug ? { "X-Faculty-Slug": headerMetaValue(payload.facultySlug, 80) } : {}),
      ...(payload.facultyName ? { "X-Faculty-Name": headerMetaValue(payload.facultyName, 120) } : {}),
      ...payload.extraHeaders,
    };
    if (localHour !== undefined) {
      headers["X-Voice-Local-Hour"] = String(localHour);
    }
    if (payload.face) {
      headers["X-Voice-Face"] = payload.face;
    }
    return new Response(mp3, { status: 200, headers });
  }

  const localHour = requestLocalHour(body);
  const voice = watchTtsVoiceSelection({ voice: ttsVoice });
  const audioBase64 = await ttsMp3Base64(tts, spoken, { localHour, voice: ttsVoice });
  return jsonResponse(
    200,
    {
      transcript: payload.transcript,
      reply: payload.reply,
      audioBase64,
      route: payload.route,
      ...(payload.face ? { face: payload.face } : {}),
      ...(payload.facultySlug ? { facultySlug: payload.facultySlug } : {}),
      ...(payload.facultyName ? { facultyName: payload.facultyName } : {}),
    },
    {
      "x-mynah-route": payload.route,
      "x-voice-language": voice.languageCode,
      "x-voice-name": voice.name,
      ...(payload.face ? { "x-mynah-face": payload.face } : {}),
      ...(payload.facultySlug ? { "x-faculty-slug": headerMetaValue(payload.facultySlug, 80) } : {}),
      ...(payload.facultyName ? { "x-faculty-name": headerMetaValue(payload.facultyName, 120) } : {}),
      ...payload.extraHeaders,
    },
  );
}

async function forwardToAskFaculty(
  req: Request,
  payload: {
    message: string;
    languageCode: string;
    geminiModel: string;
    rawTranscript: string;
    skipLlm: boolean;
    localHour?: number;
    facultySlug?: string;
    facultyName?: string;
    /** Only forwarded when the client set `systemInstruction`; otherwise ask-faculty uses its faculty default. */
    overrideSystemInstruction?: string;
  },
): Promise<Response> {
  const url = siblingFunctionUrl("ask-faculty");
  const auth = req.headers.get("Authorization") ?? "";
  const apikey = req.headers.get("apikey") ?? "";
  const facultySlug =
    normalizeFacultyVoiceSlug(payload.facultySlug) ||
    normalizeFacultyVoiceSlug(inferFacultySlug(payload.message));
  const facultyVoice = await resolveFacultyVoiceProfile(facultySlug);
  const facultyVoicePrompt = buildFacultyVoicePrompt(facultyVoice);
  const body: Record<string, unknown> = {
    message: payload.message,
    languageCode: payload.languageCode,
    geminiModel: payload.geminiModel,
    rawTranscript: payload.rawTranscript,
    skipLlm: payload.skipLlm,
  };
  if (facultySlug) body.facultySlug = facultySlug;
  if (payload.facultyName?.trim()) body.facultyName = payload.facultyName.trim();
  if (facultyVoice?.ethnicity) body.facultyEthnicity = facultyVoice.ethnicity;
  if (facultyVoice?.accent) body.facultyAccent = facultyVoice.accent;
  if (facultyVoice?.language) body.facultyLanguage = facultyVoice.language;
  if (facultyVoicePrompt) body.facultyVoicePrompt = facultyVoicePrompt;
  if (facultyVoice?.ttsVoice) body.facultyTtsVoice = facultyVoice.ttsVoice;
  if (payload.localHour !== undefined) {
    body.localHour = payload.localHour;
  }
  const sys = payload.overrideSystemInstruction?.trim();
  if (sys) body.systemInstruction = sys;

  return await fetch(url, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...(auth ? { Authorization: auth } : {}),
      ...(apikey ? { apikey } : {}),
    },
    body: JSON.stringify(body),
  });
}

function decodeBase64Audio(audioBase64: string): Uint8Array {
  const b64 = audioBase64.trim();
  if (!b64) return new Uint8Array();
  return Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
}

function headerMetaFromUnknown(value: unknown, maxLen: number): string | undefined {
  if (typeof value !== "string") return undefined;
  const trimmed = value.trim();
  return trimmed ? headerMetaValue(trimmed, maxLen) : undefined;
}

async function askFacultyPipelineResponse(
  req: Request,
  body: ReqBody,
  fr: Response,
  face?: string,
): Promise<Response> {
  const text = await fr.text();
  const routeHeaders: Record<string, string> = {
    "x-mynah-route": "ask-faculty",
    ...(face ? { "x-mynah-face": face } : {}),
  };

  let faculty: AskFacultyResponse | undefined;
  try {
    faculty = JSON.parse(text) as AskFacultyResponse;
  } catch {
    // Preserve the upstream body on non-JSON error responses.
  }

  if (!fr.ok || !faculty) {
    return new Response(text, {
      status: fr.status,
      headers: {
        ...corsHeaders,
        "Content-Type": fr.headers.get("Content-Type") ?? "application/json",
        ...routeHeaders,
      },
    });
  }

  const transcript = (faculty.transcript ?? "").trim();
  const reply = (faculty.reply ?? "").trim();
  const audioBase64 = (faculty.audioBase64 ?? "").trim();
  const upstreamVoice = voiceFromUnknown(faculty.facultyVoice?.ttsVoice);

  if (wantsMp3Response(req, body) && audioBase64) {
    const mp3 = decodeBase64Audio(audioBase64);
    if (mp3.length > 0) {
      const headers: Record<string, string> = {
        ...corsHeaders,
        "Content-Type": "audio/mpeg",
        "X-Voice-Route": "ask-faculty",
        "X-Voice-Tts-Source": "ask-faculty",
        "X-Voice-Tts-Chars": String(reply.length),
        ...(upstreamVoice
          ? {
            "X-Voice-Language": upstreamVoice.languageCode,
            "X-Voice-Name": upstreamVoice.name,
          }
          : {}),
        ...routeHeaders,
      };
      const localHour = requestLocalHour(body);
      if (localHour !== undefined) {
        headers["X-Voice-Local-Hour"] = String(localHour);
      }
      const transcriptHeader = headerMetaFromUnknown(transcript, 300);
      if (transcriptHeader) headers["X-Voice-Transcript"] = transcriptHeader;
      const replyHeader = headerMetaFromUnknown(reply, 700);
      if (replyHeader) headers["X-Voice-Reply"] = replyHeader;
      const slugHeader = headerMetaFromUnknown(faculty.facultySlug, 80);
      if (slugHeader) headers["X-Faculty-Slug"] = slugHeader;
      const nameHeader = headerMetaFromUnknown(faculty.facultyName, 120);
      if (nameHeader) headers["X-Faculty-Name"] = nameHeader;
      const accentHeader = headerMetaFromUnknown(faculty.facultyVoice?.accent, 140);
      if (accentHeader) headers["X-Faculty-Accent"] = accentHeader;
      const languageHeader = headerMetaFromUnknown(faculty.facultyVoice?.language, 140);
      if (languageHeader) headers["X-Faculty-Language"] = languageHeader;
      if (face) headers["X-Voice-Face"] = face;

      return new Response(mp3, { status: 200, headers });
    }
  }

  if (wantsMp3Response(req, body) && reply) {
    return await voicePipelineOk(req, body, {
      transcript,
      reply,
      route: "ask-faculty",
      face,
      facultySlug: faculty.facultySlug,
      facultyName: faculty.facultyName,
      extraHeaders: {
        ...routeHeaders,
        "X-Voice-Tts-Source": "voice-pipeline-fallback",
      },
    });
  }

  return new Response(JSON.stringify(faculty), {
    status: fr.status,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json",
      ...routeHeaders,
    },
  });
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse(405, { error: "Method not allowed" });
  }

  let body: ReqBody;
  try {
    body = (await req.json()) as ReqBody;
  } catch {
    return jsonResponse(400, { error: "Invalid JSON body" });
  }

  const { speech, gemini, tts } = envKeys();
  const languageCode = (body.languageCode ?? "en-US").trim() || "en-US";
  const sampleRateHertz = body.sampleRateHertz ?? 16000;
  const geminiModel =
    (body.geminiModel ?? Deno.env.get("GEMINI_MODEL") ?? "gemini-2.5-flash")
      .trim();

  const clientSystem = (body.systemInstruction ?? "").trim();
  const defaultSystem =
    Deno.env.get("GEMINI_SYSTEM_INSTRUCTION")?.trim() ||
    "You are Mynah, a concise, friendly bedside assistant. Answer clearly in one or two short paragraphs unless the user asks for detail. Do not invent device actions you cannot perform.";

  const systemInstruction = clientSystem || defaultSystem;

  const face = (body.face ?? "").trim().toLowerCase();

  const audio = (body.audioBase64 ?? "").trim();
  const message = (body.message ?? "").trim();

  try {
    if (face === VOICE_FACE_CLOCK_AGENDA) {
      const epochSeconds =
        typeof body.epochSeconds === "number" && Number.isFinite(body.epochSeconds)
          ? Math.floor(body.epochSeconds)
          : Math.floor(Date.now() / 1000);

      const transcript = await buildClockAgendaTranscript(epochSeconds);

      const route = matchAskFacultyRoute(transcript);
      if (route.kind === "ask-faculty") {
        const fr = await forwardToAskFaculty(req, {
          message: route.facultyMessage,
          languageCode,
          geminiModel,
          rawTranscript: transcript,
          skipLlm: body.skipLlm ?? false,
          localHour: requestLocalHour(body),
          overrideSystemInstruction: clientSystem || undefined,
        });
        return await askFacultyPipelineResponse(
          req,
          body,
          fr,
          VOICE_FACE_CLOCK_AGENDA,
        );
      }

      const skipLlm = body.skipLlm !== false;
      let reply: string;
      if (skipLlm || !gemini) {
        reply = transcript;
      } else {
        const sys = clientSystem || SYSTEM_VOICE_FACE_CLOCK_AGENDA;
        reply = await geminiGenerate({
          apiKey: gemini,
          model: geminiModel,
          systemInstruction: sys,
          userText: transcript,
        });
      }

      return await voicePipelineOk(req, body, {
        transcript,
        reply,
        route: "voice-pipeline",
        face: VOICE_FACE_CLOCK_AGENDA,
        extraHeaders: {
          "x-mynah-route": "voice-pipeline",
          "x-mynah-face": VOICE_FACE_CLOCK_AGENDA,
        },
      });
    }

    if (face === VOICE_FACE_DAILY_BRIEFING) {
      const epochSeconds =
        typeof body.epochSeconds === "number" && Number.isFinite(body.epochSeconds)
          ? Math.floor(body.epochSeconds)
          : Math.floor(Date.now() / 1000);
      const deviceFacts = (body.briefingFacts ?? "").trim();
      const transcript = await buildDailyBriefingTranscript({
        epochSeconds,
        deviceFacts,
      });
      const userText =
        (message ||
          "Deliver today's Mynah Astrolabe daily briefing now from the facts. " +
            "Speak for about three to five minutes in one flowing narrative.").trim();
      const sys = clientSystem || SYSTEM_VOICE_FACE_DAILY_BRIEFING;
      const reply = gemini
        ? await geminiGenerate({
            apiKey: gemini,
            model: geminiModel,
            systemInstruction: sys,
            userText: `${userText}\n\n${transcript}`,
          })
        : transcript;
      return await voicePipelineOk(req, body, {
        transcript,
        reply,
        route: "voice-pipeline",
        face: VOICE_FACE_DAILY_BRIEFING,
        ttsMaxChars: watchTtsMaxCharsDailyBriefing(),
        extraHeaders: {
          "x-mynah-route": "voice-pipeline",
          "x-mynah-face": VOICE_FACE_DAILY_BRIEFING,
        },
      });
    }

    let transcript = "";

    if (audio) {
      if (!speech) {
        return jsonResponse(500, {
          error: "Server missing GOOGLE_SPEECH_API_KEY or GOOGLE_CLOUD_API_KEY",
        });
      }
      transcript = await speechRecognize(
        speech,
        audio,
        languageCode,
        sampleRateHertz,
      );
      if (!transcript) {
        return jsonResponse(422, {
          error: "No speech detected",
          transcript: "",
          reply: "",
          audioBase64: "",
        });
      }
    } else if (message) {
      transcript = message;
    } else {
      return jsonResponse(400, {
        error:
          "Provide audioBase64, message, or face (e.g. clock_agenda, daily_briefing with optional epochSeconds)",
      });
    }

    const route = matchAskFacultyRoute(transcript);
    if (route.kind === "ask-faculty") {
      const fr = await forwardToAskFaculty(req, {
        message: route.facultyMessage,
        languageCode,
        geminiModel,
        rawTranscript: transcript,
        skipLlm: body.skipLlm ?? false,
        localHour: requestLocalHour(body),
        overrideSystemInstruction: clientSystem || undefined,
      });
      return await askFacultyPipelineResponse(req, body, fr, face || undefined);
    }

    let reply: string;
    if (body.skipLlm) {
      reply = transcript;
    } else {
      if (!gemini) {
        return jsonResponse(500, {
          error:
            "Server missing GOOGLE_GEMINI_API_KEY, GOOGLE_AI_API_KEY, or GOOGLE_CLOUD_API_KEY",
        });
      }
      reply = await geminiGenerate({
        apiKey: gemini,
        model: geminiModel,
        systemInstruction,
        userText: transcript,
      });
    }

    return await voicePipelineOk(req, body, {
      transcript,
      reply,
      route: commonplaceRoute(face, "voice-pipeline"),
      face: face || undefined,
      facultySlug: body.facultySlug,
      facultyName: body.facultyName,
      extraHeaders: {
        "x-mynah-route": commonplaceRoute(face, "voice-pipeline"),
        ...(face ? { "x-mynah-face": face } : {}),
      },
    });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.error("voice-pipeline error:", msg);
    return jsonResponse(500, { error: msg });
  }
});
