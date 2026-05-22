import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { Buffer } from "node:buffer";
import { SpeechClient } from "npm:@google-cloud/speech@6.7.1";

import { corsHeaders, jsonResponse } from "../_shared/googleVoice.ts";
import { siblingFunctionUrl } from "../_shared/askFacultyRoute.ts";

type StreamConfig = {
  sampleRateHertz?: number;
  languageCode?: string;
  message?: string;
  systemInstruction?: string;
  skipLlm?: boolean;
  geminiModel?: string;
  face?: string;
  epochSeconds?: number;
  briefingFacts?: string;
  localHour?: number;
  facultySlug?: string;
  facultyName?: string;
};

type ClientJson =
  | ({ type?: "start" | "config" } & StreamConfig)
  | { type: "audio"; audioBase64?: string }
  | { type: "message"; message?: string }
  | { type: "commit" }
  | { type: "reset" }
  | { type: "ping" };

const streamCorsHeaders: Record<string, string> = {
  ...corsHeaders,
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type, x-sample-rate-hertz, x-language-code, x-face, x-local-hour, x-faculty-slug, x-faculty-name",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
};

type GoogleServiceAccount = {
  project_id?: string;
  client_email?: string;
  private_key?: string;
};

type SttTranscript = {
  transcript: string;
  final: boolean;
  stability?: number;
};

type SpeechStream = ReturnType<SpeechClient["streamingRecognize"]>;

let sSpeechClient: SpeechClient | null | undefined;
let sGoogleToken: { accessToken: string; expiresAtMs: number } | undefined;

function maxAudioBytes(): number {
  const raw = Deno.env.get("MYNAH_STREAM_MAX_AUDIO_BYTES")?.trim();
  const n = raw ? parseInt(raw, 10) : 1200 * 1024;
  return Number.isFinite(n) && n >= 64 * 1024 ? n : 1200 * 1024;
}

function cleanString(value: unknown): string | undefined {
  return typeof value === "string" && value.trim() ? value.trim() : undefined;
}

function finiteNumber(value: unknown): number | undefined {
  const n = Number(value);
  return Number.isFinite(n) ? n : undefined;
}

function normalizeConfig(input: StreamConfig = {}): StreamConfig {
  const out: StreamConfig = {};
  const sampleRateHertz = finiteNumber(input.sampleRateHertz);
  if (sampleRateHertz && sampleRateHertz >= 8000 && sampleRateHertz <= 48000) {
    out.sampleRateHertz = Math.floor(sampleRateHertz);
  }
  out.languageCode = cleanString(input.languageCode) ?? "en-US";
  out.message = cleanString(input.message);
  out.systemInstruction = cleanString(input.systemInstruction);
  out.skipLlm = input.skipLlm === true;
  out.geminiModel = cleanString(input.geminiModel);
  out.face = cleanString(input.face)?.toLowerCase();
  const epochSeconds = finiteNumber(input.epochSeconds);
  if (epochSeconds) out.epochSeconds = Math.floor(epochSeconds);
  out.briefingFacts = cleanString(input.briefingFacts);
  const localHour = finiteNumber(input.localHour);
  if (localHour !== undefined && localHour >= 0 && localHour <= 23) {
    out.localHour = Math.floor(localHour);
  }
  out.facultySlug = cleanString(input.facultySlug);
  out.facultyName = cleanString(input.facultyName);
  return out;
}

function mergeConfig(base: StreamConfig, next: StreamConfig): StreamConfig {
  return normalizeConfig({ ...base, ...next });
}

function serviceAccountJson(): GoogleServiceAccount | undefined {
  const raw = Deno.env.get("GOOGLE_STT_SERVICE_ACCOUNT_JSON")?.trim() ||
    Deno.env.get("GOOGLE_APPLICATION_CREDENTIALS_JSON")?.trim() ||
    Deno.env.get("VERTEX_SERVICE_ACCOUNT_JSON")?.trim() ||
    "";
  if (!raw) return undefined;
  const parsed = JSON.parse(raw) as GoogleServiceAccount;
  if (parsed.private_key) {
    parsed.private_key = parsed.private_key.replace(/\\n/g, "\n");
  }
  if (!parsed.client_email || !parsed.private_key) {
    throw new Error(
      "Google service account JSON missing client_email/private_key",
    );
  }
  return parsed;
}

