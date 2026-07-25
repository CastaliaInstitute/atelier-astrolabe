#!/usr/bin/env -S deno run --allow-env --allow-net --allow-read

import {
  LUNASAY_GENERATED_DAILY_FACE_IDS,
  type LunaSayDailyPacket,
} from "../supabase/functions/_shared/lunasayDailyPacket.ts";
import type { LunaSayReadingMemory } from "../supabase/functions/_shared/lunasayContinuity.ts";

type QualitySummary = {
  benchmarkVersion: number;
  hardGatePassed: boolean;
  releaseGatePassed: boolean;
  averageScore: number;
  minimumFaceScore: number;
  maximumSimilarity: number;
  weakFaces: Array<{
    face: string;
    score: number;
    issues: string[];
    spokenBytes?: number;
    spokenSentences?: number;
  }>;
};

type ProbeResult = {
  sample: number;
  status: number;
  model: string;
  llmCalls: number;
  fallback: boolean;
  faceFallbacks: string[];
  faceFallbackReasons: Record<string, string>;
  faceRetries: Record<string, number>;
  faceRetryReasons: Record<string, string[]>;
  continuityAppliedFaces: string[];
  continuityDepthByFace: Record<string, number>;
  continuityContractPassed: boolean;
  actionContractPassed: boolean;
  quality: QualitySummary;
};

function requiredEnv(name: string): string {
  const value = Deno.env.get(name)?.trim() ?? "";
  if (!value) throw new Error(`missing environment variable: ${name}`);
  return value;
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join(
    "",
  );
}

function hexBytes(value: string): Uint8Array {
  const parts = value.trim().match(/../g);
  if (!parts || parts.join("").length !== value.trim().length) {
    throw new Error("ASTROLABE_DEVICE_SECRET must be even-length hex");
  }
  return new Uint8Array(parts.map((part) => Number.parseInt(part, 16)));
}

