import { jsonResponse, optionsResponse } from "../_shared/cors.ts";
import { currentBootstrapConfig } from "../_shared/bootstrap.ts";
import { generateApiKey, generateClaimCode, hashApiKey } from "../_shared/crypto.ts";
import { normalizeMac } from "../_shared/mac.ts";
import { ensureSiteForUser } from "../_shared/sites.ts";
import { serviceClient, userClientFromAuthHeader } from "../_shared/supabase.ts";
import { ensureTrialSubscription, resolveTargetUser } from "../_shared/users.ts";

const PROVISION_TTL_DAYS = 180;

type BulkRow = {
  email?: string;
  user_id?: string;
  device_mac?: string;
  name?: string | null;
  /** Optional site slug (default "home"). */
  site?: string | null;
  site_name?: string | null;
};

type BulkBody = {
  devices?: BulkRow[];
  /** Default true: create trial subscription if target has none. */
  ensure_subscription?: boolean;
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
    const callerId = userData.user.id;

    const admin = serviceClient();
    const { data: isAdmin, error: adminErr } = await admin.rpc("is_admin", {
      p_uid: callerId,
    });
    if (adminErr) {
      console.error("provision-devices-bulk is_admin:", adminErr);
      return jsonResponse({ error: "Admin check failed" }, 500);
    }
    if (!isAdmin) {
      return jsonResponse({ error: "Forbidden: caller is not an admin" }, 403);
    }

    let body: BulkBody;
    try {
      body = await req.json();
    } catch {
      return jsonResponse({ error: "Invalid JSON body" }, 400);
    }

    const rows = Array.isArray(body.devices) ? body.devices : [];
    if (rows.length === 0) {
      return jsonResponse({ error: "devices array required" }, 400);
    }
    if (rows.length > 200) {
      return jsonResponse({ error: "Max 200 devices per request" }, 400);
    }

    const ensureSub = body.ensure_subscription !== false;
    const expiresAt = new Date(
      Date.now() + PROVISION_TTL_DAYS * 24 * 60 * 60 * 1000,
    ).toISOString();
    const bootstrap = currentBootstrapConfig();
    if (!bootstrap.project_url || !bootstrap.anon_key) {
      return jsonResponse({
        error: "Server misconfigured: SUPABASE_URL / SUPABASE_ANON_KEY missing",
      }, 500);
    }

    const results: unknown[] = [];

    for (const row of rows) {
      const deviceMac = normalizeMac(row.device_mac ?? "");
      if (!deviceMac) {
        results.push({
          ok: false,
          error: "invalid_device_mac",
          device_mac: row.device_mac ?? null,
        });
        continue;
      }

      try {
        // Skip if MAC already registered on devices table
        const { data: existingDev, error: findErr } = await admin
          .from("devices")
          .select("id, user_id, is_active")
          .eq("device_mac", deviceMac)
          .maybeSingle();
        if (findErr) throw findErr;

        if (existingDev) {
          results.push({
            ok: false,
            error: "already_registered",
            device_mac: deviceMac,
            device_id: existingDev.id,
            user_id: existingDev.user_id,
          });
          continue;
        }

        const target = await resolveTargetUser(admin, {
          email: row.email,
          user_id: row.user_id,
        });

        if (ensureSub) {
          await ensureTrialSubscription(admin, target.userId);
        }

        const site = await ensureSiteForUser(
          admin,
          target.userId,
          row.site,
          row.site_name,
        );

        const { apiKey, prefix } = generateApiKey();
        const apiKeyHash = hashApiKey(apiKey);
        const claimCode = generateClaimCode();
        const name = row.name?.trim() || null;

        const { data: createdDev, error: insDevErr } = await admin
          .from("devices")
          .insert({
            user_id: target.userId,
            device_mac: deviceMac,
            api_key_hash: apiKeyHash,
            api_key_prefix: prefix,
            name,
            is_active: true,
            site_id: site.id,
          })
          .select("id, device_mac, api_key_prefix, name, is_active, created_at, site_id")
          .single();

        if (insDevErr || !createdDev) {
          throw insDevErr || new Error("device insert failed");
        }

        // Replace any stale provisioning row for this MAC
        await admin.from("device_provisioning").delete().eq("device_mac", deviceMac);

        const { data: staged, error: stageErr } = await admin
          .from("device_provisioning")
          .insert({
            device_mac: deviceMac,
            user_id: target.userId,
            user_email: target.email || (row.email || ""),
            api_key_plaintext: apiKey,
            claim_code: claimCode,
            expires_at: expiresAt,
            created_by: callerId,
            project_url: bootstrap.project_url,
            anon_key: bootstrap.anon_key,
            mint_path: bootstrap.mint_path,
            device_api_path: bootstrap.device_api_path,
            claim_path: bootstrap.claim_path,
            api_version: bootstrap.api_version,
            site_slug: site.slug,
            site_id: site.id,
          })
          .select(
            "id, device_mac, user_id, user_email, claim_code, expires_at, created_at, project_url, mint_path, device_api_path, claim_path, api_version, site_slug, site_id",
          )
          .single();

        if (stageErr || !staged) {
          throw stageErr || new Error("provisioning insert failed");
        }

        results.push({
          ok: true,
          device_mac: deviceMac,
          device_id: createdDev.id,
          user_id: target.userId,
          user_email: staged.user_email,
          user_created: target.userCreated,
          temporary_password: target.temporaryPassword,
          claim_code: claimCode,
          api_key: apiKey,
          api_key_prefix: prefix,
          expires_at: expiresAt,
          name,
          site_id: site.id,
          site_slug: site.slug,
          site_name: site.name,
          project_url: bootstrap.project_url,
          anon_key: bootstrap.anon_key,
          mint_path: bootstrap.mint_path,
          device_api_path: bootstrap.device_api_path,
          claim_path: bootstrap.claim_path,
          api_version: bootstrap.api_version,
        });
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        console.error("provision-devices-bulk row:", deviceMac, msg);
        results.push({
          ok: false,
          error: "row_failed",
          device_mac: deviceMac,
          detail: msg,
        });
      }
    }

    const succeeded = results.filter((r) => (r as { ok?: boolean }).ok).length;
    return jsonResponse({
      ok: true,
      count: rows.length,
      succeeded,
      failed: rows.length - succeeded,
      expires_at: expiresAt,
      results,
      warning:
        "Store claim_code + api_key securely. Devices claim via claim-device; staging row is deleted after claim. Plaintext is not readable via PostgREST.",
    });
  } catch (e) {
    console.error("provision-devices-bulk:", e);
    return jsonResponse({ error: "Internal error" }, 500);
  }
});
