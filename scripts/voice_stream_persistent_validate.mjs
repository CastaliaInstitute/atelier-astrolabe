#!/usr/bin/env node

import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";

const ROOT = resolve(new URL("..", import.meta.url).pathname);
const DEFAULT_PHRASE = "Charles Darwin, what are you observing today?";

function parseArgs(argv) {
  const out = {
    phrase: DEFAULT_PHRASE,
    segments: 3,
    rate: 170,
    segmentPauseMs: 400,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--phrase") out.phrase = argv[++i];
    else if (arg === "--segments") out.segments = Math.max(1, Number(argv[++i] || "3"));
    else if (arg === "--rate") out.rate = Math.max(80, Number(argv[++i] || "170"));
    else if (arg === "--segment-pause-ms") out.segmentPauseMs = Math.max(0, Number(argv[++i] || "400"));
    else if (arg === "--help") {
      console.log("usage: node scripts/voice_stream_persistent_validate.mjs [--phrase TEXT] [--segments N] [--rate WPM]");
      process.exit(0);
    }
  }
  return out;
}

function readSecret(name) {
  const text = readFileSync(resolve(ROOT, "include/secrets.local.h"), "utf8");
  const match = text.match(new RegExp(`#define\\s+${name}\\s+"([^"]*)"`));
  if (!match || !match[1]) {
    throw new Error(`missing ${name} in include/secrets.local.h`);
  }
  return match[1];
}

function supabaseWsUrl(httpUrl) {
  const url = new URL(httpUrl);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  url.pathname = "/functions/v1/voice-stream";
  url.search = "";
  url.hash = "";
  return url.toString();
}

function run(cmd, args, opts = {}) {
  const res = spawnSync(cmd, args, { stdio: "pipe", encoding: "utf8", ...opts });
  if (res.status !== 0) {
    throw new Error(`${cmd} failed: ${res.stderr || res.stdout}`);
  }
  return res;
}

function synthesizePcm(tempDir, phrase, rate) {
  const aiff = join(tempDir, "prompt.aiff");
  const pcm = join(tempDir, "prompt.s16le");
  run("say", ["-r", String(rate), "-o", aiff, phrase]);
  run("ffmpeg", [
    "-hide_banner",
    "-loglevel",
    "error",
    "-y",
    "-i",
    aiff,
    "-ac",
    "1",
    "-ar",
    "16000",
    "-f",
    "s16le",
    pcm,
  ]);
  return readFileSync(pcm);
}

function makeBinaryFrame(sequence, captureMs, pcmSlice) {
  const out = Buffer.alloc(9 + pcmSlice.length);
  out[0] = 0xa1;
  out.writeUInt32LE(sequence >>> 0, 1);
  out.writeUInt32LE(captureMs >>> 0, 5);
  pcmSlice.copy(out, 9);
  return out;
}

