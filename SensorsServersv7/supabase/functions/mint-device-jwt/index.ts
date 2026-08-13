import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { verifyApiKey } from "../_shared/crypto.ts";
import { mintUserJwt, DEFAULT_EXPIRES_IN } from "../_shared/jwt.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { serviceClient } from "../_shared/supabase.ts";

type MintBody = {
  device_mac?: string;
  api_key?: string;
  /** Optional override; clamped to 60..86400 seconds. */
  expires_in?: number;
};

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
    const apiKey = typeof body.api_key === "string" ? body.api_key.trim() : "";
    if (!deviceMac || !apiKey) {
      return jsonResponse({ error: "device_mac and api_key are required" }, 400);
    }

    let expiresIn = DEFAULT_EXPIRES_IN;
    if (typeof body.expires_in === "number" && Number.isFinite(body.expires_in)) {
      expiresIn = Math.min(86400, Math.max(60, Math.floor(body.expires_in)));
    }

    const admin = serviceClient();
    const { data: device, error: findErr } = await admin
      .from("devices")
      .select("id, user_id, device_mac, api_key_hash, is_active")
      .eq("device_mac", deviceMac)
      .maybeSingle();

    if (findErr) {
      console.error("mint-device-jwt find:", findErr);
      return jsonResponse({ error: "Failed to look up device" }, 500);
    }

    // Same generic error for missing/inactive/bad key (avoid MAC enumeration).
    if (!device || !device.is_active || !verifyApiKey(apiKey, device.api_key_hash)) {
      return jsonResponse({ error: "Invalid device credentials" }, 401);
    }

    const token = await mintUserJwt({
      userId: device.user_id,
      deviceMac: device.device_mac,
      expiresInSec: expiresIn,
    });

    // Best-effort last_seen update; do not fail the mint if this errors.
    const { error: seenErr } = await admin
      .from("devices")
      .update({ last_seen_at: new Date().toISOString() })
      .eq("id", device.id);
    if (seenErr) console.error("mint-device-jwt last_seen:", seenErr);

    return jsonResponse({
      access_token: token.accessToken,
      token_type: "bearer",
      expires_in: token.expiresIn,
      expires_at: token.expiresAt,
      user_id: device.user_id,
      device_mac: device.device_mac,
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