function actionKey(value: string | undefined): string {
  return (value ?? "").toLowerCase().replace(/[^a-z0-9']+/g, " ").trim();
}

async function signature(
  secretHex: string,
  payload: string,
): Promise<string> {
  const secret = hexBytes(secretHex);
  const secretBuffer = new ArrayBuffer(secret.byteLength);
  new Uint8Array(secretBuffer).set(secret);
  const key = await crypto.subtle.importKey(
    "raw",
    secretBuffer,
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  return hex(
    new Uint8Array(
      await crypto.subtle.sign(
        "HMAC",
        key,
        new TextEncoder().encode(payload),
      ),
    ),
  );
}

function actionContractPassed(packet: LunaSayDailyPacket): boolean {
  return LUNASAY_GENERATED_DAILY_FACE_IDS.every((id) => {
    const face = packet.faces[id];
    const action = actionKey(face.action);
    return action.length > 0 && actionKey(face.spoken).includes(action);
  });
}

function continuityContractPassed(
  packet: LunaSayDailyPacket,
  memory: LunaSayReadingMemory | null,
  appliedFaces: string[],
  depthByFace: Record<string, number>,
): boolean {
  if (!memory) return true;
  const history = Array.isArray(memory.history) && memory.history.length
    ? memory.history
    : [memory];
  const expected = LUNASAY_GENERATED_DAILY_FACE_IDS.filter((id) =>
    history.some((day) => Boolean(day.faces[id]))
  );
  return expected.length > 0 &&
    expected.every((id) => appliedFaces.includes(id)) &&
    expected.every((id) =>
      depthByFace[id] ===
        history.filter((day) => Boolean(day.faces[id])).length
    ) &&
    expected.every((id) =>
      history.every((day) =>
        actionKey(packet.faces[id].action) !==
          actionKey(day.faces[id]?.action)
      )
    );
}

function percentile(values: number[], fraction: number): number {
  if (!values.length) return 0;
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.floor((sorted.length - 1) * fraction)];
}

const factsPath = Deno.args[0];
if (!factsPath) {
  console.error(
    "usage: deno run --allow-env --allow-net --allow-read scripts/lunasay_quality_soak.ts FACTS.txt [SAMPLES] [READING_MEMORY.json]",
  );
  Deno.exit(2);
}
const samples = Math.max(
  1,
  Math.min(24, Number.parseInt(Deno.args[1] ?? "6", 10) || 6),
);
const url = requiredEnv("SUPABASE_URL").replace(/\/+$/, "");
const anon = requiredEnv("SUPABASE_ANON_KEY");
const mac = requiredEnv("ASTROLABE_DEVICE_MAC").toLowerCase();
const channel = requiredEnv("ASTROLABE_DEVICE_CHANNEL");
const secret = requiredEnv("ASTROLABE_DEVICE_SECRET");
const timezone = Deno.env.get("LUNASAY_PROBE_TIMEZONE")?.trim() ||
  "America/Denver";
const epochSeconds = Number.parseInt(
  Deno.env.get("LUNASAY_PROBE_EPOCH_SECONDS") ?? "",
  10,
) || Math.floor(Date.now() / 1000);
const facts = (await Deno.readTextFile(factsPath)).trim();
const memoryPath = Deno.args[2]?.trim();
const readingMemory = memoryPath
  ? JSON.parse(await Deno.readTextFile(memoryPath)) as LunaSayReadingMemory
  : null;
const results: ProbeResult[] = [];

for (let index = 0; index < samples; index++) {
  const nonce = hex(crypto.getRandomValues(new Uint8Array(16)));
  const payload = `${mac}\n${nonce}\n${channel}\n`;
  const response = await fetch(`${url}/functions/v1/voice-pipeline`, {
    method: "POST",
    headers: {
      apikey: anon,
      Authorization: `Bearer ${anon}`,
      "Content-Type": "application/json",
      "X-Astrolabe-Device-Mac": mac,
      "X-Astrolabe-Device-Nonce": nonce,
      "X-Astrolabe-Device-Channel": channel,
      "X-Astrolabe-Device-Signature": await signature(secret, payload),
    },
    body: JSON.stringify({
      face: "lunasay_daily_packet",
      epochSeconds,
      timezone,
      briefingFacts: facts,
      ...(readingMemory ? { readingMemory } : {}),
    }),
  });
  const body = await response.json();
  if (!response.ok || !body.packet || !body.quality) {
    throw new Error(
      `sample ${index + 1} failed (${response.status}): ${
        JSON.stringify(body)
      }`,
    );
  }
  const continuityAppliedFaces = body.continuityAppliedFaces ?? [];
  const continuityDepthByFace = body.continuityDepthByFace ?? {};
  results.push({
    sample: index + 1,
    status: response.status,
    model: body.model ?? "unknown",
    llmCalls: body.llmCalls ?? 0,
    fallback: body.fallback === true,
    faceFallbacks: body.faceFallbacks ?? [],
    faceFallbackReasons: body.faceFallbackReasons ?? {},
    faceRetries: body.faceRetries ?? {},
    faceRetryReasons: body.faceRetryReasons ?? {},
    continuityAppliedFaces,
    continuityDepthByFace,
    continuityContractPassed: continuityContractPassed(
      body.packet,
      readingMemory,
      continuityAppliedFaces,
      continuityDepthByFace,
    ),
    actionContractPassed: actionContractPassed(body.packet),
    quality: body.quality,
  });
}

const scores = results.map((result) => result.quality.averageScore);
const minimums = results.map((result) => result.quality.minimumFaceScore);
const count = (predicate: (result: ProbeResult) => boolean) =>
  results.filter(predicate).length;
const aggregate = {
  samples: results.length,
  model: [...new Set(results.map((result) => result.model))],
  hardPassRate: count((result) => result.quality.hardGatePassed) /
    results.length,
  releasePassRate: count((result) => result.quality.releaseGatePassed) /
    results.length,
  actionContractPassRate: count((result) => result.actionContractPassed) /
    results.length,
  continuityRequested: readingMemory !== null,
  continuityPassRate: count((result) => result.continuityContractPassed) /
    results.length,
  fallbackRate: count((result) => result.fallback) / results.length,
  averageLlmCalls: Number(
    (
      results.reduce((sum, result) => sum + result.llmCalls, 0) / results.length
    ).toFixed(2),
  ),
  averageScore: Number(
    (scores.reduce((sum, score) => sum + score, 0) / scores.length).toFixed(1),
  ),
  p10Score: percentile(scores, 0.1),
  minimumObservedFaceScore: Math.min(...minimums),
};

console.log(JSON.stringify({ aggregate, results }, null, 2));
if (
  aggregate.hardPassRate < 1 ||
  aggregate.releasePassRate < 1 ||
  aggregate.actionContractPassRate < 1 ||
  aggregate.continuityPassRate < 1 ||
  aggregate.fallbackRate > 0
) {
  Deno.exit(1);
}