function sleep(ms) {
  return new Promise((resolvePromise) => setTimeout(resolvePromise, ms));
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const supabaseUrl = readSecret("MYNAH_SUPABASE_URL");
  const anonKey = readSecret("MYNAH_SUPABASE_ANON_KEY");
  const wsUrl = supabaseWsUrl(supabaseUrl);
  const tempDir = mkdtempSync(join(tmpdir(), "astrolabe-ws-"));
  const events = [];

  try {
    const pcm = synthesizePcm(tempDir, args.phrase, args.rate);
    const segmentBytes = Math.ceil(pcm.length / args.segments);

    const ws = new WebSocket(wsUrl, {
      headers: {
        apikey: anonKey,
        Authorization: `Bearer ${anonKey}`,
      },
    });

    let sessionCreated = 0;
    let sessionUpdated = 0;
    let committed = 0;
    let textDelta = 0;
    let audioDelta = 0;
    let done = 0;
    let transcript = "";
    let reply = "";
    let maxAudioChars = 0;
    let closed = false;
    let finalError = null;

    const donePromise = new Promise((resolvePromise, rejectPromise) => {
      const timeout = setTimeout(() => {
        rejectPromise(new Error("timed out waiting for response.done"));
      }, 120000);

      ws.addEventListener("open", async () => {
        events.push({ type: "socket.open", at: Date.now() });
        ws.send(JSON.stringify({
          type: "session.update",
          session: {
            sampleRateHertz: 16000,
            sample_width_bits: 16,
            channels: 1,
            encoding: "pcm16",
            face: "faculty",
            facultySlug: "a.darwin",
            facultyName: "Charles Darwin",
            interactionMode: "conversation",
            commonplaceMode: "off",
            responseFormat: "mp3",
            skipLlm: false,
            logToCommonplace: false,
          },
        }));

        let sentBytes = 0;
        for (let i = 0; i < args.segments; i += 1) {
          const start = i * segmentBytes;
          const end = Math.min(pcm.length, start + segmentBytes);
          const slice = pcm.subarray(start, end);
          if (slice.length === 0) break;
          const captureMs = Math.floor((sentBytes / 2) * 1000 / 16000);
          ws.send(makeBinaryFrame(i, captureMs, slice));
          sentBytes += slice.length;
          const final = i === args.segments - 1 || end >= pcm.length;
          ws.send(JSON.stringify({
            type: "input_audio_buffer.commit",
            turnId: `segment-${i}`,
            final,
            bytes: slice.length,
          }));
          events.push({ type: "client.commit", segment: i, final, bytes: slice.length, captureMs });
          if (!final && args.segmentPauseMs > 0) {
            await sleep(args.segmentPauseMs);
          }
        }
      });

      ws.addEventListener("message", (event) => {
        const text = typeof event.data === "string" ? event.data : Buffer.from(event.data).toString("utf8");
        let parsed;
        try {
          parsed = JSON.parse(text);
        } catch {
          events.push({ type: "server.raw", bytes: text.length });
          return;
        }
        events.push({ type: parsed.type, at: Date.now() });
        if (parsed.type === "session.created") sessionCreated += 1;
        if (parsed.type === "session.updated") sessionUpdated += 1;
        if (parsed.type === "input_audio_buffer.committed") committed += 1;
        if (parsed.type === "conversation.item.input_audio_transcription.completed") {
          transcript = parsed.transcript || "";
        }
        if (parsed.type === "response.text.delta") {
          textDelta += 1;
          reply += parsed.delta || "";
        }
        if (parsed.type === "response.audio.delta") {
          audioDelta += 1;
          maxAudioChars = Math.max(maxAudioChars, (parsed.audio || "").length);
        }
        if (parsed.type === "error") {
          finalError = parsed;
        }
        if (parsed.type === "response.done") {
          done += 1;
          clearTimeout(timeout);
          resolvePromise({
            sessionCreated,
            sessionUpdated,
            committed,
            textDelta,
            audioDelta,
            done,
            transcript,
            reply,
            maxAudioChars,
            finalError,
          });
        }
      });

      ws.addEventListener("close", () => {
        closed = true;
        events.push({ type: "socket.close", at: Date.now() });
      });

      ws.addEventListener("error", (event) => {
        events.push({ type: "socket.error", message: String(event.message || event.type || "error") });
      });
    });

    const summary = await donePromise;
    if (!closed) {
      ws.close();
      await sleep(300);
    }

    const out = {
      phrase: args.phrase,
      segments: args.segments,
      pcmBytes: pcm.length,
      wsUrl,
      summary,
      events,
    };
    const outPath = resolve(ROOT, "artifacts/qa/stream-persistent-validate.json");
    writeFileSync(outPath, JSON.stringify(out, null, 2));
    console.log(`stream persistent validation written to ${outPath}`);
    console.log(JSON.stringify(summary, null, 2));
  } finally {
    rmSync(tempDir, { recursive: true, force: true });
  }
}

main().catch((error) => {
  console.error(error.stack || String(error));
  process.exit(1);
});
