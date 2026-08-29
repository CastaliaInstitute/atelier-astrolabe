export type GeminiLiveTranscriptEvent =
  | { kind: "setup" }
  | { kind: "interim"; text: string }
  | { kind: "final"; text: string }
  | { kind: "error"; message: string }
  | { kind: "none" };

export function normalizeGeminiLiveTranscript(text: string): string {
  const normalized = text.trim();
  if (!normalized) return "";
  const placeholder = normalized
    .toLowerCase()
    .replaceAll(/[\[\]().:_-]/g, " ")
    .replaceAll(/\s+/g, " ")
    .trim();
  if (
    placeholder === "none" ||
    placeholder === "null" ||
    placeholder === "no transcript" ||
    placeholder === "no transcription" ||
    placeholder === "inaudible"
  ) return "";
  return normalized;
}

export function mergeGeminiLiveFinal(
  existing: string,
  incoming: string,
): string {
  const current = normalizeGeminiLiveTranscript(existing);
  const next = normalizeGeminiLiveTranscript(incoming);
  if (!current) return next;
  if (!next || current === next || current.endsWith(next)) return current;
  if (next.startsWith(current)) return next;
  return `${current} ${next}`;
}

export function parseGeminiLiveTranscriptMessage(
  raw: string,
): GeminiLiveTranscriptEvent {
  let message: Record<string, unknown>;
  try {
    message = JSON.parse(raw) as Record<string, unknown>;
  } catch {
    return { kind: "none" };
  }
  const serverError = message.error as Record<string, unknown> | undefined;
  if (serverError) {
    const code = typeof serverError.code === "number"
      ? `Gemini Live error ${serverError.code}`
      : "Gemini Live server error";
    const detail = typeof serverError.message === "string"
      ? serverError.message.trim()
      : "";
    return { kind: "error", message: detail ? `${code}: ${detail}` : code };
  }
  if (message.setupComplete || message.setup_complete) return { kind: "setup" };
  const content =
    (message.serverContent ?? message.server_content ?? {}) as Record<
      string,
      unknown
    >;
  const interim = (content.interimInputTranscription ??
    content.interim_input_transcription ?? {}) as Record<string, unknown>;
  if (typeof interim.text === "string") {
    const text = normalizeGeminiLiveTranscript(interim.text);
    if (text) return { kind: "interim", text };
  }
  const final =
    (content.inputTranscription ?? content.input_transcription ?? {}) as Record<
      string,
      unknown
    >;
  if (typeof final.text === "string") {
    const text = normalizeGeminiLiveTranscript(final.text);
    if (text) return { kind: "final", text };
  }
  return { kind: "none" };
}

type GeminiLiveTranscriberOptions = {
  apiKey: string;
  languageCode?: string;
  model?: string;
  vocabulary?: string[];
  onInterim?: (text: string) => void;
  onFinal?: (text: string) => void;
  onDiagnostic?: (message: string) => void;
};

const LIVE_ENDPOINT =
  "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";

const LIVE_AUDIO_CHUNK_MS = 100;

export function splitPcmForGeminiLive(
  pcm: Uint8Array,
  sampleRateHertz = 16000,
): Uint8Array[] {
  const bytesPerSample = 2;
  const chunkBytes = Math.max(
    bytesPerSample,
    Math.floor(
      sampleRateHertz * bytesPerSample * LIVE_AUDIO_CHUNK_MS / 1000,
    ),
  );
  const chunks: Uint8Array[] = [];
  for (let offset = 0; offset < pcm.byteLength; offset += chunkBytes) {
    chunks.push(pcm.subarray(offset, offset + chunkBytes));
  }
  return chunks;
}

export class GeminiLiveTranscriber {
  private readonly options: GeminiLiveTranscriberOptions;
  private socket?: WebSocket;
  private setupPromise?: Promise<void>;
  private setupResolve?: () => void;
  private setupReject?: (reason: Error) => void;
  private setupReady = false;
  private turnActive = false;
  private interim = "";
  private final = "";
  private finalResolve?: (text: string) => void;
  private finalSettleTimer?: ReturnType<typeof setTimeout>;
  private finishRequested = false;

  constructor(options: GeminiLiveTranscriberOptions) {
    this.options = options;
  }

  private fail(error: Error) {
    if (this.finalSettleTimer !== undefined) {
      clearTimeout(this.finalSettleTimer);
      this.finalSettleTimer = undefined;
    }
    this.setupReject?.(error);
    this.setupResolve = undefined;
    this.setupReject = undefined;
    this.finalResolve?.(this.final || this.interim);
    this.finalResolve = undefined;
  }

  private scheduleFinalSettle() {
    if (!this.finishRequested || !this.finalResolve || !this.final) return;
    if (this.finalSettleTimer !== undefined) {
      clearTimeout(this.finalSettleTimer);
    }
    this.finalSettleTimer = setTimeout(() => {
      this.finalSettleTimer = undefined;
      this.finalResolve?.(this.final);
      this.finalResolve = undefined;
    }, 650);
  }

  private handleMessage(raw: string) {
    const event = parseGeminiLiveTranscriptMessage(raw);
    if (event.kind === "setup") {
      this.options.onDiagnostic?.("setup-complete");
      this.setupReady = true;
      this.setupResolve?.();
      this.setupResolve = undefined;
      this.setupReject = undefined;
    } else if (event.kind === "interim") {
      this.interim = event.text;
      this.options.onInterim?.(event.text);
    } else if (event.kind === "final") {
      this.final = mergeGeminiLiveFinal(this.final, event.text);
      this.options.onFinal?.(this.final);
      this.scheduleFinalSettle();
    } else if (event.kind === "error") {
      this.options.onDiagnostic?.(event.message);
      this.fail(new Error(event.message));
    } else {
      this.options.onDiagnostic?.(describeGeminiLiveMessage(raw));
    }
  }

