import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { verifyAstrolabeDevice } from "../_shared/deviceAuth.ts";

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
};

const MAX_BUFFER_BYTES = 1024 * 1024;
const ASTROLABE_BINARY_PCM = 0xa1;
const VOICE_STREAM_CT = "application/vnd.astrolabe.voice-stream";

function send(socket: WebSocket, payload: unknown) {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(payload));
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

function encodeVoicePipelineStreamBody(session: SessionState, pcm: Uint8Array): Uint8Array {
  const metadata: Record<string, unknown> = {
    languageCode: session.languageCode,
    sampleRateHertz: session.sampleRateHertz,
    responseFormat: session.responseFormat,
    skipLlm: session.skipLlm,
    interactionMode: session.interactionMode,
    commonplaceMode: session.commonplaceMode,
  };
  if (typeof session.logToCommonplace === "boolean") metadata.logToCommonplace = session.logToCommonplace;
  if (session.face) metadata.face = session.face;
  if (session.facultySlug) metadata.facultySlug = session.facultySlug;
  if (session.facultyName) metadata.facultyName = session.facultyName;
  if (session.systemInstruction) metadata.systemInstruction = session.systemInstruction;
  if (session.conversationHistory) metadata.conversationHistory = session.conversationHistory;

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

function authHeaders(req: Request): HeadersInit {
  const headers: Record<string, string> = {
    "Content-Type": VOICE_STREAM_CT,
    "Accept": "application/json,audio/mpeg",
  };
  const auth = req.headers.get("Authorization");
  const apikey = req.headers.get("apikey");
  if (auth) headers.Authorization = auth;
  if (apikey) headers.apikey = apikey;
  return headers;
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

Deno.serve(async (req) => {
  const upgrade = req.headers.get("upgrade") ?? "";
  if (upgrade.toLowerCase() !== "websocket") {
    return new Response("voice-stream expects a WebSocket upgrade", { status: 400 });
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
  let turnCounter = 0;
  let frameCounter = 0;
  const voicePipelineUrl = siblingVoicePipelineUrl(req);
  const headers = authHeaders(req);

  function clearBuffer() {
    chunks.length = 0;
    totalBytes = 0;
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
  }

  async function commit(turnId?: string) {
    if (totalBytes === 0) {
      send(socket, { type: "error", code: "empty_audio_buffer", message: "No audio to commit" });
      return;
    }

    const id = turnId || `turn-${++turnCounter}`;
    send(socket, { type: "input_audio_buffer.committed", turnId: id, pcmBytes: totalBytes });
    console.log("voice-stream commit", { turnId: id, pcmBytes: totalBytes, frames: frameCounter });

    const pcm = concatChunks(chunks, totalBytes);
    clearBuffer();
    frameCounter = 0;
    const body = encodeVoicePipelineStreamBody(session, pcm);

    try {
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
        send(socket, {
          type: "response.audio.delta",
          turnId: id,
          audio: bytesToBase64(mp3),
          encoding: "mp3",
        });
        send(socket, { type: "response.done", turnId: id });
        return;
      }

      const json = await res.json();
      if (json.transcript) {
        send(socket, {
          type: "conversation.item.input_audio_transcription.completed",
          turnId: id,
          transcript: json.transcript,
        });
      }
      if (json.reply) {
        send(socket, { type: "response.text.delta", turnId: id, delta: json.reply });
      }
      if (json.audioBase64) {
        send(socket, {
          type: "response.audio.delta",
          turnId: id,
          audio: json.audioBase64,
          encoding: "mp3",
        });
      }
      send(socket, { type: "response.done", turnId: id });
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
      send(socket, { type: "error", code: "invalid_json", message: "Invalid JSON event" });
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
      session.languageCode = typeof next.languageCode === "string" ? next.languageCode : session.languageCode;
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
        interactionMode === "conversation" || interactionMode === "transcribe" ||
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
      session.skipLlm = typeof next.skipLlm === "boolean" ? next.skipLlm : session.skipLlm;
      session.logToCommonplace = typeof next.logToCommonplace === "boolean"
        ? next.logToCommonplace
        : typeof next.log_to_commonplace === "boolean"
        ? next.log_to_commonplace
        : session.logToCommonplace;
      send(socket, { type: "session.updated", session });
      return;
    }

    if (type === "input_audio_buffer.append") {
      const chunk = decodeAudioAppend(message.audio);
      if (!chunk) {
        send(socket, { type: "error", code: "invalid_audio", message: "Missing base64 audio" });
        return;
      }
      appendChunk(chunk);
      return;
    }

    if (type === "input_audio_buffer.commit") {
      const turnId = typeof message.turnId === "string" ? message.turnId : undefined;
      void commit(turnId);
      return;
    }

    if (type === "input_audio_buffer.clear") {
      clearBuffer();
      send(socket, { type: "input_audio_buffer.cleared" });
      return;
    }

    send(socket, { type: "error", code: "unknown_event", message: `Unknown event: ${String(type)}` });
  };

  socket.onerror = (event) => {
    console.error("voice-stream socket error", event instanceof ErrorEvent ? event.message : event.type);
  };

  socket.onclose = () => {
    console.log("voice-stream close", { bufferedBytes: totalBytes, frames: frameCounter });
    clearBuffer();
    frameCounter = 0;
  };

  return response;
});
