import { getConfig } from "./config.js";
import {
  SYNastry_BOOT_USER_MSG,
  SYNastry_VOICE_SYS,
  buildVoiceSnapshot,
} from "./synastry-math.js";

function authHeaders() {
  const c = getConfig();
  const token = c.accessToken || c.supabaseAnonKey;
  return {
    Authorization: `Bearer ${token}`,
    apikey: c.supabaseAnonKey,
    "Content-Type": "application/json",
  };
}

function parseAudioBase64(json) {
  const m = json.match(/"audioBase64"\s*:\s*"([^"]+)"/);
  if (!m) return null;
  const b64 = m[1];
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

function extractField(json, key) {
  const re = new RegExp(`"${key}"\\s*:\\s*"((?:\\\\.|[^"\\\\])*)"`);
  const m = json.match(re);
  if (!m) return "";
  return m[1].replace(/\\n/g, "\n").replace(/\\"/g, '"').replace(/\\\\/g, "\\");
}

export async function postVoiceMessage(message, systemInstruction) {
  const c = getConfig();
  if (!c.supabaseUrl || !c.supabaseAnonKey) {
    throw new Error("Add Supabase URL and anon key in Settings for voice.");
  }
  const body = {
    languageCode: "en-US",
    message,
    ...(systemInstruction ? { systemInstruction } : {}),
  };
  const res = await fetch(`${c.supabaseUrl}/functions/v1/voice-pipeline`, {
    method: "POST",
    headers: authHeaders(),
    body: JSON.stringify(body),
  });
  const text = await res.text();
  if (!res.ok) {
    if (res.status === 401) throw new Error("Sign in on Castalia for full voice access.");
    throw new Error(`Voice HTTP ${res.status}`);
  }
  return {
    transcript: extractField(text, "transcript"),
    reply: extractField(text, "reply"),
    audio: parseAudioBase64(text),
  };
}

export async function postVoicePcm(pcmBase64, systemInstruction) {
  const c = getConfig();
  if (!c.supabaseUrl || !c.supabaseAnonKey) {
    throw new Error("Add Supabase URL and anon key in Settings for voice.");
  }
  const body = {
    languageCode: "en-US",
    audioContent: pcmBase64,
    audioEncoding: "LINEAR16",
    sampleRateHertz: 16000,
    ...(systemInstruction ? { systemInstruction } : {}),
  };
  const res = await fetch(`${c.supabaseUrl}/functions/v1/voice-pipeline`, {
    method: "POST",
    headers: authHeaders(),
    body: JSON.stringify(body),
  });
  const text = await res.text();
  if (!res.ok) {
    if (res.status === 422) throw new Error("No speech heard — try again.");
    if (res.status === 401) throw new Error("Sign in on Castalia for full voice access.");
    throw new Error(`Voice HTTP ${res.status}`);
  }
  return {
    transcript: extractField(text, "transcript"),
    reply: extractField(text, "reply"),
    audio: parseAudioBase64(text),
  };
}

export function buildSynastryPrompts(userBirth, target, userPos, targetPos, aspects) {
  const snapshot = buildVoiceSnapshot(userBirth, target, userPos, targetPos, aspects);
  const system = `${SYNastry_VOICE_SYS}\n\nSynastry chart snapshot:\n${snapshot}`;
  return { snapshot, system };
}

export async function requestBriefReading(userBirth, target, userPos, targetPos, aspects) {
  const { system } = buildSynastryPrompts(userBirth, target, userPos, targetPos, aspects);
  return postVoiceMessage(SYNastry_BOOT_USER_MSG, system);
}

export function playMp3(bytes) {
  const blob = new Blob([bytes], { type: "audio/mpeg" });
  const url = URL.createObjectURL(blob);
  const audio = new Audio(url);
  audio.playsInline = true;
  return new Promise((resolve, reject) => {
    audio.onended = () => {
      URL.revokeObjectURL(url);
      resolve();
    };
    audio.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Playback failed"));
    };
    audio.play().catch(reject);
  });
}
