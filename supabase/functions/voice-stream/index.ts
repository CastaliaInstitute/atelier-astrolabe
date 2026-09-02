import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { isDedicatedFacultyFace } from "../_shared/askFacultyRoute.ts";
import { verifyAstrolabeDevice } from "../_shared/deviceAuth.ts";
import { GeminiLiveTranscriber } from "../_shared/geminiLiveTranscriber.ts";
import { envKeys } from "../_shared/googleVoice.ts";

type SessionState = {
  face?: string;
  facultySlug?: string;
  facultyName?: string;
  systemInstruction?: string;
  conversationHistory?: string;
  interactionMode: "conversation" | "transcribe" | "journal";
  commonplaceMode: "off" | "conversation" | "journal";
  languageCode: string;
  sampleRateHertz: number;
  responseFormat: "json" | "mp3";
  skipLlm: boolean;
  logToCommonplace?: boolean;
  respondent?: "daniel" | "camille";
  mode?: "therapy" | "editor";
  topic?: string;
  workSlug?: string;
  sessionId?: string;
  syntheticValidation?: boolean;
};

const MAX_BUFFER_BYTES = 1024 * 1024;
const ASTROLABE_BINARY_PCM = 0xa1;
const VOICE_STREAM_CT = "application/vnd.astrolabe.voice-stream";
const AUDIO_DELTA_CHARS = 12 * 1024;

type VoicePipelineResponse = {
  transcript?: string;
  reply?: string;
  audioBase64?: string;
  audioChunksBase64?: string[];
  facultySlug?: string;
  facultyName?: string;
  askFacultyRoute?: boolean;
};

type AskFacultyResponse = {
  transcript?: string;
  reply?: string;
  audioBase64?: string;
  facultySlug?: string;
  facultyName?: string;
};

function send(socket: WebSocket, payload: unknown) {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(payload));
  }
}

