import { createClient } from "npm:@supabase/supabase-js@2.49.8";

type DeviceRow = {
  mac: string;
  channel: string;
  device_secret: string;
  enabled: boolean;
};

const MAC_RE = /^[0-9a-f]{2}(:[0-9a-f]{2}){5}$/;
const HEX_RE = /^[0-9a-f]+$/;

function required(): boolean {
  return (Deno.env.get("ASTROLABE_DEVICE_AUTH_REQUIRED") ?? "").trim().toLowerCase() === "true";
}

function normalizeMac(value: string | null): string {
  const raw = (value ?? "").trim().toLowerCase();
  const compact = raw.replace(/[:-]/g, "");
  if (compact.length !== 12 || !HEX_RE.test(compact)) return "";
  const mac = compact.match(/.{1,2}/g)?.join(":") ?? "";
  return MAC_RE.test(mac) ? mac : "";
}

function hexToBytes(hex: string): Uint8Array | null {
  const value = hex.trim().toLowerCase();
  if (value.length === 0 || value.length % 2 !== 0 || !HEX_RE.test(value)) return null;
  const out = new Uint8Array(value.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = Number.parseInt(value.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqualHex(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) {
    diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return diff === 0;
}

async function hmacSha256Hex(secretHex: string, payload: string): Promise<string | null> {
  const secret = hexToBytes(secretHex);
  if (!secret) return null;
  const secretKey = new ArrayBuffer(secret.byteLength);
  new Uint8Array(secretKey).set(secret);
  const key = await crypto.subtle.importKey(
    "raw",
    secretKey,
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(payload));
  return bytesToHex(new Uint8Array(sig));
}

export async function verifyAstrolabeDevice(req: Request): Promise<Response | null> {
  if (!required()) return null;

  const url = Deno.env.get("SUPABASE_URL")?.trim() ?? "";
  const serviceRole = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")?.trim() ?? "";
  if (!url || !serviceRole) {
    return new Response(JSON.stringify({ error: "Device auth is not configured" }), {
      status: 500,
      headers: { "Content-Type": "application/json" },
    });
  }

  const mac = normalizeMac(req.headers.get("X-Astrolabe-Device-Mac"));
  const nonce = (req.headers.get("X-Astrolabe-Device-Nonce") ?? "").trim().toLowerCase();
  const signature = (req.headers.get("X-Astrolabe-Device-Signature") ?? "").trim().toLowerCase();
  const channel = (req.headers.get("X-Astrolabe-Device-Channel") ?? "").trim();
  if (!mac || nonce.length !== 32 || !HEX_RE.test(nonce) || signature.length !== 64 || !HEX_RE.test(signature) ||
    !channel) {
    return new Response(JSON.stringify({ error: "Missing or invalid device auth headers" }), {
      status: 401,
      headers: { "Content-Type": "application/json" },
    });
  }

  const admin = createClient(url, serviceRole, {
    auth: { autoRefreshToken: false, persistSession: false },
  });
  const { data, error } = await admin
    .from("astrolabe_devices")
    .select("mac,channel,device_secret,enabled")
    .eq("mac", mac)
    .eq("channel", channel)
    .maybeSingle<DeviceRow>();
  if (error || !data || !data.enabled) {
    return new Response(JSON.stringify({ error: "Device is not provisioned" }), {
      status: 403,
      headers: { "Content-Type": "application/json" },
    });
  }

  const payload = `${mac}\n${nonce}\n${channel}\n`;
  const expected = await hmacSha256Hex(data.device_secret, payload);
  if (!expected || !timingSafeEqualHex(expected, signature)) {
    return new Response(JSON.stringify({ error: "Invalid device signature" }), {
      status: 401,
      headers: { "Content-Type": "application/json" },
    });
  }
  return null;
}
