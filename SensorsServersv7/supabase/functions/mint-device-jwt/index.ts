import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { verifyApiKey } from "../_shared/crypto.ts";
import { mintUserJwt, DEFAULT_EXPIRES_IN } from "../_shared/jwt.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { clientIp, enforceRateLimits } from "../_shared/ratelimit.ts";
import { serviceClient } from "../_shared/supabase.ts";

type MintBody = {
  device_mac?: string;
  api_key?: string;
  /** Optional override; clamped to 60..86400 seconds. */
  expires_in?: number;
};

function parseClientInet(ip: string): string | null {
  const s = (ip || "").trim();
  if (!s || s === "unknown") return null;
  if (s.length > 64) return null;
  return s;
}

/** Compare Edge client IP to devices.last_auth_public_ip (inet may include /prefix). */
function ipsEqual(a: string, b: string): boolean {
  const norm = (s: string) =>
    s.trim().toLowerCase().replace(/\/\d+$/, "");
  return norm(a) === norm(b);
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return optionsResponse();
  if (req.method !== "POST") {
    return jsonResponse({ error: "Method not allowed" }, 405);
  }

  try {
    let body: MintBody;
    try {
      body = await req.json();
    } catch {
      return jsonResponse({ error: "Invalid JSON body" }, 400);
    }

    const deviceMac = normalizeMac(body.device_mac ?? "");
    const rate = await enforceRateLimits(req, deviceMac);
    if (!rate.ok) {
      return jsonResponse({ error: rate.error, code: rate.code }, rate.status);
    }

    const apiKey = typeof body.api_key === "string" ? body.api_key.trim() : "";
    if (!deviceMac) {
      return jsonResponse({ error: "device_mac is required" }, 400);
    }

    let expiresIn = DEFAULT_EXPIRES_IN;
    if (typeof body.expires_in === "number" && Number.isFinite(body.expires_in)) {
      expiresIn = Math.min(86400, Math.max(60, Math.floor(body.expires_in)));
    }

    const publicIpRaw = clientIp(req);
    const publicIp = parseClientInet(publicIpRaw);

    const admin = serviceClient();
    const { data: device, error: findErr } = await admin
      .from("devices")
      .select("id, user_id, device_mac, api_key_hash, is_active, last_auth_public_ip")
      .eq("device_mac", deviceMac)
      .maybeSingle();

    if (findErr) {
      console.error("mint-device-jwt find:", findErr);
      return jsonResponse({ error: "Failed to look up device" }, 500);
    }

    // Same generic error for missing/inactive (avoid MAC enumeration).
    if (!device || !device.is_active) {
      return jsonResponse({
        error: "Invalid device credentials",
        code: "invalid_device",
      }, 401);
    }

    const keyOk = apiKey.length > 0 && verifyApiKey(apiKey, device.api_key_hash);
    let method: "api_key" | "ip_recovery" | null = null;

    if (keyOk) {
      method = "api_key";
    } else {
      const lastIp = device.last_auth_public_ip
        ? String(device.last_auth_public_ip).trim()
        : "";
      if (publicIp && lastIp && ipsEqual(publicIp, lastIp)) {
        method = "ip_recovery";
      } else {
        // Key missing/wrong and IP does not match last successful mint → re-claim.
        return jsonResponse({
          error: "Re-claim required: public IP does not match last auth for this device",
          code: "reclaim_required",
        }, 401);
      }
    }

    const token = await mintUserJwt({
      userId: device.user_id,
      deviceMac: device.device_mac,
      expiresInSec: expiresIn,
    });

    const nowIso = new Date().toISOString();
    const deviceUpdate: Record<string, unknown> = {
      last_seen_at: nowIso,
      last_auth_at: nowIso,
    };
    // Always refresh last_auth_public_ip on successful api_key mint.
    // On IP recovery, IP is already matching; still refresh last_auth_at.
    if (method === "api_key" && publicIp) {
      deviceUpdate.last_auth_public_ip = publicIp;
    } else if (method === "ip_recovery" && publicIp && !device.last_auth_public_ip) {
      deviceUpdate.last_auth_public_ip = publicIp;
    }

    const { error: seenErr } = await admin
      .from("devices")
      .update(deviceUpdate)
      .eq("id", device.id);
    if (seenErr) console.error("mint-device-jwt device update:", seenErr);

    const { error: logErr } = await admin.from("device_auth_log").insert({
      device_mac: device.device_mac,
      user_id: device.user_id,
      public_ip: publicIp,
      method,
    });
    if (logErr) console.error("mint-device-jwt auth_log:", logErr);

    return jsonResponse({
      access_token: token.accessToken,
      token_type: "bearer",
      expires_in: token.expiresIn,
      expires_at: token.expiresAt,
      user_id: device.user_id,
      device_mac: device.device_mac,
      auth_method: method,
    });
  } catch (e) {
    console.error("mint-device-jwt:", e);
    const msg = e instanceof Error ? e.message : "Internal error";
    if (msg.includes("Missing env: JWT_SECRET")) {
      return jsonResponse({
        error:
          "Server misconfigured: set Edge Function secret JWT_SECRET to your project's legacy JWT Secret (Project Settings → API)",
      }, 500);
    }
    return jsonResponse({ error: "Internal error" }, 500);
  }
});