function speechClient(): SpeechClient | undefined {
  if (sSpeechClient !== undefined) return sSpeechClient ?? undefined;
  try {
    const sa = serviceAccountJson();
    if (!sa) {
      sSpeechClient = null;
      return undefined;
    }
    sSpeechClient = new SpeechClient({
      projectId: sa.project_id,
      credentials: {
        client_email: sa.client_email,
        private_key: sa.private_key,
      },
    });
    return sSpeechClient;
  } catch (e) {
    console.error(
      "voice-stream: speech client init failed:",
      e instanceof Error ? e.message : String(e),
    );
    sSpeechClient = null;
    return undefined;
  }
}

function streamingSttEnabled(): boolean {
  return Deno.env.get("MYNAH_STREAMING_STT_DISABLED") !== "1" &&
    speechClient() !== undefined;
}

function uint8ToBase64(bytes: Uint8Array): string {
  let out = "";
  const step = 0x8000;
  for (let i = 0; i < bytes.length; i += step) {
    out += String.fromCharCode(...bytes.subarray(i, i + step));
  }
  return btoa(out);
}

function base64ToUint8(b64: string): Uint8Array {
  const bin = atob(b64.trim());
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) out[i] = bin.charCodeAt(i);
  return out;
}

function base64UrlEncode(bytes: Uint8Array): string {
  let raw = "";
  for (const b of bytes) raw += String.fromCharCode(b);
  return btoa(raw).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function textBase64Url(value: string): string {
  return base64UrlEncode(new TextEncoder().encode(value));
}

function pemPrivateKeyDer(pem: string): Uint8Array {
  const b64 = pem.replace(/-----BEGIN PRIVATE KEY-----/g, "")
    .replace(/-----END PRIVATE KEY-----/g, "")
    .replace(/\s+/g, "");
  return base64ToUint8(b64);
}

async function googleAccessToken(): Promise<string> {
  const now = Date.now();
  if (sGoogleToken && sGoogleToken.expiresAtMs - now > 120_000) {
    return sGoogleToken.accessToken;
  }
  const sa = serviceAccountJson();
  if (!sa?.client_email || !sa.private_key) {
    throw new Error("Google service account JSON is not configured");
  }
  const iat = Math.floor(now / 1000);
  const exp = iat + 3600;
  const assertionHead = textBase64Url(
    JSON.stringify({ alg: "RS256", typ: "JWT" }),
  );
  const assertionBody = textBase64Url(JSON.stringify({
    iss: sa.client_email,
    scope: "https://www.googleapis.com/auth/cloud-platform",
    aud: "https://oauth2.googleapis.com/token",
    iat,
    exp,
  }));
  const unsigned = `${assertionHead}.${assertionBody}`;
  const key = await crypto.subtle.importKey(
    "pkcs8",
    pemPrivateKeyDer(sa.private_key),
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const sig = new Uint8Array(
    await crypto.subtle.sign(
      "RSASSA-PKCS1-v1_5",
      key,
      new TextEncoder().encode(unsigned),
    ),
  );
  const assertion = `${unsigned}.${base64UrlEncode(sig)}`;
  const res = await fetch("https://oauth2.googleapis.com/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      grant_type: "urn:ietf:params:oauth:grant-type:jwt-bearer",
      assertion,
    }),
  });
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`Google OAuth failed: ${res.status} ${text}`);
  }
  const data = JSON.parse(text) as {
    access_token?: string;
    expires_in?: number;
  };
  if (!data.access_token) {
    throw new Error("Google OAuth returned no access token");
  }
  sGoogleToken = {
    accessToken: data.access_token,
    expiresAtMs: now + Math.max(60, data.expires_in ?? 3600) * 1000,
  };
  return data.access_token;
}