async function sendAudioDelta(
  socket: WebSocket,
  turnId: string,
  audioBase64: string,
  encoding = "mp3",
  segmentIndex?: number,
  segmentCount?: number,
) {
  for (
    let offset = 0;
    offset < audioBase64.length;
    offset += AUDIO_DELTA_CHARS
  ) {
    const end = Math.min(offset + AUDIO_DELTA_CHARS, audioBase64.length);
    send(socket, {
      type: "response.audio.delta",
      turnId,
      audio: audioBase64.slice(offset, end),
      encoding,
      ...(segmentIndex === undefined ? {} : {
        segmentIndex,
        segmentCount,
        segmentStart: offset === 0,
        segmentEnd: end === audioBase64.length,
      }),
    });
    if (offset + AUDIO_DELTA_CHARS < audioBase64.length) {
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
  }
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

function concatChunks(chunks: Uint8Array[], totalBytes: number): Uint8Array {
  const out = new Uint8Array(totalBytes);
  let offset = 0;
  for (const chunk of chunks) {
    out.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return out;
}

function encodeVoicePipelineStreamBody(
  session: SessionState,
  pcm: Uint8Array,
  final: boolean,
  overrides?: Partial<Record<string, unknown>>,
): Uint8Array {
  const metadata: Record<string, unknown> = {
    languageCode: session.languageCode,
    sampleRateHertz: session.sampleRateHertz,
    responseFormat: final ? session.responseFormat : "json",
    skipLlm: final ? session.skipLlm : true,
    interactionMode: final ? session.interactionMode : "transcribe",
    commonplaceMode: final ? session.commonplaceMode : "off",
    earlyRoute: !final,
  };
  if (typeof session.logToCommonplace === "boolean") {
    metadata.logToCommonplace = session.logToCommonplace;
  }
  if (session.face) metadata.face = session.face;
  if (session.facultySlug) metadata.facultySlug = session.facultySlug;
  if (session.facultyName) metadata.facultyName = session.facultyName;
  if (session.systemInstruction) {
    metadata.systemInstruction = session.systemInstruction;
  }
  if (session.conversationHistory) {
    metadata.conversationHistory = session.conversationHistory;
  }
  if (session.respondent) metadata.respondent = session.respondent;
  if (session.mode) metadata.mode = session.mode;
  if (session.topic) metadata.topic = session.topic;
  if (session.workSlug) metadata.workSlug = session.workSlug;
  if (session.sessionId) metadata.sessionId = session.sessionId;
  if (typeof session.syntheticValidation === "boolean") {
    metadata.syntheticValidation = session.syntheticValidation;
  }
  if (overrides) {
    for (const [key, value] of Object.entries(overrides)) {
      if (value === undefined) continue;
      metadata[key] = value;
    }
  }

  const json = new TextEncoder().encode(JSON.stringify(metadata));
  const out = new Uint8Array(4 + json.byteLength + pcm.byteLength);
  new DataView(out.buffer).setUint32(0, json.byteLength, true);
  out.set(json, 4);
  out.set(pcm, 4 + json.byteLength);
  return out;
}

function siblingVoicePipelineUrl(req: Request): string {
  const base = Deno.env.get("SUPABASE_URL")?.trim();
  if (base) return `${base.replace(/\/+$/, "")}/functions/v1/voice-pipeline`;
  const url = new URL(req.url);
  return `${url.origin}/functions/v1/voice-pipeline`;
}

function siblingAskFacultyUrl(req: Request): string {
  const base = Deno.env.get("SUPABASE_URL")?.trim();
  if (base) {
    return `${base.replace(/\/+$/, "")}/functions/v1/ask-faculty-voice`;
  }
  const url = new URL(req.url);
  return `${url.origin}/functions/v1/ask-faculty-voice`;
}

function authHeaders(req: Request): HeadersInit {
  const headers: Record<string, string> = {
    "Content-Type": VOICE_STREAM_CT,
    "Accept": "application/json",
  };
  const auth = req.headers.get("Authorization");
  const apikey = req.headers.get("apikey");
  if (auth) headers.Authorization = auth;
  if (apikey) headers.apikey = apikey;
  for (
    const name of [
      "X-Astrolabe-Device-Mac",
      "X-Astrolabe-Device-Nonce",
      "X-Astrolabe-Device-Signature",
      "X-Astrolabe-Device-Channel",
    ]
  ) {
    const value = req.headers.get(name);
    if (value) headers[name] = value;
  }
  return headers;
}

function decodedHeader(res: Response, name: string): string | undefined {
  const raw = res.headers.get(name)?.trim();
  if (!raw) return undefined;
  try {
    return decodeURIComponent(raw);
  } catch {
    return raw;
  }
}

function decodeAudioAppend(value: unknown): Uint8Array | null {
  if (typeof value !== "string" || !value.trim()) return null;
  const raw = atob(value.trim());
  const out = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) out[i] = raw.charCodeAt(i);
  return out;
}

function decodeBinaryFrame(data: ArrayBuffer): Uint8Array | null {
  const bytes = new Uint8Array(data);
  if (bytes.byteLength === 0) return null;
  if (bytes[0] !== ASTROLABE_BINARY_PCM) return bytes;
  if (bytes.byteLength <= 9) return new Uint8Array();
  return bytes.subarray(9);
}

async function jsonFetch<T>(
  url: string,
  init: RequestInit,
): Promise<
  { ok: true; json: T } | { ok: false; status: number; message: string }
> {
  const res = await fetch(url, init);
  const text = await res.text();
  if (!res.ok) {
    return {
      ok: false,
      status: res.status,
      message: text || `HTTP ${res.status}`,
    };
  }
  try {
    return { ok: true, json: JSON.parse(text) as T };
  } catch {
    return {
      ok: false,
      status: 502,
      message: "Invalid JSON response from voice service",
    };
  }
}

Deno.serve(async (req) => {
  const upgrade = req.headers.get("upgrade") ?? "";
  if (upgrade.toLowerCase() !== "websocket") {
    return new Response("voice-stream expects a WebSocket upgrade", {
      status: 400,
    });
  }

  const deviceAuthError = await verifyAstrolabeDevice(req);
  if (deviceAuthError) return deviceAuthError;

  const { socket, response } = Deno.upgradeWebSocket(req);
  const session: SessionState = {
    languageCode: "en-US",
    sampleRateHertz: 16000,
    interactionMode: "conversation",
    commonplaceMode: "conversation",
    responseFormat: "json",
    skipLlm: false,
  };
  const chunks: Uint8Array[] = [];
  let totalBytes = 0;
  const pendingTurnChunks: Uint8Array[] = [];
  let pendingTurnBytes = 0;
  let turnCounter = 0;
  let frameCounter = 0;
  const voicePipelineUrl = siblingVoicePipelineUrl(req);
  const askFacultyUrl = siblingAskFacultyUrl(req);
  const headers = authHeaders(req);
  const liveApiKey = envKeys().gemini;
  let liveTranscriber: GeminiLiveTranscriber | undefined;
  let liveAudioChain = Promise.resolve();
  let liveFailed = false;
  let liveTurnId = "live-1";

  function theritorLiveEnabled(): boolean {
    return session.face?.trim().toLowerCase() === "theritor" && !!liveApiKey;
  }

  function ensureLiveTranscriber(): GeminiLiveTranscriber | undefined {
    if (!theritorLiveEnabled()) return undefined;
    if (!liveTranscriber) {
      liveTranscriber = new GeminiLiveTranscriber({
        apiKey: liveApiKey,
        languageCode: session.languageCode,
        model: Deno.env.get("GEMINI_LIVE_TRANSCRIBE_MODEL")?.trim() ||
          "gemini-3.5-transcribe-live",
        vocabulary: [
          "Daniel",
          "Daniel McShan",
          "Camille",
          "Daniel and Camille",
          "AtelierNymphet",
          "Atelier Nymphet",
          "La Recherche",
          "La Recherche story",
          "Theritor",
          "therapist-editor",
          "Paris",
        ],
        onInterim: (text) =>
          send(socket, {
            type: "conversation.item.input_audio_transcription.delta",
            turnId: liveTurnId,
            delta: text,
          }),
        onFinal: (text) =>
          send(socket, {
            type: "conversation.item.input_audio_transcription.completed",
            turnId: liveTurnId,
            transcript: text,
          }),
        onDiagnostic: (message) => {
          console.log("voice-stream Gemini Live", message);
          if (Deno.env.get("VOICE_STREAM_DIAGNOSTICS") === "true") {
            send(socket, {
              type: `transcription.diagnostic.${
                message.replaceAll(
                  /[^a-zA-Z0-9+_-]/g,
                  "_",
                ).slice(0, 96)
              }`,
              turnId: liveTurnId,
            });
          }
        },
      });
    }
    return liveTranscriber;
  }

  function queueLiveAudio(chunk: Uint8Array) {
    const transcriber = ensureLiveTranscriber();
    if (!transcriber || liveFailed) return;
    liveAudioChain = liveAudioChain.then(() =>
      transcriber.appendPcm(chunk, session.sampleRateHertz)
    ).catch((error) => {
      liveFailed = true;
      console.warn(
        "voice-stream Gemini Live fallback",
        error instanceof Error ? error.message : String(error),
      );
    });
  }

  async function finishLiveTranscript(): Promise<string> {
    const transcriber = liveTranscriber;
    if (!transcriber || liveFailed) return "";
    await liveAudioChain;
    try {
      return await transcriber.finishTurn();
    } catch (error) {
      liveFailed = true;
      console.warn(
        "voice-stream Gemini Live finalization fallback",
        error instanceof Error ? error.message : String(error),
      );
      return "";
    }
  }

  function clearBuffer() {
    chunks.length = 0;
    totalBytes = 0;
  }

  function clearPendingTurn() {
    pendingTurnChunks.length = 0;
    pendingTurnBytes = 0;
  }

  function appendPendingTurn(chunk: Uint8Array) {
    if (chunk.byteLength === 0) return true;
    if (pendingTurnBytes + chunk.byteLength > MAX_BUFFER_BYTES) {
      clearPendingTurn();
      send(socket, {
        type: "error",
        code: "audio_turn_overflow",
        message: `Accumulated turn audio exceeded ${MAX_BUFFER_BYTES} bytes`,
      });
      return false;
    }
    pendingTurnChunks.push(chunk);
    pendingTurnBytes += chunk.byteLength;
    return true;
  }

  function appendChunk(chunk: Uint8Array) {
    if (chunk.byteLength === 0) return;
    if (totalBytes + chunk.byteLength > MAX_BUFFER_BYTES) {
      clearBuffer();
      send(socket, {
        type: "error",
        code: "audio_buffer_overflow",
        message: `Audio buffer exceeded ${MAX_BUFFER_BYTES} bytes`,
      });
      return;
    }
    chunks.push(chunk);
    totalBytes += chunk.byteLength;
    frameCounter++;
    queueLiveAudio(chunk);
  }

  async function commit(turnId?: string, final = true) {
    if (totalBytes === 0) {
      send(socket, {
        type: "error",
        code: "empty_audio_buffer",
        message: "No audio to commit",
      });
      return;
    }

    const id = turnId || `turn-${++turnCounter}`;
    send(socket, {
      type: "input_audio_buffer.committed",
      turnId: id,
      pcmBytes: totalBytes,
    });
    console.log("voice-stream commit", {
      turnId: id,
      final,
      pcmBytes: totalBytes,
      frames: frameCounter,
    });

    const pcm = concatChunks(chunks, totalBytes);
    clearBuffer();
    frameCounter = 0;

    try {
      if (!final) {
        appendPendingTurn(pcm);
        return;
      }

      let turnPcm = pcm;
      if (pendingTurnBytes > 0) {
        const aggregateParts = [...pendingTurnChunks, pcm];
        turnPcm = concatChunks(
          aggregateParts,
          pendingTurnBytes + pcm.byteLength,
        );
      }
      clearPendingTurn();

      const liveTranscript = await finishLiveTranscript();

      if (session.face?.trim().toLowerCase() === "theritor") {
        const theritorBody = encodeVoicePipelineStreamBody(
          session,
          turnPcm,
          true,
          liveTranscript ? { transcript: liveTranscript } : undefined,
        );
        const requestBody = new ArrayBuffer(theritorBody.byteLength);
        new Uint8Array(requestBody).set(theritorBody);
        const theritorResponse = await fetch(voicePipelineUrl, {
          method: "POST",
          headers: { ...headers, Accept: "application/x-ndjson" },
          body: requestBody,
        });
        if (!theritorResponse.ok) {
          send(socket, {
            type: "error",
            turnId: id,
            code: "theritor_voice_http",
            status: theritorResponse.status,
            message: await theritorResponse.text(),
          });
          return;
        }
        const contentType = theritorResponse.headers.get("Content-Type") ?? "";
        if (
          contentType.includes("application/x-ndjson") && theritorResponse.body
        ) {
          const reader = theritorResponse.body.getReader();
          const decoder = new TextDecoder();
          let pending = "";
          let doneSeen = false;

          const handleEvent = async (line: string) => {
            if (!line.trim()) return;
            const event = JSON.parse(line);
            if (event.type === "response") {
              const transcript = String(event.transcript ?? liveTranscript)
                .trim();
              const reply = String(event.reply ?? "").trim();
              if (transcript) {
                send(socket, {
                  type: "conversation.item.input_audio_transcription.completed",
                  turnId: id,
                  transcript,
                });
              }
              if (reply) {
                send(socket, {
                  type: "response.text.delta",
                  turnId: id,
                  delta: reply,
                });
              }
            } else if (event.type === "audio" && event.audioBase64) {
              await sendAudioDelta(
                socket,
                id,
                event.audioBase64,
                "mp3",
                Number(event.segmentIndex ?? 0),
                Number(event.segmentCount ?? 1),
              );
            } else if (event.type === "done") {
              send(socket, {
                type: "response.done",
                turnId: id,
                sessionId: event.sessionId,
                expression: event.expression,
              });
              doneSeen = true;
            } else if (event.type === "error") {
              throw new Error(String(event.error ?? "Theritor stream failed"));
            }
          };

          while (true) {
            const { value, done } = await reader.read();
            pending += decoder.decode(value, { stream: !done });
            const lines = pending.split("\n");
            pending = lines.pop() ?? "";
            for (const line of lines) await handleEvent(line);
            if (done) break;
          }
          await handleEvent(pending);
          if (!doneSeen) {
            throw new Error("Theritor stream ended before response.done");
          }
        } else {
          const theritorResult = await theritorResponse.json() as
            & VoicePipelineResponse
            & { sessionId?: string; expression?: string };
          const transcript = (theritorResult.transcript ?? liveTranscript)
            .trim();
          const reply = (theritorResult.reply ?? "").trim();
          if (transcript) {
            send(socket, {
              type: "conversation.item.input_audio_transcription.completed",
              turnId: id,
              transcript,
            });
          }
          if (reply) {
            send(socket, {
              type: "response.text.delta",
              turnId: id,
              delta: reply,
            });
          }
          const audioSegments = Array.isArray(theritorResult.audioChunksBase64)
            ? theritorResult.audioChunksBase64.filter((chunk) =>
              typeof chunk === "string" && chunk.length > 0
            )
            : [];
          if (audioSegments.length > 0) {
            for (let index = 0; index < audioSegments.length; index++) {
              await sendAudioDelta(
                socket,
                id,
                audioSegments[index],
                "mp3",
                index,
                audioSegments.length,
              );
            }
          } else if (theritorResult.audioBase64) {
            await sendAudioDelta(socket, id, theritorResult.audioBase64, "mp3");
          }
          send(socket, {
            type: "response.done",
            turnId: id,
            sessionId: theritorResult.sessionId,
            expression: theritorResult.expression,
          });
        }
        liveFailed = false;
        liveTurnId = `live-${++turnCounter}`;
        return;
      }

      const stagedConversation = final &&
        session.interactionMode === "conversation";

      if (stagedConversation) {
        const serviceHeaders: HeadersInit = {
          "Content-Type": "application/json",
          "Accept": "application/json",
          ...(req.headers.get("Authorization")
            ? { Authorization: req.headers.get("Authorization")! }
            : {}),
          ...(req.headers.get("apikey")
            ? { apikey: req.headers.get("apikey")! }
            : {}),
        };
        const sttBody = encodeVoicePipelineStreamBody(session, turnPcm, false, {
          interactionMode: "transcribe",
          commonplaceMode: "off",
          earlyRoute: true,
          skipLlm: true,
          responseFormat: "json",
          logToCommonplace: false,
        });
        const sttRequestBody = new ArrayBuffer(sttBody.byteLength);
        new Uint8Array(sttRequestBody).set(sttBody);
        const sttResult = await jsonFetch<VoicePipelineResponse>(
          voicePipelineUrl,
          {
            method: "POST",
            headers,
            body: sttRequestBody,
          },
        );
        if (!sttResult.ok) {
          send(socket, {
            type: "error",
            turnId: id,
            code: "voice_pipeline_stt_http",
            status: sttResult.status,
            message: sttResult.message,
          });
          return;
        }

        const transcript = (sttResult.json.transcript ?? "").trim();
        if (
          typeof sttResult.json.facultySlug === "string" &&
          sttResult.json.facultySlug.trim()
        ) {
          session.facultySlug = sttResult.json.facultySlug.trim();
        }
        if (
          typeof sttResult.json.facultyName === "string" &&
          sttResult.json.facultyName.trim()
        ) {
          session.facultyName = sttResult.json.facultyName.trim();
        }
        if (transcript) {
          send(socket, {
            type: "conversation.item.input_audio_transcription.completed",
            turnId: id,
            transcript,
          });
        }

        const facultyConversation = !!session.facultySlug ||
          isDedicatedFacultyFace(session.face ?? "") ||
          sttResult.json.askFacultyRoute === true;
        const textStage = facultyConversation
          ? await jsonFetch<AskFacultyResponse>(askFacultyUrl, {
            method: "POST",
            headers: serviceHeaders,
            body: JSON.stringify({
              message: transcript,
              rawTranscript: transcript,
              languageCode: session.languageCode,
              geminiModel: Deno.env.get("GEMINI_MODEL")?.trim() ??
                "gemini-2.5-flash",
              generateTts: false,
              facultySlug: session.facultySlug,
              facultyName: session.facultyName,
              conversationHistory: session.conversationHistory,
              ...(session.systemInstruction
                ? { systemInstruction: session.systemInstruction }
                : {}),
              skipLlm: session.skipLlm,
              commonplaceMode: session.commonplaceMode,
              logToCommonplace: session.logToCommonplace,
            }),
          })
          : await jsonFetch<VoicePipelineResponse>(voicePipelineUrl, {
            method: "POST",
            headers: serviceHeaders,
            body: JSON.stringify({
              message: transcript,
              languageCode: session.languageCode,
              face: session.face,
              facultySlug: session.facultySlug,
              facultyName: session.facultyName,
              conversationHistory: session.conversationHistory,
              ...(session.systemInstruction
                ? { systemInstruction: session.systemInstruction }
                : {}),
              skipLlm: session.skipLlm,
              generateTts: false,
              responseFormat: "json",
              commonplaceMode: session.commonplaceMode,
              logToCommonplace: session.logToCommonplace,
            }),
          });
        if (!textStage.ok) {
          send(socket, {
            type: "error",
            turnId: id,
            code: facultyConversation
              ? "ask_faculty_text_http"
              : "voice_pipeline_text_http",
            status: textStage.status,
            message: textStage.message,
          });
          return;
        }

        const reply = (textStage.json.reply ?? "").trim();
        if (
          typeof textStage.json.facultySlug === "string" &&
          textStage.json.facultySlug.trim()
        ) {
          session.facultySlug = textStage.json.facultySlug.trim();
        }
        if (
          typeof textStage.json.facultyName === "string" &&
          textStage.json.facultyName.trim()
        ) {
          session.facultyName = textStage.json.facultyName.trim();
        }
        if (reply) {
          send(socket, {
            type: "response.text.delta",
            turnId: id,
            delta: reply,
          });
        }

        const ttsStage = facultyConversation
          ? await jsonFetch<AskFacultyResponse>(askFacultyUrl, {
            method: "POST",
            headers: serviceHeaders,
            body: JSON.stringify({
              message: reply,
              rawTranscript: transcript,
              languageCode: session.languageCode,
              responseFormat: "json",
              generateTts: true,
              skipLlm: true,
              facultySlug: session.facultySlug,
              facultyName: session.facultyName,
              logToCommonplace: false,
              commonplaceMode: "off",
            }),
          })
          : await jsonFetch<VoicePipelineResponse>(voicePipelineUrl, {
            method: "POST",
            headers: serviceHeaders,
            body: JSON.stringify({
              ttsText: reply,
              message: transcript,
              languageCode: session.languageCode,
              face: session.face,
              facultySlug: session.facultySlug,
              facultyName: session.facultyName,
              responseFormat: "json",
              generateTts: true,
              logToCommonplace: false,
              commonplaceMode: "off",
            }),
          });
        if (!ttsStage.ok) {
          send(socket, {
            type: "error",
            turnId: id,
            code: facultyConversation
              ? "ask_faculty_tts_http"
              : "voice_pipeline_tts_http",
            status: ttsStage.status,
            message: ttsStage.message,
          });
          return;
        }

        if (ttsStage.json.audioBase64) {
          await sendAudioDelta(socket, id, ttsStage.json.audioBase64, "mp3");
        }
        send(socket, {
          type: "response.done",
          turnId: id,
          ...(session.facultySlug ? { facultySlug: session.facultySlug } : {}),
          ...(session.facultyName ? { facultyName: session.facultyName } : {}),
        });
        return;
      }

      const body = encodeVoicePipelineStreamBody(session, turnPcm, final);
      const requestBody = new ArrayBuffer(body.byteLength);
      new Uint8Array(requestBody).set(body);
      const res = await fetch(voicePipelineUrl, {
        method: "POST",
        headers,
        body: requestBody,
      });
      const contentType = res.headers.get("Content-Type") ?? "";
      if (!res.ok) {
        send(socket, {
          type: "error",
          turnId: id,
          code: "voice_pipeline_http",
          status: res.status,
          message: await res.text(),
        });
        return;
      }

      if (contentType.includes("audio/mpeg")) {
        const mp3 = new Uint8Array(await res.arrayBuffer());
        const facultySlug = decodedHeader(res, "X-Faculty-Slug");
        const facultyName = decodedHeader(res, "X-Faculty-Name");
        await sendAudioDelta(socket, id, bytesToBase64(mp3), "mp3");
        send(socket, {
          type: "response.done",
          turnId: id,
          ...(facultySlug ? { facultySlug } : {}),
          ...(facultyName ? { facultyName } : {}),
        });
        return;
      }

      const json = await res.json();
      if (typeof json.facultySlug === "string" && json.facultySlug.trim()) {
        session.facultySlug = json.facultySlug.trim();
      }
      if (typeof json.facultyName === "string" && json.facultyName.trim()) {
        session.facultyName = json.facultyName.trim();
      }
      if (json.transcript) {
        send(socket, {
          type: "conversation.item.input_audio_transcription.completed",
          turnId: id,
          transcript: json.transcript,
        });
      }
      if (json.reply) {
        send(socket, {
          type: "response.text.delta",
          turnId: id,
          delta: json.reply,
        });
      }
      if (json.audioBase64) {
        await sendAudioDelta(socket, id, json.audioBase64, "mp3");
      }
      send(socket, {
        type: "response.done",
        turnId: id,
        ...(json.facultySlug ? { facultySlug: json.facultySlug } : {}),
        ...(json.facultyName ? { facultyName: json.facultyName } : {}),
      });
    } catch (e) {
      send(socket, {
        type: "error",
        turnId: id,
        code: "voice_pipeline_fetch",
        message: e instanceof Error ? e.message : String(e),
      });
    }
  }

  socket.onopen = () => {
    send(socket, {
      type: "session.created",
      maxBufferBytes: MAX_BUFFER_BYTES,
      audio: { input: { format: "pcm16", sampleRateHz: 16000, channels: 1 } },
    });
  };

  socket.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) {
      const chunk = decodeBinaryFrame(event.data);
      if (chunk) appendChunk(chunk);
      return;
    }

    if (event.data instanceof Blob) {
      event.data.arrayBuffer().then((buffer) => {
        const chunk = decodeBinaryFrame(buffer);
        if (chunk) appendChunk(chunk);
      });
      return;
    }

    if (typeof event.data !== "string") return;

    let message: Record<string, unknown>;
    try {
      message = JSON.parse(event.data) as Record<string, unknown>;
    } catch {
      send(socket, {
        type: "error",
        code: "invalid_json",
        message: "Invalid JSON event",
      });
      return;
    }

    const type = message.type;
    if (type === "session.update") {
      const next = (message.session ?? {}) as Record<string, unknown>;
      session.face = typeof next.face === "string" ? next.face : session.face;
      session.facultySlug = typeof next.facultySlug === "string"
        ? next.facultySlug
        : typeof next.faculty_slug === "string"
        ? next.faculty_slug
        : session.facultySlug;
      session.facultyName = typeof next.facultyName === "string"
        ? next.facultyName
        : typeof next.faculty_name === "string"
        ? next.faculty_name
        : session.facultyName;
      session.systemInstruction = typeof next.systemInstruction === "string"
        ? next.systemInstruction
        : typeof next.system_instruction === "string"
        ? next.system_instruction
        : session.systemInstruction;
      session.conversationHistory = typeof next.conversationHistory === "string"
        ? next.conversationHistory
        : typeof next.history === "string"
        ? next.history
        : session.conversationHistory;
      session.languageCode = typeof next.languageCode === "string"
        ? next.languageCode
        : session.languageCode;
      session.sampleRateHertz = typeof next.sampleRateHertz === "number"
        ? next.sampleRateHertz
        : typeof next.sample_rate_hz === "number"
        ? next.sample_rate_hz
        : session.sampleRateHertz;
      session.responseFormat = next.responseFormat === "mp3" ? "mp3" : "json";
      const interactionMode = typeof next.interactionMode === "string"
        ? next.interactionMode
        : typeof next.interaction_mode === "string"
        ? next.interaction_mode
        : undefined;
      if (
        interactionMode === "conversation" ||
        interactionMode === "transcribe" ||
        interactionMode === "journal"
      ) {
        session.interactionMode = interactionMode;
      }
      const commonplaceMode = typeof next.commonplaceMode === "string"
        ? next.commonplaceMode
        : typeof next.commonplace_mode === "string"
        ? next.commonplace_mode
        : undefined;
      if (
        commonplaceMode === "off" || commonplaceMode === "conversation" ||
        commonplaceMode === "journal"
      ) {
        session.commonplaceMode = commonplaceMode;
      }
      session.skipLlm = typeof next.skipLlm === "boolean"
        ? next.skipLlm
        : session.skipLlm;
      session.logToCommonplace = typeof next.logToCommonplace === "boolean"
        ? next.logToCommonplace
        : typeof next.log_to_commonplace === "boolean"
        ? next.log_to_commonplace
        : session.logToCommonplace;
      if (next.respondent === "daniel" || next.respondent === "camille") {
        session.respondent = next.respondent;
      }
      if (next.mode === "therapy" || next.mode === "editor") {
        session.mode = next.mode;
      }
      session.topic = typeof next.topic === "string"
        ? next.topic
        : session.topic;
      session.workSlug = typeof next.workSlug === "string"
        ? next.workSlug
        : typeof next.work_slug === "string"
        ? next.work_slug
        : session.workSlug;
      session.sessionId = typeof next.sessionId === "string"
        ? next.sessionId
        : typeof next.session_id === "string"
        ? next.session_id
        : session.sessionId;
      session.syntheticValidation = typeof next.syntheticValidation === "boolean"
        ? next.syntheticValidation
        : typeof next.synthetic_validation === "boolean"
        ? next.synthetic_validation
        : session.syntheticValidation;
      send(socket, { type: "session.updated", session });
      return;
    }

    if (type === "input_audio_buffer.append") {
      const chunk = decodeAudioAppend(message.audio);
      if (!chunk) {
        send(socket, {
          type: "error",
          code: "invalid_audio",
          message: "Missing base64 audio",
        });
        return;
      }
      appendChunk(chunk);
      return;
    }

    if (type === "input_audio_buffer.commit") {
      const turnId = typeof message.turnId === "string"
        ? message.turnId
        : undefined;
      void commit(turnId, message.final !== false);
      return;
    }

    if (type === "input_audio_buffer.clear") {
      clearBuffer();
      send(socket, { type: "input_audio_buffer.cleared" });
      return;
    }

    send(socket, {
      type: "error",
      code: "unknown_event",
      message: `Unknown event: ${String(type)}`,
    });
  };

  socket.onerror = (event) => {
    console.error(
      "voice-stream socket error",
      event instanceof ErrorEvent ? event.message : event.type,
    );
  };

  socket.onclose = () => {
    console.log("voice-stream close", {
      bufferedBytes: totalBytes,
      frames: frameCounter,
    });
    clearBuffer();
    clearPendingTurn();
    frameCounter = 0;
    liveTranscriber?.close();
  };

  return response;
});