  private async handleSocketData(data: unknown) {
    if (typeof data === "string") {
      this.handleMessage(data);
      return;
    }
    if (data instanceof Blob) {
      this.handleMessage(await data.text());
      return;
    }
    if (data instanceof ArrayBuffer) {
      this.handleMessage(new TextDecoder().decode(data));
      return;
    }
    this.options.onDiagnostic?.("unsupported-websocket-frame");
  }

  private async connect(): Promise<void> {
    if (this.socket?.readyState === WebSocket.OPEN && this.setupReady) return;
    if (this.setupPromise) return await this.setupPromise;
    const apiKey = this.options.apiKey.trim();
    if (!apiKey) throw new Error("Gemini Live API key is missing");

    this.setupReady = false;
    this.setupPromise = new Promise<void>((resolve, reject) => {
      this.setupResolve = resolve;
      this.setupReject = reject;
    });
    const url = `${LIVE_ENDPOINT}?key=${encodeURIComponent(apiKey)}`;
    const socket = new WebSocket(url);
    this.socket = socket;
    socket.onopen = () => {
      socket.send(JSON.stringify({
        setup: {
          model: `models/${
            this.options.model?.trim() || "gemini-3.5-transcribe-live"
          }`,
          generationConfig: { responseModalities: ["TEXT"] },
          inputAudioTranscription: {
            languageCodes: [this.options.languageCode?.trim() || "en-US"],
            customVocabulary: this.options.vocabulary ?? [],
            mode: "SMART",
          },
        },
      }));
    };
    socket.onmessage = (event) => {
      void this.handleSocketData(event.data).catch((error) =>
        this.fail(
          error instanceof Error
            ? error
            : new Error("Gemini Live frame decoding failed"),
        )
      );
    };
    socket.onerror = () => {
      this.options.onDiagnostic?.("websocket-error");
      this.fail(new Error("Gemini Live WebSocket error"));
    };
    socket.onclose = (event) => {
      this.options.onDiagnostic?.(
        `websocket-close-${event.code}${
          event.reason ? `-${event.reason}` : ""
        }`,
      );
      this.setupReady = false;
      this.setupPromise = undefined;
      this.socket = undefined;
      this.turnActive = false;
      this.fail(new Error("Gemini Live WebSocket closed"));
    };

    try {
      await Promise.race([
        this.setupPromise,
        new Promise<never>((_, reject) =>
          setTimeout(
            () => reject(new Error("Gemini Live setup timed out")),
            8000,
          )
        ),
      ]);
    } catch (error) {
      socket.close();
      this.setupPromise = undefined;
      throw error;
    }
  }

  private send(payload: unknown) {
    if (this.socket?.readyState !== WebSocket.OPEN || !this.setupReady) {
      throw new Error("Gemini Live WebSocket is not ready");
    }
    this.socket.send(JSON.stringify(payload));
  }

  async appendPcm(pcm: Uint8Array, sampleRateHertz = 16000): Promise<void> {
    if (!pcm.byteLength) return;
    await this.connect();
    if (!this.turnActive) {
      this.interim = "";
      this.final = "";
      this.finishRequested = false;
      this.turnActive = true;
    }
    for (const chunk of splitPcmForGeminiLive(pcm, sampleRateHertz)) {
      this.send({
        realtimeInput: {
          audio: {
            data: bytesToBase64(chunk),
            mimeType: `audio/pcm;rate=${sampleRateHertz}`,
          },
        },
      });
    }
  }

  async finishTurn(timeoutMs = 10000): Promise<string> {
    if (!this.turnActive) return "";
    const transcript = new Promise<string>((resolve) => {
      this.finalResolve = resolve;
    });
    this.finishRequested = true;
    this.send({ realtimeInput: { audioStreamEnd: true } });
    this.scheduleFinalSettle();
    const result = await Promise.race([
      transcript,
      new Promise<string>((resolve) =>
        setTimeout(() => resolve(this.final || this.interim), timeoutMs)
      ),
    ]);
    if (this.finalSettleTimer !== undefined) {
      clearTimeout(this.finalSettleTimer);
      this.finalSettleTimer = undefined;
    }
    this.turnActive = false;
    this.finishRequested = false;
    this.finalResolve = undefined;
    return result.trim();
  }

  close() {
    this.socket?.close();
    this.socket = undefined;
    this.setupPromise = undefined;
    this.setupReady = false;
    this.turnActive = false;
    this.finishRequested = false;
    if (this.finalSettleTimer !== undefined) {
      clearTimeout(this.finalSettleTimer);
      this.finalSettleTimer = undefined;
    }
  }
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunkSize = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    binary += String.fromCharCode(
      ...bytes.subarray(offset, offset + chunkSize),
    );
  }
  return btoa(binary);
}

function describeGeminiLiveMessage(raw: string): string {
  try {
    const message = JSON.parse(raw) as Record<string, unknown>;
    const topLevel = Object.keys(message).sort().join("+") || "empty";
    const content = (message.serverContent ?? message.server_content) as
      | Record<string, unknown>
      | undefined;
    const contentKeys = content
      ? Object.keys(content).sort().join("+") || "empty"
      : "none";
    return `message-${topLevel}-content-${contentKeys}`;
  } catch {
    return "message-invalid-json";
  }
}