function concatChunks(chunks: Uint8Array[], total: number): Uint8Array {
  const out = new Uint8Array(total);
  let off = 0;
  for (const chunk of chunks) {
    out.set(chunk, off);
    off += chunk.length;
  }
  return out;
}

function requestHeaders(req: Request): Record<string, string> {
  const auth = req.headers.get("Authorization") ?? "";
  const apikey = req.headers.get("apikey") ?? "";
  return {
    "Content-Type": "application/json",
    Accept: "audio/mpeg",
    ...(auth ? { Authorization: auth } : {}),
    ...(apikey ? { apikey } : {}),
  };
}

function pipelineBody(
  config: StreamConfig,
  audioBase64?: string,
): Record<string, unknown> {
  const body: Record<string, unknown> = {
    ...normalizeConfig(config),
    responseFormat: "mp3",
  };
  if (audioBase64) {
    body.audioBase64 = audioBase64;
    body.sampleRateHertz = body.sampleRateHertz ?? 16000;
  }
  return body;
}

async function callVoicePipeline(
  req: Request,
  config: StreamConfig,
  audio?: Uint8Array,
): Promise<Response> {
  const body = pipelineBody(
    config,
    audio && audio.length ? uint8ToBase64(audio) : undefined,
  );
  if (!body.audioBase64 && !body.message && !body.face) {
    throw new Error("no audio, message, or face supplied");
  }
  const res = await fetch(siblingFunctionUrl("voice-pipeline"), {
    method: "POST",
    headers: requestHeaders(req),
    body: JSON.stringify(body),
  });
  return res;
}

async function callVoicePipelineWithMessage(
  req: Request,
  config: StreamConfig,
  message: string,
): Promise<Response> {
  const merged = mergeConfig(config, { message });
  const body = pipelineBody(merged);
  const res = await fetch(siblingFunctionUrl("voice-pipeline"), {
    method: "POST",
    headers: requestHeaders(req),
    body: JSON.stringify(body),
  });
  return res;
}

async function recognizeBufferedAudio(
  config: StreamConfig,
  audio: Uint8Array,
): Promise<string> {
  if (!audio.length) return "";
  const apiKey = Deno.env.get("GOOGLE_SPEECH_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_SPEECH_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    "";
  const token = apiKey ? "" : await googleAccessToken();
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort("recognize timeout"), 12_000);
  const url = apiKey
    ? `https://speech.googleapis.com/v1/speech:recognize?key=${
      encodeURIComponent(apiKey)
    }`
    : "https://speech.googleapis.com/v1/speech:recognize";
  const res = await fetch(url, {
    method: "POST",
    signal: ctrl.signal,
    headers: {
      ...(token ? { "Authorization": `Bearer ${token}` } : {}),
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      config: {
        encoding: "LINEAR16",
        sampleRateHertz: config.sampleRateHertz ?? 16000,
        languageCode: config.languageCode ?? "en-US",
        enableAutomaticPunctuation: true,
      },
      audio: {
        content: uint8ToBase64(audio),
      },
    }),
  });
  clearTimeout(timer);
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`Google recognize failed: ${res.status} ${text}`);
  }
  const data = JSON.parse(text) as {
    results?: Array<{ alternatives?: Array<{ transcript?: string }> }>;
  };
  return (data.results ?? [])
    .map((r) => r.alternatives?.[0]?.transcript?.trim() ?? "")
    .filter(Boolean)
    .join(" ")
    .replace(/\s+/g, " ")
    .trim();
}

function metaFromPipeline(res: Response): Record<string, string> {
  const keys = [
    "x-voice-route",
    "x-mynah-route",
    "x-mynah-face",
    "x-voice-face",
    "x-voice-language",
    "x-voice-name",
    "x-voice-transcript",
    "x-voice-reply",
    "x-voice-tts-chars",
    "x-faculty-slug",
    "x-faculty-name",
    "x-faculty-accent",
    "x-faculty-language",
  ];
  const out: Record<string, string> = {};
  for (const key of keys) {
    const value = res.headers.get(key);
    if (value) out[key] = value;
  }
  return out;
}

