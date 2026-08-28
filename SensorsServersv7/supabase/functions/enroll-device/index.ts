import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { generateApiKey, hashApiKey } from "../_shared/crypto.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { ensureSiteForUser } from "../_shared/sites.ts";
import { serviceClient, userClientFromAuthHeader } from "../_shared/supabase.ts";

type EnrollBody = {
  device_mac?: string;
  name?: string | null;
  /** Optional site slug (default "home"). */
  site?: string | null;
  site_name?: string | null;
  /** If true and device already belongs to this user, issue a new API key. */
  rotate?: boolean;
};

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return optionsResponse();
  if (req.method !== "POST") {
    return jsonResponse({ error: "Method not allowed" }, 405);
  }

  try {
    const authHeader = req.headers.get("Authorization");
    if (!authHeader?.startsWith("Bearer ")) {
      return jsonResponse({ error: "Missing Authorization Bearer token" }, 401);
    }

    const userSb = userClientFromAuthHeader(authHeader);
    const { data: userData, error: userErr } = await userSb.auth.getUser();
    if (userErr || !userData.user) {
      return jsonResponse({ error: "Invalid or expired user session" }, 401);
    }
    const userId = userData.user.id;

    let body: EnrollBody;
    try {
      body = await req.json();
    } catch {
      return jsonResponse({ error: "Invalid JSON body" }, 400);
    }

    const deviceMac = normalizeMac(body.device_mac ?? "");
    if (!deviceMac) {
      return jsonResponse({
        error: "device_mac required (12 hex digits, separators optional)",
      }, 400);
    }

    const admin = serviceClient();
    const site = await ensureSiteForUser(admin, userId, body.site, body.site_name);

    const { data: existing, error: findErr } = await admin
      .from("devices")
      .select("id, user_id, is_active, site_id")
      .eq("device_mac", deviceMac)
      .maybeSingle();

    if (findErr) {
      console.error("enroll-device find:", findErr);
      return jsonResponse({ error: "Failed to look up device" }, 500);
    }

    if (existing && existing.user_id !== userId) {
      return jsonResponse({ error: "Device MAC is registered to another user" }, 403);
    }

    if (existing && !body.rotate) {
      return jsonResponse({
        error: "Device already enrolled. Pass rotate:true to issue a new API key.",
        device_id: existing.id,
      }, 409);
    }

    const { apiKey, prefix } = generateApiKey();
    const apiKeyHash = hashApiKey(apiKey);
    const name = body.name?.trim() || null;

    if (existing && body.rotate) {
      const { data: updated, error: updErr } = await admin
        .from("devices")
        .update({
          api_key_hash: apiKeyHash,
          api_key_prefix: prefix,
          name: name ?? undefined,
          is_active: true,
          site_id: site.id,
        })
        .eq("id", existing.id)
        .eq("user_id", userId)
        .select("id, device_mac, api_key_prefix, name, is_active, created_at, site_id")
        .single();

      if (updErr || !updated) {
        console.error("enroll-device rotate:", updErr);
        return jsonResponse({ error: "Failed to rotate device key" }, 500);
      }

      return jsonResponse({
        ...updated,
        api_key: apiKey,
        site_slug: site.slug,
        site_name: site.name,
        rotated: true,
        warning: "Store api_key on the device now; it cannot be retrieved again.",
      });
    }

    const { data: created, error: insErr } = await admin
      .from("devices")
      .insert({
        user_id: userId,
        device_mac: deviceMac,
        api_key_hash: apiKeyHash,
        api_key_prefix: prefix,
        name,
        is_active: true,
        site_id: site.id,
      })
      .select("id, device_mac, api_key_prefix, name, is_active, created_at, site_id")
      .single();

    if (insErr || !created) {
      console.error("enroll-device insert:", insErr);
      return jsonResponse({ error: "Failed to enroll device" }, 500);
    }

    return jsonResponse({
      ...created,
      api_key: apiKey,
      site_slug: site.slug,
      site_name: site.name,
      rotated: false,
      warning: "Store api_key on the device now; it cannot be retrieved again.",
    }, 201);
  } catch (e) {
    console.error("enroll-device:", e);
    return jsonResponse({ error: "Internal error" }, 500);
  }
});
