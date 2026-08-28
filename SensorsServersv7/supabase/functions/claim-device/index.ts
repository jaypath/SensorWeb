import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { currentBootstrapConfig } from "../_shared/bootstrap.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { enforceRateLimits } from "../_shared/ratelimit.ts";
import { serviceClient } from "../_shared/supabase.ts";

type ClaimBody = {
  device_mac?: string;
  claim_code?: string;
};

function normalizeClaimCode(raw: string): string | null {
  const c = raw.trim().toUpperCase().replace(/[^A-Z0-9]/g, "");
  if (c.length !== 4) return null;
  return c;
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return optionsResponse();
  if (req.method !== "POST") {
    return jsonResponse({ error: "Method not allowed" }, 405);
  }

  try {
    let body: ClaimBody;
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

    const claimCode = normalizeClaimCode(body.claim_code ?? "");
    if (!deviceMac || !claimCode) {
      return jsonResponse({
        error: "device_mac and claim_code (4 alphanumeric) are required",
      }, 400);
    }

    const admin = serviceClient();
    const live = currentBootstrapConfig();

    const { data: row, error: findErr } = await admin
      .from("device_provisioning")
      .select(
        "id, device_mac, user_id, user_email, api_key_plaintext, claim_code, expires_at, project_url, anon_key, mint_path, device_api_path, claim_path, api_version, site_slug, site_id",
      )
      .eq("device_mac", deviceMac)
      .eq("claim_code", claimCode)
      .maybeSingle();

    if (findErr) {
      console.error("claim-device find:", findErr);
      return jsonResponse({ error: "Lookup failed" }, 500);
    }

    if (!row) {
      return jsonResponse({ error: "Invalid claim credentials" }, 404);
    }

    const expiresMs = Date.parse(row.expires_at);
    if (!Number.isFinite(expiresMs) || expiresMs <= Date.now()) {
      await admin.from("device_provisioning").delete().eq("id", row.id);
      return jsonResponse({ error: "Invalid claim credentials" }, 404);
    }

    const { error: delErr } = await admin
      .from("device_provisioning")
      .delete()
      .eq("id", row.id);
    if (delErr) {
      console.error("claim-device delete:", delErr);
      return jsonResponse({ error: "Claim finalize failed" }, 500);
    }

    // Prefer live env for URL/anon (handles key rotation); fall back to staged snapshot.
    const projectUrl = live.project_url || row.project_url || "";
    const anonKey = live.anon_key || row.anon_key || "";

    return jsonResponse({
      ok: true,
      // Identity / secrets
      device_mac: row.device_mac,
      user_id: row.user_id,
      user_email: row.user_email,
      api_key: row.api_key_plaintext,
      // Supabase connection (enough to call mint + device-api)
      project_url: projectUrl,
      anon_key: anonKey,
      mint_path: row.mint_path || live.mint_path,
      device_api_path: row.device_api_path || live.device_api_path,
      claim_path: row.claim_path || live.claim_path,
      api_version: row.api_version ?? live.api_version,
      site_id: row.site_id ?? null,
      site_slug: row.site_slug || "home",
      // Convenience full URLs
      mint_url: projectUrl ? `${projectUrl}${row.mint_path || live.mint_path}` : null,
      device_api_url: projectUrl
        ? `${projectUrl}${row.device_api_path || live.device_api_path}`
        : null,
      warning:
        "Credentials delivered once; staging row deleted. Persist project_url, anon_key, device_mac, api_key, user_id, site_slug on device.",
    });
  } catch (e) {
    console.error("claim-device:", e);
    return jsonResponse({ error: "Internal error" }, 500);
  }
});