async function readAllBytes(
  stream: ReadableStream<Uint8Array> | null,
): Promise<Uint8Array> {
  if (!stream) return new Uint8Array();
  const reader = stream.getReader();
  const chunks: Uint8Array[] = [];
  let total = 0;
  const limit = maxAudioBytes();
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    if (!value?.length) continue;
    total += value.length;
    if (total > limit) {
      try {
        await reader.cancel();
      } catch {
        // best effort
      }
      throw new Error(`audio too large (${total} > ${limit})`);
    }
    chunks.push(value);
  }
  return concatChunks(chunks, total);
}

function configFromRequest(req: Request): StreamConfig {
  const url = new URL(req.url);
  return normalizeConfig({
    sampleRateHertz: finiteNumber(url.searchParams.get("sampleRateHertz")) ??
      finiteNumber(req.headers.get("x-sample-rate-hertz")),
    languageCode: url.searchParams.get("languageCode") ??
      req.headers.get("x-language-code") ??
      undefined,
    face: url.searchParams.get("face") ?? req.headers.get("x-face") ??
      undefined,
    localHour: finiteNumber(url.searchParams.get("localHour")) ??
      finiteNumber(req.headers.get("x-local-hour")),
    facultySlug: url.searchParams.get("facultySlug") ??
      req.headers.get("x-faculty-slug") ??
      undefined,
    facultyName: url.searchParams.get("facultyName") ??
      req.headers.get("x-faculty-name") ??
      undefined,
    message: url.searchParams.get("message") ?? undefined,
  });
}

async function handleHttpPost(req: Request): Promise<Response> {
  const contentType = req.headers.get("content-type")?.toLowerCase() ?? "";
  if (contentType.includes("application/json")) {
    const body = await req.json() as StreamConfig & { audioBase64?: string };
    const config = normalizeConfig(body);
    const audio = body.audioBase64
      ? base64ToUint8(body.audioBase64)
      : undefined;
    const upstream = await callVoicePipeline(req, config, audio);
    return proxyHttpResponse(upstream);
  }

  const config = configFromRequest(req);
  const audio = await readAllBytes(req.body);
  const upstream = await callVoicePipeline(req, config, audio);
  return proxyHttpResponse(upstream);
}

function proxyHttpResponse(upstream: Response): Response {
  const headers = new Headers(streamCorsHeaders);
  headers.set(
    "Content-Type",
    upstream.headers.get("Content-Type") ?? "audio/mpeg",
  );
  for (const [key, value] of Object.entries(metaFromPipeline(upstream))) {
    headers.set(key, value);
  }
  return new Response(upstream.body, {
    status: upstream.status,
    headers,
  });
}

function wsSendJson(ws: WebSocket, payload: Record<string, unknown>) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  }
}

class StreamingSttSession {
  private stream?: SpeechStream;
  private finalParts: string[] = [];
  private latestTranscript = "";
  private done?: Promise<string>;
  private resolveDone?: (value: string) => void;
  private rejectDone?: (reason?: unknown) => void;
  private closed = false;

  constructor(
    private readonly config: StreamConfig,
    private readonly onTranscript: (event: SttTranscript) => void,
  ) {}

  start() {
    if (this.stream) return;
    const client = speechClient();
    if (!client) {
      throw new Error(
        "streaming STT unavailable: set GOOGLE_STT_SERVICE_ACCOUNT_JSON, GOOGLE_APPLICATION_CREDENTIALS_JSON, or VERTEX_SERVICE_ACCOUNT_JSON",
      );
    }
    this.done = new Promise<string>((resolve, reject) => {
      this.resolveDone = resolve;
      this.rejectDone = reject;
    });
    this.stream = client.streamingRecognize({
      config: {
        encoding: "LINEAR16",
        sampleRateHertz: this.config.sampleRateHertz ?? 16000,
        languageCode: this.config.languageCode ?? "en-US",
        enableAutomaticPunctuation: true,
      },
      interimResults: true,
      singleUtterance: false,
    });
    this.stream.on("data", (data) => this.handleData(data));
    this.stream.on("error", (err) => {
      this.closed = true;
      this.rejectDone?.(err);
    });
    this.stream.on("end", () => this.finish());
    this.stream.on("close", () => this.finish());
  }

  write(bytes: Uint8Array) {
    if (!bytes.length) return;
    this.start();
    if (!this.stream || this.closed) return;
    this.stream.write({ audioContent: Buffer.from(bytes) });
  }

  async commit(): Promise<string> {
    this.start();
    if (!this.stream || this.closed) {
      return this.bestTranscript();
    }
    this.closed = true;
    this.stream.end();
    const done = this.done ?? Promise.resolve(this.bestTranscript());
    return await Promise.race([
      done,
      new Promise<string>((resolve) =>
        setTimeout(() => resolve(this.bestTranscript()), 4500)
      ),
    ]);
  }

  cancel() {
    this.closed = true;
    try {
      this.stream?.destroy();
    } catch {
      // best effort
    }
  }

  private handleData(data: unknown) {
    const result = (data as {
      results?: Array<{
        isFinal?: boolean;
        stability?: number;
        alternatives?: Array<{ transcript?: string }>;
      }>;
    }).results?.[0];
    const transcript = result?.alternatives?.[0]?.transcript?.trim() ?? "";
    if (!transcript) return;
    this.latestTranscript = transcript;
    if (result?.isFinal) {
      this.finalParts.push(transcript);
    }
    this.onTranscript({
      transcript,
      final: result?.isFinal === true,
      stability: result?.stability,
    });
  }

  private bestTranscript(): string {
    const final = this.finalParts.join(" ").replace(/\s+/g, " ").trim();
    return final || this.latestTranscript.trim();
  }

  private finish() {
    if (this.resolveDone) {
      this.resolveDone(this.bestTranscript());
      this.resolveDone = undefined;
      this.rejectDone = undefined;
    }
  }
}

async function wsSendPipelineResult(ws: WebSocket, upstream: Response) {
  const meta = metaFromPipeline(upstream);
  wsSendJson(ws, {
    type: upstream.ok ? "metadata" : "error",
    status: upstream.status,
    headers: meta,
  });

  if (!upstream.ok) {
    const text = await upstream.text();
    wsSendJson(ws, { type: "error", status: upstream.status, error: text });
    return;
  }

  const reader = upstream.body?.getReader();
  if (!reader) {
    wsSendJson(ws, { type: "done", bytes: 0 });
    return;
  }

  let total = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    if (!value?.length) continue;
    total += value.length;
    if (ws.readyState !== WebSocket.OPEN) {
      try {
        await reader.cancel();
      } catch {
        // best effort
      }
      return;
    }
    ws.send(value);
  }
  wsSendJson(ws, { type: "done", bytes: total });
}

function handleWebSocket(req: Request): Response {
  const { socket, response } = Deno.upgradeWebSocket(req);
  let config = configFromRequest(req);
  let chunks: Uint8Array[] = [];
  let total = 0;
  let busy = false;
  let stt: StreamingSttSession | undefined;

  const resetStt = () => {
    stt?.cancel();
    stt = undefined;
  };

  const ensureStt = () => {
    if (!streamingSttEnabled()) return undefined;
    if (!stt) {
      stt = new StreamingSttSession(config, (event) => {
        wsSendJson(socket, { type: "transcript", ...event });
      });
    }
    return stt;
  };

  const appendAudio = (bytes: Uint8Array) => {
    if (!bytes.length) return;
    const session = ensureStt();
    total += bytes.length;
    const limit = maxAudioBytes();
    if (total > limit) {
      const attempted = total;
      chunks = [];
      total = 0;
      throw new Error(`audio too large (${attempted} > ${limit})`);
    }
    chunks.push(bytes);
    if (session) {
      session.write(bytes);
    }
  };

  socket.onopen = () => {
    wsSendJson(socket, {
      type: "ready",
      protocol: "mynah.voice-stream.v1",
      maxAudioBytes: maxAudioBytes(),
      mode: streamingSttEnabled()
        ? "google-streaming-stt"
        : "commit-to-voice-pipeline",
    });
  };

  socket.onmessage = (event) => {
    void (async () => {
      try {
        if (busy) {
          wsSendJson(socket, { type: "error", error: "pipeline busy" });
          return;
        }

        if (typeof event.data === "string") {
          const msg = JSON.parse(event.data) as ClientJson;
          const type = msg.type ?? "config";
          if (type === "ping") {
            wsSendJson(socket, { type: "pong" });
            return;
          }
          if (type === "reset") {
            resetStt();
            chunks = [];
            total = 0;
            wsSendJson(socket, { type: "reset" });
            return;
          }
          if (type === "audio") {
            const audioMsg = msg as Extract<ClientJson, { type: "audio" }>;
            if (audioMsg.audioBase64) {
              appendAudio(base64ToUint8(audioMsg.audioBase64));
            }
            wsSendJson(socket, { type: "ack", bytes: total });
            return;
          }
          if (type === "message") {
            const messageMsg = msg as Extract<ClientJson, { type: "message" }>;
            config = mergeConfig(config, { message: messageMsg.message });
            wsSendJson(socket, { type: "config", config });
            return;
          }
          if (type === "commit") {
            busy = true;
            wsSendJson(socket, { type: "processing", bytes: total });
            const session = stt;
            stt = undefined;
            const audio = concatChunks(chunks, total);
            chunks = [];
            total = 0;
            let upstream: Response;
            if (session) {
              let transcript = await session.commit();
              if (!transcript && audio.length) {
                transcript = await recognizeBufferedAudio(config, audio);
                if (transcript) {
                  wsSendJson(socket, {
                    type: "transcript",
                    transcript,
                    final: true,
                    fallback: "recognize",
                  });
                }
              }
              wsSendJson(socket, {
                type: "transcript",
                transcript,
                final: true,
                committed: true,
              });
              if (!transcript) {
                throw new Error("No speech detected");
              }
              upstream = await callVoicePipelineWithMessage(
                req,
                config,
                transcript,
              );
            } else {
              upstream = await callVoicePipeline(req, config, audio);
            }
            await wsSendPipelineResult(socket, upstream);
            busy = false;
            return;
          }
          config = mergeConfig(config, msg as StreamConfig);
          resetStt();
          wsSendJson(socket, { type: "config", config });
          return;
        }

        if (event.data instanceof ArrayBuffer) {
          appendAudio(new Uint8Array(event.data));
        } else if (event.data instanceof Uint8Array) {
          appendAudio(event.data);
        } else if (event.data instanceof Blob) {
          appendAudio(new Uint8Array(await event.data.arrayBuffer()));
        } else {
          wsSendJson(socket, { type: "error", error: "unsupported frame" });
          return;
        }
        wsSendJson(socket, { type: "ack", bytes: total });
      } catch (e) {
        busy = false;
        resetStt();
        const error = e instanceof Error ? e.message : String(e);
        wsSendJson(socket, { type: "error", error });
      }
    })();
  };

  socket.onclose = () => resetStt();
  socket.onerror = () => resetStt();

  return response;
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: streamCorsHeaders });
  }

  const upgrade = req.headers.get("upgrade")?.toLowerCase() ?? "";
  if (upgrade === "websocket") {
    return handleWebSocket(req);
  }

  if (req.method !== "POST") {
    return jsonResponse(405, {
      error: "Method not allowed",
      websocket:
        'Connect with WebSocket, send config JSON, binary LINEAR16 PCM chunks, then {"type":"commit"}.',
      http:
        "POST raw LINEAR16 PCM with x-sample-rate-hertz/x-language-code headers for an audio/mpeg response.",
    }, streamCorsHeaders);
  }

  try {
    return await handleHttpPost(req);
  } catch (e) {
    const error = e instanceof Error ? e.message : String(e);
    console.error("voice-stream error:", error);
    return jsonResponse(500, { error }, streamCorsHeaders);
  }
});
